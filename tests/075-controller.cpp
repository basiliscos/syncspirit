// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"

#include "model/cluster.h"
#include "model/diff/contact/peer_state.h"
#include "diff-builder.h"
#include "hasher/hasher_proxy_actor.h"
#include "hasher/hasher_actor.h"
#include "net/controller_actor.h"
#include "net/names.h"
#include "fs/messages.h"
#include "utils/error_code.h"
#include "utils/tls.h"
#include "proto/bep_support.h"
#include <boost/core/demangle.hpp>
#include <type_traits>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::hasher;

namespace {

struct sample_peer_config_t : public r::actor_config_t {
    model::device_id_t peer_device_id;
    bool auto_share = false;
};

template <typename Actor> struct sample_peer_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&peer_device_id(const model::device_id_t &value) && noexcept {
        parent_t::config.peer_device_id = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&auto_share(bool value) && noexcept {
        parent_t::config.auto_share = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct sample_peer_t : r::actor_base_t {
    using config_t = sample_peer_config_t;
    template <typename Actor> using config_builder_t = sample_peer_config_builder_t<Actor>;

    using remote_message_t = r::intrusive_ptr_t<net::message::forwarded_message_t>;
    using remote_messages_t = std::list<remote_message_t>;

    struct block_response_t {
        std::string name;
        size_t block_index;
        std::string data;
        sys::error_code ec;
    };
    using allowed_index_updates_t = std::unordered_set<std::string>;
    using block_responses_t = std::list<block_response_t>;
    using block_request_t = r::intrusive_ptr_t<net::message::block_request_t>;
    using block_requests_t = std::list<block_request_t>;
    using uploaded_blocks_t = std::list<proto::message::Response>;

    sample_peer_t(config_t &config)
        : r::actor_base_t{config}, auto_share(config.auto_share), peer_device{config.peer_device_id} {
        log = utils::get_logger("test.sample_peer");
    }

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("sample_peer", false); });
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&sample_peer_t::on_start_reading);
            p.subscribe_actor(&sample_peer_t::on_termination);
            p.subscribe_actor(&sample_peer_t::on_transfer);
            p.subscribe_actor(&sample_peer_t::on_block_request);
        });
    }

    void shutdown_start() noexcept override {
        LOG_TRACE(log, "{}, shutdown_start", identity);
        if (controller) {
            send<net::payload::termination_t>(controller, shutdown_reason);
        }
        r::actor_base_t::shutdown_start();
    }

    void shutdown_finish() noexcept override {
        r::actor_base_t::shutdown_finish();
        LOG_TRACE(log, "{}, shutdown_finish, blocks requested = {}", identity, blocks_requested);
        if (controller) {
            send<net::payload::termination_t>(controller, shutdown_reason);
        }
    }

    void on_start_reading(net::message::start_reading_t &msg) noexcept {
        LOG_TRACE(log, "{}, on_start_reading", identity);
        controller = msg.payload.controller;
        reading = msg.payload.start;
    }

    void on_termination(net::message::termination_signal_t &msg) noexcept {
        LOG_TRACE(log, "{}, on_termination", identity);

        if (!shutdown_reason) {
            auto &ee = msg.payload.ee;
            auto reason = ee->message();
            LOG_TRACE(log, "{}, on_termination: {}", identity, reason);

            do_shutdown(ee);
        }
    }

    void on_transfer(net::message::transfer_data_t &message) noexcept {
        auto &data = message.payload.data;
        LOG_TRACE(log, "{}, on_transfer, bytes = {}", identity, data.size());
        auto buff = boost::asio::buffer(data.data(), data.size());
        auto result = proto::parse_bep(buff);
        auto orig = std::move(result.value().message);
        auto variant = net::payload::forwarded_message_t();
        std::visit(
            [&](auto &msg) {
                using boost::core::demangle;
                using T = std::decay_t<decltype(msg)>;
                LOG_TRACE(log, "{}, received '{}' message", identity, demangle(typeid(T).name()));
                using V = net::payload::forwarded_message_t;
                if constexpr (std::is_constructible_v<V, T>) {
                    variant = std::move(msg);
                } else if constexpr (std::is_same_v<T, proto::message::Response>) {
                    uploaded_blocks.push_back(std::move(msg));
                }
            },
            orig);
        auto fwd_msg = new net::message::forwarded_message_t(address, std::move(variant));
        messages.emplace_back(fwd_msg);

        for (auto &msg : messages) {
            auto &p = msg->payload;
            if (auto m = std::get_if<proto::message::Index>(&p); m) {
                allowed_index_updates.emplace((*m)->folder());
            }
            if (auto m = std::get_if<proto::message::IndexUpdate>(&p); m) {
                auto &folder_id = (*m)->folder();
                if ((allowed_index_updates.count(folder_id) == 0) && !auto_share) {
                    LOG_WARN(log, "{}, IndexUpdate w/o previously recevied index", identity);
                    std::abort();
                }
            }
        }
    }

    void process_block_requests() noexcept {
        auto condition = [&]() -> bool {
            if (block_requests.size() && block_responses.size()) {
                auto &req = block_requests.front();
                auto &res = block_responses.front();
                auto &req_payload = req->payload.request_payload;
                if (req_payload.block_index == res.block_index) {
                    auto &name = res.name;
                    return name.empty() || name == req_payload.file_name;
                }
            }
            return false;
        };
        while (condition()) {
            auto &reply = block_responses.front();
            auto &request = *block_requests.front();
            log->debug("{}, matched '{}', replying..., ec = {}", identity, reply.name, reply.ec.value());
            if (!reply.ec) {
                reply_to(request, reply.data);
            } else {
                reply_with_error(request, make_error(reply.ec));
            }
            block_responses.pop_front();
            block_requests.pop_front();
        }
    }

    void on_block_request(net::message::block_request_t &req) noexcept {
        block_requests.push_front(&req);
        ++blocks_requested;
        log->debug("{}, requesting block # {}", identity, block_requests.front()->payload.request_payload.block_index);
        if (block_responses.size()) {
            log->debug("{}, top response block # {}", identity, block_responses.front().block_index);
        }
        process_block_requests();
    }

    void forward(net::payload::forwarded_message_t payload) noexcept {
        send<net::payload::forwarded_message_t>(controller, std::move(payload));
    }

    static const constexpr size_t next_block = 1000000;

    void push_block(std::string_view data, size_t index, std::string name = {}) {
        if (index == next_block) {
            index = block_responses.size();
        }
        block_responses.push_back(block_response_t{std::move(name), index, std::string(data), {}});
    }

    void push_block(sys::error_code ec, size_t index) {
        if (index == next_block) {
            index = block_responses.size();
        }
        block_responses.push_back(block_response_t{std::string{}, index, std::string{}, ec});
    }

    int blocks_requested = 0;
    bool reading = false;
    bool auto_share = false;
    remote_messages_t messages;
    r::address_ptr_t controller;
    model::device_id_t peer_device;
    utils::logger_t log;
    block_requests_t block_requests;
    block_responses_t block_responses;
    uploaded_blocks_t uploaded_blocks;
    allowed_index_updates_t allowed_index_updates;
};

struct hasher_config_t : hasher::hasher_actor_config_t {
    uint32_t index;
    bool auto_reply = true;
};

template <typename Actor> struct hasher_config_builder_t : hasher::hasher_actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = ::hasher_actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&auto_reply(uint32_t value) && noexcept {
        parent_t::config.auto_reply = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct managed_hasher_t : r::actor_base_t {
    using config_t = hasher_config_t;
    template <typename Actor> using config_builder_t = hasher_config_builder_t<Actor>;

    using validation_request_t = hasher::message::validation_request_t;
    using validation_request_ptr_t = model::intrusive_ptr_t<validation_request_t>;
    using queue_t = std::deque<validation_request_ptr_t>;

    managed_hasher_t(config_t &cfg) : r::actor_base_t{cfg}, index{cfg.index}, auto_reply{cfg.auto_reply} {}

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
            p.set_identity(fmt::format("hasher-{}", 1), false);
            log = utils::get_logger(fmt::format("test-hasher-{}", 1));
        });
        plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(identity, get_address()); });
        plugin.with_casted<r::plugin::starter_plugin_t>(
            [&](auto &p) { p.subscribe_actor(&managed_hasher_t::on_validation); });
    }
    void on_validation(validation_request_t &req) noexcept {
        queue.emplace_back(&req);
        if (auto_reply) {
            process_requests();
        }
    }
    void process_requests() noexcept {
        static const constexpr size_t SZ = SHA256_DIGEST_LENGTH;

        LOG_TRACE(log, "{}, process_requests", identity);
        while (!queue.empty()) {
            auto req = queue.front();
            queue.pop_front();
            auto &payload = *req->payload.request_payload;

            char digest[SZ];
            auto &data = payload.data;

            utils::digest(data.data(), data.length(), digest);
            bool eq = payload.hash == std::string_view(digest, SZ);
            reply_to(*req, eq);
        }
    }

    uint32_t index;
    bool auto_reply;
    utils::logger_t log;
    queue_t queue;
};

struct fixture_t {
    using peer_ptr_t = r::intrusive_ptr_t<sample_peer_t>;
    using target_ptr_t = r::intrusive_ptr_t<net::controller_actor_t>;
    using blk_req_t = fs::message::block_request_t;
    using blk_req_ptr_t = r::intrusive_ptr_t<blk_req_t>;
    using blk_res_t = fs::message::block_response_t;
    using blk_res_ptr_t = r::intrusive_ptr_t<blk_res_t>;
    using block_requests_t = std::deque<blk_req_ptr_t>;
    using block_responses_t = std::deque<r::message_ptr_t>;

    fixture_t(bool auto_start_, int64_t max_sequence_, bool auto_share_ = true) noexcept
        : auto_start{auto_start_}, auto_share{auto_share_}, max_sequence{max_sequence_} {
        test::init_logging();
    }

    void _start_target(std::string connection_id) {
        peer_actor = sup->create_actor<sample_peer_t>().auto_share(auto_share).timeout(timeout).finish();

        auto diff = model::diff::contact::peer_state_t::create(*cluster, peer_device->device_id().get_sha256(),
                                                               peer_actor->get_address(), device_state_t::online,
                                                               connection_id);
        sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);

        target = sup->create_actor<controller_actor_t>()
                     .peer(peer_device)
                     .peer_addr(peer_actor->get_address())
                     .connection_id(connection_id)
                     .request_pool(1024)
                     .outgoing_buffer_max(1024'000)
                     .cluster(cluster)
                     .sequencer(sup->sequencer)
                     .timeout(timeout)
                     .request_timeout(timeout)
                     .blocks_max_requested(get_blocks_max_requested())
                     .finish();

        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
        target_addr = target->get_address();
    }

    virtual void start_target() noexcept { _start_target("test-common://1.2.3.4:5"); }

    virtual void run() noexcept {
        auto peer_id =
            device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        auto my_id =
            device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        my_device = device_t::create(my_id, "my-device").value();
        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        auto folder_id_1 = "1234-5678";
        auto folder_id_2 = "5555";
        auto builder = diff_builder_t(*cluster);
        auto sha256 = peer_id.get_sha256();
        builder.upsert_folder(folder_id_1, "")
            .upsert_folder(folder_id_2, "")
            .configure_cluster(sha256)
            .add(sha256, folder_id_1, 123, max_sequence)
            .finish();
        REQUIRE(builder.apply());

        if (auto_share) {
            REQUIRE(builder.share_folder(peer_id.get_sha256(), folder_id_1).apply());
        }

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::registry_plugin_t>(
                [&](auto &p) { p.register_name(net::names::fs_actor, sup->get_address()); });
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<blk_req_t>([&](blk_req_t &msg) {
                    block_requests.push_back(&msg);
                    if (block_responses.size()) {
                        sup->put(block_responses.front());
                        block_responses.pop_front();
                    }
                }));
            });
        };
        sup->start();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);
        create_hasher();
        sup->create_actor<hasher::hasher_proxy_actor_t>()
            .timeout(timeout)
            .hasher_threads(1)
            .name(net::names::hasher_proxy)
            .finish();

        auto &folders = cluster->get_folders();
        folder_1 = folders.by_id(folder_id_1);
        folder_2 = folders.by_id(folder_id_2);

        folder_1_peer = folder_1->get_folder_infos().by_device_id(peer_id.get_sha256());

        start_target();

        if (auto_start) {
            REQUIRE(peer_actor->reading);
            REQUIRE(peer_actor->messages.size() == 1);
            auto &msg = (*peer_actor->messages.front()).payload;
            REQUIRE(std::get_if<proto::message::ClusterConfig>(&msg));
            peer_actor->messages.pop_front();
        }
        main(builder);

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void create_hasher() noexcept { sup->create_actor<hasher_actor_t>().index(1).timeout(timeout).finish(); }

    virtual void main(diff_builder_t &) noexcept {}

    virtual std::uint32_t get_blocks_max_requested() { return 8; }

    bool auto_start;
    bool auto_share;
    int64_t max_sequence;
    peer_ptr_t peer_actor;
    target_ptr_t target;
    r::address_ptr_t target_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    device_ptr_t peer_device;
    device_ptr_t my_device;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::system_context_t ctx;
    model::folder_ptr_t folder_1;
    model::folder_info_ptr_t folder_1_peer;
    model::folder_ptr_t folder_2;
    block_requests_t block_requests;
    block_responses_t block_responses;
};

} // namespace

void test_startup() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            REQUIRE(peer_actor->reading);
            REQUIRE(peer_actor->messages.size() == 1);
            auto &msg = (*peer_actor->messages.front()).payload;
            REQUIRE(std::get_if<proto::message::ClusterConfig>(&msg));

            peer_actor->messages.pop_front();
            CHECK(peer_actor->messages.empty());

            auto cc = proto::ClusterConfig{};
            auto payload = proto::message::ClusterConfig(new proto::ClusterConfig(cc));
            peer_actor->forward(std::move(payload));
            sup->do_process();

            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            CHECK(peer_actor->messages.empty());
        }
    };
    F(false, 10, false).run();
}

void test_overwhelm() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        void main(diff_builder_t &) noexcept override {
            auto msg = &(*peer_actor->messages.front()).payload;
            REQUIRE(std::get_if<proto::message::ClusterConfig>(msg));

            peer_actor->messages.pop_front();
            CHECK(peer_actor->messages.empty());

            auto cc = proto::ClusterConfig{};
            auto payload = proto::message::ClusterConfig(new proto::ClusterConfig(cc));
            peer_actor->forward(std::move(payload));
            sup->do_process();

            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            CHECK(peer_actor->messages.empty());

            auto ex_peer = peer_actor;
            auto ex_target = target;

            _start_target("best://1.2.3.4:5");
            sup->do_process();

            REQUIRE(ex_peer != peer_actor);
            REQUIRE(ex_target != target);
            CHECK(static_cast<r::actor_base_t *>(ex_peer.get())->access<to::state>() == r::state_t::SHUT_DOWN);
            CHECK(static_cast<r::actor_base_t *>(ex_target.get())->access<to::state>() == r::state_t::SHUT_DOWN);

            msg = &(*peer_actor->messages.front()).payload;
            REQUIRE(std::get_if<proto::message::ClusterConfig>(msg));
            peer_actor->messages.pop_front();

            payload = proto::message::ClusterConfig(new proto::ClusterConfig(cc));
            peer_actor->forward(std::move(payload));
            sup->do_process();

            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
            CHECK(peer_actor->messages.empty());
        }
    };
    F(false, 10, false).run();
}

void test_index_receiving() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {

            auto cc = proto::ClusterConfig{};
            auto index = proto::Index{};

            SECTION("wrong index") {
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));

                index.set_folder("non-existing-folder");
                peer_actor->forward(proto::message::Index(new proto::Index(index)));
                sup->do_process();

                CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::SHUT_DOWN);
                CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
            }

            SECTION("index is applied") {
                auto folder = cc.add_folders();
                folder->set_id(std::string(folder_1->get_id()));
                auto d_peer = folder->add_devices();
                d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
                REQUIRE(cluster->get_pending_folders().size() == 0);
                d_peer->set_max_sequence(10);
                d_peer->set_index_id(folder_1_peer->get_index());
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));

                index.set_folder(std::string(folder_1->get_id()));
                auto file = index.add_files();
                file->set_name("some-dir");
                file->set_type(proto::FileInfoType::DIRECTORY);
                file->set_sequence(10);
                auto v = file->mutable_version();
                auto c = v->add_counters();
                c->set_id(peer_device->device_id().get_uint());
                c->set_value(1);

                peer_actor->forward(proto::message::Index(new proto::Index(index)));
                sup->do_process();

                CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
                CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

                auto &folder_infos = folder_1->get_folder_infos();

                auto folder_peer = folder_infos.by_device(*peer_device);
                REQUIRE(folder_peer);
                CHECK(folder_peer->get_max_sequence() == 10ul);
                REQUIRE(folder_peer->get_file_infos().size() == 1);
                CHECK(folder_peer->get_file_infos().begin()->item->get_name() == file->name());

                auto folder_my = folder_infos.by_device(*my_device);
                REQUIRE(folder_my);
                CHECK(folder_my->get_max_sequence() == 1ul);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                CHECK(folder_my->get_file_infos().begin()->item->get_name() == file->name());

                SECTION("then index update is applied") {
                    auto index_update = proto::IndexUpdate{};
                    index_update.set_folder(std::string(folder_1->get_id()));
                    auto file = index_update.add_files();
                    file->set_name("some-dir-2");
                    file->set_type(proto::FileInfoType::DIRECTORY);
                    file->set_sequence(folder_1_peer->get_max_sequence() + 1);
                    auto v = file->mutable_version();
                    auto c = v->add_counters();
                    c->set_id(peer_device->device_id().get_uint());
                    c->set_value(1);

                    peer_actor->forward(proto::message::IndexUpdate(new proto::IndexUpdate(index_update)));

                    sup->do_process();
                    CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
                    CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() ==
                          r::state_t::OPERATIONAL);

                    CHECK(folder_peer->get_max_sequence() == file->sequence());
                    REQUIRE(folder_peer->get_file_infos().size() == 2);
                    CHECK(folder_peer->get_file_infos().by_name("some-dir-2"));

                    CHECK(folder_my->get_max_sequence() == 2ul);
                    REQUIRE(folder_my->get_file_infos().size() == 2);
                    CHECK(folder_my->get_file_infos().by_name("some-dir-2"));
                }
            }
        }
    };
    F(true, 10).run();
}

void test_index_sending() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {

            proto::FileInfo pr_file_info;
            pr_file_info.set_name("link");
            pr_file_info.set_type(proto::FileInfoType::SYMLINK);
            pr_file_info.set_symlink_target("/some/where");

            auto builder = diff_builder_t(*cluster);
            builder.local_update(folder_1->get_id(), pr_file_info);
            builder.apply(*sup);

            auto folder_1_my = folder_1->get_folder_infos().by_device(*my_device);

            auto cc = proto::ClusterConfig{};

            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            auto d_peer = folder->add_devices();
            d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
            d_peer->set_max_sequence(folder_1_peer->get_max_sequence());
            d_peer->set_index_id(folder_1_peer->get_index());

            SECTION("peer has outdated by sequence view") {
                auto d_my = folder->add_devices();
                d_my->set_id(std::string(my_device->device_id().get_sha256()));
                d_my->set_max_sequence(folder_1_my->get_max_sequence() - 1);
                d_my->set_index_id(folder_1_my->get_index());

                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                sup->do_process();

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() == 1);
                auto msg = &(*queue.front()).payload;
                auto &my_index_update = *std::get<proto::message::IndexUpdate>(*msg);
                REQUIRE(my_index_update.files_size() == 1);
            }

            SECTION("peer has outdated by index view") {
                auto d_my = folder->add_devices();
                d_my->set_id(std::string(my_device->device_id().get_sha256()));
                d_my->set_max_sequence(folder_1_my->get_max_sequence());
                d_my->set_index_id(folder_1_my->get_index() + 5);

                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                sup->do_process();

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() == 2);
                auto msg = &(*queue.front()).payload;
                auto &my_index = *std::get<proto::message::Index>(*msg);
                REQUIRE(my_index.files_size() == 0);
                queue.pop_front();

                msg = &(*queue.front()).payload;
                auto &my_index_update = *std::get<proto::message::IndexUpdate>(*msg);
                REQUIRE(my_index_update.files_size() == 1);
            }

            SECTION("peer has actual view") {
                auto d_my = folder->add_devices();
                d_my->set_id(std::string(my_device->device_id().get_sha256()));
                d_my->set_max_sequence(folder_1_my->get_max_sequence());
                d_my->set_index_id(folder_1_my->get_index());

                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                sup->do_process();

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() == 0);
            }
        }
    };
    F(true, 10).run();
}

void test_downloading() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_my = folder_infos.by_device(*my_device);

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            auto d_peer = folder->add_devices();
            d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
            d_peer->set_max_sequence(10);
            d_peer->set_index_id(folder_1_peer->get_index());
            auto d_my = folder->add_devices();
            d_my->set_id(std::string(my_device->device_id().get_sha256()));
            d_my->set_max_sequence(folder_my->get_max_sequence());
            d_my->set_index_id(folder_my->get_index());

            SECTION("cluster config & index has a new file => download it") {
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                auto index = proto::Index{};
                index.set_folder(std::string(folder_1->get_id()));
                auto file = index.add_files();
                file->set_name("some-file");
                file->set_type(proto::FileInfoType::FILE);
                file->set_sequence(folder_1_peer->get_max_sequence() + 1);
                file->set_block_size(5);
                file->set_size(5);
                auto version = file->mutable_version();
                auto counter = version->add_counters();
                counter->set_id(1ul);
                counter->set_value(1ul);

                auto b1 = file->add_blocks();
                b1->set_hash(utils::sha256_digest("12345").value());
                b1->set_offset(0);
                b1->set_size(5);

                auto folder_my = folder_infos.by_device(*my_device);
                CHECK(folder_my->get_max_sequence() == 0ul);
                CHECK(!folder_my->get_folder()->is_synchronizing());

                peer_actor->forward(proto::message::Index(new proto::Index(index)));
                sup->do_process();
                CHECK(folder_my->get_folder()->is_synchronizing());

                peer_actor->push_block("12345", 0);
                peer_actor->process_block_requests();
                sup->do_process();

                CHECK(!folder_my->get_folder()->is_synchronizing());
                REQUIRE(folder_my);
                CHECK(folder_my->get_max_sequence() == 1ul);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == file->name());
                CHECK(f->get_size() == 5);
                CHECK(f->get_blocks().size() == 1);
                CHECK(f->is_locally_available());
                CHECK(!f->is_locked());
                CHECK(peer_actor->blocks_requested == 1);

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() > 0);

                auto msg = &(*queue.back()).payload;
                auto &my_index_update = *std::get<proto::message::IndexUpdate>(*msg);
                REQUIRE(my_index_update.files_size() == 1);

                SECTION("dont redownload file only if metadata has changed") {
                    auto index_update = proto::IndexUpdate{};
                    index_update.set_folder(index.folder());
                    file->set_sequence(folder_1_peer->get_max_sequence() + 1);
                    counter->set_value(2ul);

                    *index_update.add_files() = *file;
                    peer_actor->forward(proto::message::IndexUpdate(new proto::IndexUpdate(index_update)));
                    sup->do_process();
                    CHECK(peer_actor->blocks_requested == 1);
                    CHECK(folder_my->get_max_sequence() == 2ul);
                    f = folder_my->get_file_infos().begin()->item;
                    CHECK(f->is_locally_available());
                    CHECK(f->get_sequence() == 2ul);
                }
            }

            SECTION("download 2 files") {
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                auto index = proto::Index{};
                index.set_folder(std::string(folder_1->get_id()));
                auto file_1 = index.add_files();
                file_1->set_name("file-1");
                file_1->set_type(proto::FileInfoType::FILE);
                file_1->set_sequence(folder_1_peer->get_max_sequence() + 1);
                file_1->set_block_size(5);
                file_1->set_size(5);
                auto version_1 = file_1->mutable_version();
                auto counter_1 = version_1->add_counters();
                counter_1->set_id(1ul);
                counter_1->set_value(1ul);

                auto file_2 = index.add_files();
                file_2->set_name("file-2");
                file_2->set_type(proto::FileInfoType::FILE);
                file_2->set_sequence(folder_1_peer->get_max_sequence() + 2);
                file_2->set_block_size(5);
                file_2->set_size(5);
                auto version_2 = file_2->mutable_version();
                auto counter_2 = version_2->add_counters();
                counter_2->set_id(1ul);
                counter_2->set_value(2ul);

                auto b1 = file_1->add_blocks();
                b1->set_hash(utils::sha256_digest("12345").value());
                b1->set_offset(0);
                b1->set_size(5);

                SECTION("with different blocks") {
                    auto b2 = file_2->add_blocks();
                    b2->set_hash(utils::sha256_digest("67890").value());
                    b2->set_offset(0);
                    b2->set_size(5);

                    auto folder_my = folder_infos.by_device(*my_device);
                    CHECK(folder_my->get_max_sequence() == 0ul);
                    CHECK(!folder_my->get_folder()->is_synchronizing());

                    peer_actor->forward(proto::message::Index(new proto::Index(index)));
                    peer_actor->push_block("12345", 0, file_1->name());
                    peer_actor->push_block("67890", 0, file_2->name());
                    sup->do_process();

                    CHECK(!folder_my->get_folder()->is_synchronizing());
                    CHECK(peer_actor->blocks_requested == 2);
                    REQUIRE(folder_my);
                    CHECK(folder_my->get_max_sequence() == 2ul);
                    REQUIRE(folder_my->get_file_infos().size() == 2);
                    {
                        auto f = folder_my->get_file_infos().by_name(file_1->name());
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->get_blocks().size() == 1);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                    {
                        auto f = folder_my->get_file_infos().by_name(file_2->name());
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->get_blocks().size() == 1);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                }

                SECTION("with the same block") {
                    *file_2->add_blocks() = *b1;

                    auto folder_my = folder_infos.by_device(*my_device);
                    CHECK(folder_my->get_max_sequence() == 0ul);
                    CHECK(!folder_my->get_folder()->is_synchronizing());

                    peer_actor->forward(proto::message::Index(new proto::Index(index)));
                    peer_actor->push_block("12345", 0, file_1->name());
                    sup->do_process();

                    CHECK(!folder_my->get_folder()->is_synchronizing());
                    CHECK(peer_actor->blocks_requested == 1);
                    REQUIRE(folder_my);
                    CHECK(folder_my->get_max_sequence() == 2ul);
                    REQUIRE(folder_my->get_file_infos().size() == 2);
                    {
                        auto f = folder_my->get_file_infos().by_name(file_1->name());
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->get_blocks().size() == 1);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                    {
                        auto f = folder_my->get_file_infos().by_name(file_2->name());
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->get_blocks().size() == 1);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                }

                SECTION("with the same blocks") {
                    auto concurrent_writes = GENERATE(1, 5);
                    cluster->modify_write_requests(concurrent_writes);
                    *file_2->add_blocks() = *b1;
                    *file_2->add_blocks() = *b1;
                    file_2->set_size(10);

                    auto folder_my = folder_infos.by_device(*my_device);
                    CHECK(folder_my->get_max_sequence() == 0ul);
                    CHECK(!folder_my->get_folder()->is_synchronizing());

                    peer_actor->forward(proto::message::Index(new proto::Index(index)));
                    peer_actor->push_block("12345", 0, file_1->name());
                    sup->do_process();

                    CHECK(!folder_my->get_folder()->is_synchronizing());
                    CHECK(peer_actor->blocks_requested == 1);
                    REQUIRE(folder_my);
                    CHECK(folder_my->get_max_sequence() == 2ul);
                    REQUIRE(folder_my->get_file_infos().size() == 2);
                    {
                        auto f = folder_my->get_file_infos().by_name(file_1->name());
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->get_blocks().size() == 1);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                    {
                        auto f = folder_my->get_file_infos().by_name(file_2->name());
                        REQUIRE(f);
                        CHECK(f->get_size() == 10);
                        CHECK(f->get_blocks().size() == 2);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                }
            }

            SECTION("don't attempt to download a file, which is deleted") {
                auto folder_peer = folder_infos.by_device(*peer_device);
                auto pr_fi = proto::FileInfo{};
                pr_fi.set_name("some-file");
                pr_fi.set_type(proto::FileInfoType::FILE);
                pr_fi.set_sequence(folder_1_peer->get_max_sequence() + 1);
                pr_fi.set_block_size(5);
                pr_fi.set_size(5);
                auto version = pr_fi.mutable_version();
                auto counter = version->add_counters();
                counter->set_id(1ul);
                counter->set_value(1ul);

                auto b1 = pr_fi.add_blocks();
                b1->set_hash(utils::sha256_digest("12345").value());
                b1->set_offset(0);
                b1->set_size(5);
                auto b = model::block_info_t::create(*b1).value();

                auto uuid = sup->sequencer->next_uuid();
                auto file_info = model::file_info_t::create(uuid, pr_fi, folder_peer).value();
                file_info->assign_block(b, 0);
                REQUIRE(folder_peer->add_strict(file_info));
                cluster->get_blocks().put(b);

                d_peer->set_max_sequence(folder_1_peer->get_max_sequence() + 1);
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                sup->do_process();
                auto blocks_requested = peer_actor->blocks_requested;

                auto index = proto::IndexUpdate{};
                index.set_folder(std::string(folder_1->get_id()));
                auto file = index.add_files();
                file->set_name("some-file");
                file->set_type(proto::FileInfoType::FILE);
                file->set_deleted(true);
                file->set_sequence(folder_1_peer->get_max_sequence() + 1);
                file->set_block_size(0);
                file->set_size(0);
                auto v = file->mutable_version();
                auto c = v->add_counters();
                c->set_id(peer_device->device_id().get_uint());
                c->set_value(1);

                peer_actor->forward(proto::message::IndexUpdate(new proto::IndexUpdate(index)));
                sup->do_process();

                CHECK(folder_my->get_max_sequence() == 1ul);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == pr_fi.name());
                CHECK(f->get_size() == 0);
                CHECK(f->get_blocks().size() == 0);
                CHECK(f->is_locally_available());
                CHECK(f->is_deleted());
                CHECK(!f->is_locked());
                CHECK(f->get_sequence() == 1ul);
                CHECK(peer_actor->blocks_requested == blocks_requested);
            }

            SECTION("new file via index_update => download it") {
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));

                auto index = proto::Index{};
                index.set_folder(std::string(folder_1->get_id()));
                peer_actor->forward(proto::message::Index(new proto::Index(index)));

                auto index_update = proto::IndexUpdate{};
                index_update.set_folder(std::string(folder_1->get_id()));
                auto file = index_update.add_files();
                file->set_name("some-file");
                file->set_type(proto::FileInfoType::FILE);
                file->set_sequence(folder_1_peer->get_max_sequence() + 1);
                file->set_block_size(5);
                file->set_size(5);
                auto version = file->mutable_version();
                auto counter = version->add_counters();
                counter->set_id(1);
                counter->set_value(peer_device->device_id().get_uint());

                auto b1 = file->add_blocks();
                b1->set_hash(utils::sha256_digest("12345").value());
                b1->set_offset(0);
                b1->set_size(5);

                peer_actor->forward(proto::message::IndexUpdate(new proto::IndexUpdate(index_update)));
                peer_actor->push_block("12345", 0);
                sup->do_process();

                auto folder_my = folder_infos.by_device(*my_device);
                CHECK(folder_my->get_max_sequence() == 1);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == file->name());
                CHECK(f->get_size() == 5);
                CHECK(f->get_blocks().size() == 1);
                CHECK(f->is_locally_available());
                CHECK(!f->is_locked());

                auto fp = folder_1_peer->get_file_infos().begin()->item;
                REQUIRE(fp);
                CHECK(!fp->is_locked());
            }

            SECTION("deleted file, has been restored => download it") {
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                sup->do_process();

                auto index = proto::Index{};
                index.set_folder(std::string(folder_1->get_id()));
                auto file_1 = index.add_files();
                file_1->set_name("some-file");
                file_1->set_type(proto::FileInfoType::FILE);
                file_1->set_sequence(folder_1_peer->get_max_sequence() + 1);
                file_1->set_deleted(true);
                auto v1 = file_1->mutable_version();
                auto c1 = v1->add_counters();
                c1->set_id(1u);
                c1->set_value(1u);

                peer_actor->forward(proto::message::Index(new proto::Index(index)));
                sup->do_process();
                CHECK(!folder_my->get_folder()->is_synchronizing());

                auto folder_my = folder_infos.by_device(*my_device);
                CHECK(folder_my->get_max_sequence() == 1);

                auto index_update = proto::IndexUpdate{};
                index_update.set_folder(std::string(folder_1->get_id()));
                auto file_2 = index_update.add_files();
                file_2->set_name("some-file");
                file_2->set_type(proto::FileInfoType::FILE);
                file_2->set_sequence(folder_1_peer->get_max_sequence() + 1);
                file_2->set_block_size(128 * 1024);
                file_2->set_size(5);
                auto v2 = file_2->mutable_version();
                auto c2 = v2->add_counters();
                c2->set_id(1u);
                c2->set_value(2u);

                auto b1 = file_2->add_blocks();
                b1->set_hash(utils::sha256_digest("12345").value());
                b1->set_offset(0);
                b1->set_size(5);

                peer_actor->forward(proto::message::IndexUpdate(new proto::IndexUpdate(index_update)));
                peer_actor->push_block("12345", 0);
                sup->do_process();

                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == file_1->name());
                CHECK(f->get_size() == 5);
                CHECK(f->get_blocks().size() == 1);
                CHECK(f->is_locally_available());
                CHECK(!f->is_locked());
                CHECK(!f->is_deleted());
            }

            SECTION("download a file, which has the same blocks locally") {
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                sup->do_process();

                auto index = proto::Index{};
                index.set_folder(std::string(folder_1->get_id()));
                auto file_1 = index.add_files();
                file_1->set_name("some-file");
                file_1->set_type(proto::FileInfoType::FILE);
                file_1->set_sequence(folder_1_peer->get_max_sequence() + 1);
                auto v1 = file_1->mutable_version();
                auto c1 = v1->add_counters();
                c1->set_id(1u);
                c1->set_value(1u);

                file_1->set_block_size(5);
                file_1->set_size(10);
                auto b1 = file_1->add_blocks();
                b1->set_hash(utils::sha256_digest("12345").value());
                b1->set_offset(0);
                b1->set_size(5);
                auto bi_1 = model::block_info_t::create(*b1).value();

                auto b2 = file_1->add_blocks();
                b2->set_hash(utils::sha256_digest("67890").value());
                b2->set_offset(5);
                b2->set_size(5);
                auto bi_2 = model::block_info_t::create(*b2).value();
                auto &blocks = cluster->get_blocks();
                blocks.put(bi_1);
                blocks.put(bi_2);

                auto pr_my = proto::FileInfo{};
                pr_my.set_name("some-file.source");
                pr_my.set_type(proto::FileInfoType::FILE);
                pr_my.set_sequence(2ul);
                pr_my.set_block_size(5);
                pr_my.set_size(5);
                pr_my.mutable_version()->add_counters()->set_id(my_device->device_id().get_uint());

                auto uuid = sup->sequencer->next_uuid();
                auto file_my = model::file_info_t::create(uuid, pr_my, folder_my).value();
                file_my->assign_block(bi_1, 0);
                file_my->mark_local_available(0);
                REQUIRE(folder_my->add_strict(file_my));

                peer_actor->forward(proto::message::Index(new proto::Index(index)));
                peer_actor->push_block("67890", 1);
                cluster->modify_write_requests(10);
                sup->do_process();

                REQUIRE(folder_my->get_file_infos().size() == 2);
                auto f = folder_my->get_file_infos().by_name(file_1->name());
                REQUIRE(f);
                CHECK(f->get_name() == file_1->name());
                CHECK(f->get_size() == 10);
                CHECK(f->get_blocks().size() == 2);
                CHECK(f->is_locally_available());
                CHECK(!f->is_locked());
            }
        }
    };
    F(true, 10).run();
}

void test_downloading_errors() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        std::uint32_t get_blocks_max_requested() override { return 1; }

        void main(diff_builder_t &) noexcept override {
            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_my = folder_infos.by_device(*my_device);

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            auto d_peer = folder->add_devices();
            d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
            d_peer->set_max_sequence(folder_1_peer->get_max_sequence() + 1);
            d_peer->set_index_id(folder_1_peer->get_index());
            auto d_my = folder->add_devices();
            d_my->set_id(std::string(my_device->device_id().get_sha256()));
            d_my->set_max_sequence(folder_my->get_max_sequence());
            d_my->set_index_id(folder_my->get_index());

            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));

            auto index = proto::Index{};
            index.set_folder(std::string(folder_1->get_id()));
            auto file = index.add_files();
            file->set_name("some-file");
            file->set_type(proto::FileInfoType::FILE);
            file->set_sequence(folder_1_peer->get_max_sequence() + 1);
            file->set_block_size(5);
            file->set_size(15);
            auto version = file->mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1ul);
            counter->set_value(1ul);

            auto b1 = file->add_blocks();
            b1->set_hash(utils::sha256_digest("12345").value());
            b1->set_offset(0);
            b1->set_size(5);

            auto b2 = file->add_blocks();
            b2->set_hash(utils::sha256_digest("67890").value());
            b2->set_offset(5);
            b2->set_size(5);

            auto b3 = file->add_blocks();
            b3->set_hash(utils::sha256_digest("11111").value());
            b3->set_offset(10);
            b3->set_size(5);

            CHECK(folder_my->get_max_sequence() == 0ul);
            peer_actor->forward(proto::message::Index(new proto::Index(index)));

            SECTION("general error, ok, do not shutdown") {
                auto ec = utils::make_error_code(utils::request_error_code_t::generic);
                peer_actor->push_block(ec, 0);
            }
            SECTION("hash mismatch, do not shutdown") {
                peer_actor->push_block("zzz", 0);
                peer_actor->push_block("67890", 1); // needed to terminate/shutdown controller
            }

            sup->do_process();

            CHECK(peer_actor->blocks_requested <= 2);
            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);

            auto folder_peer = folder_infos.by_device(*peer_device);
            REQUIRE(folder_peer->get_file_infos().size() == 1);
            auto f = folder_peer->get_file_infos().begin()->item;
            REQUIRE(f);
            CHECK(f->is_unreachable());
            CHECK(!f->is_synchronizing());
            CHECK(!f->is_locked());

            CHECK(!f->local_file());
            CHECK(!folder_my->get_folder()->is_synchronizing());

            sup->do_process();
        }
    };
    F(true, 10).run();
}

void test_download_from_scratch() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            sup->do_process();
            peer_actor->messages.clear();

            auto builder = diff_builder_t(*cluster);
            auto sha256 = peer_device->device_id().get_sha256();

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            {
                auto device = folder->add_devices();
                device->set_id(std::string(peer_device->device_id().get_sha256()));
                device->set_max_sequence(15);
                device->set_index_id(12345);
            }
            {
                auto device = folder->add_devices();
                device->set_id(std::string(my_device->device_id().get_sha256()));
                device->set_max_sequence(0);
                device->set_index_id(0);
            }

            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);

            auto index = proto::Index{};
            index.set_folder(std::string(folder_1->get_id()));
            auto file = index.add_files();
            file->set_name("some-file");
            file->set_type(proto::FileInfoType::FILE);
            file->set_sequence(154);
            file->set_block_size(5);
            file->set_size(5);
            auto version = file->mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1ul);
            counter->set_value(1ul);

            auto b1 = file->add_blocks();
            b1->set_hash(utils::sha256_digest("12345").value());
            b1->set_offset(0);
            b1->set_size(5);

            peer_actor->forward(proto::message::Index(new proto::Index(index)));
            peer_actor->push_block("12345", 0, file->name());
            sup->do_process();

            auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
            CHECK(folder_my->get_max_sequence() == 1ul);
            CHECK(!folder_my->get_folder()->is_synchronizing());

            auto f = folder_my->get_file_infos().by_name(file->name());
            REQUIRE(f);
            CHECK(f->get_size() == 5);
            CHECK(f->get_blocks().size() == 1);
            CHECK(f->is_locally_available());
            CHECK(!f->is_locked());

            REQUIRE(peer_actor->messages.size() == 3);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                REQUIRE(std::get_if<proto::message::ClusterConfig>(peer_msg));

                peer_actor->messages.pop_front();
                peer_msg = &peer_actor->messages.front()->payload;
                REQUIRE(std::get_if<proto::message::Index>(peer_msg));

                peer_actor->messages.pop_front();
                peer_msg = &peer_actor->messages.front()->payload;
                REQUIRE(std::get_if<proto::message::IndexUpdate>(peer_msg));
            }
        }
    };
    F(false, 10, false).run();
}

void test_download_resuming() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            sup->do_process();

            auto builder = diff_builder_t(*cluster);
            auto sha256 = peer_device->device_id().get_sha256();

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            auto d_peer = folder->add_devices();
            d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
            d_peer->set_max_sequence(15);
            d_peer->set_index_id(12345);

            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);
            auto folder_peer = folder_1->get_folder_infos().by_device(*peer_device);
            REQUIRE(folder_peer->get_index() == d_peer->index_id());

            auto index = proto::Index{};
            index.set_folder(std::string(folder_1->get_id()));
            auto file = index.add_files();
            file->set_name("some-file");
            file->set_type(proto::FileInfoType::FILE);
            file->set_sequence(154);
            file->set_block_size(5);
            file->set_size(10);
            auto version = file->mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1ul);
            counter->set_value(1ul);

            auto b1 = file->add_blocks();
            b1->set_hash(utils::sha256_digest("12345").value());
            b1->set_offset(0);
            b1->set_size(5);

            auto b2 = file->add_blocks();
            b2->set_hash(utils::sha256_digest("67890").value());
            b2->set_offset(5);
            b2->set_size(5);

            peer_actor->forward(proto::message::Index(new proto::Index(index)));
            peer_actor->push_block("12345", 0, file->name());
            sup->do_process();

            target->do_shutdown();
            sup->do_process();

            CHECK(!folder_1->is_synchronizing());
            for (auto &it : cluster->get_blocks()) {
                REQUIRE(!it.item->is_locked());
            }

            start_target();
            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
            peer_actor->push_block("67890", 1, file->name());
            sup->do_process();

            auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
            CHECK(folder_my->get_max_sequence() == 1ul);
            CHECK(!folder_my->get_folder()->is_synchronizing());

            auto f = folder_my->get_file_infos().by_name(file->name());
            REQUIRE(f);
            CHECK(f->get_size() == 10);
            CHECK(f->get_blocks().size() == 2);
            CHECK(f->is_locally_available());
            CHECK(!f->is_locked());
        }
    };
    F(false, 10, false).run();
}

void test_initiate_my_sharing() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            sup->do_process();

            auto cc = proto::ClusterConfig{};
            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));

            // nothing is shared
            sup->do_process();

            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            REQUIRE(peer_actor->messages.size() == 1);
            auto peer_msg = &peer_actor->messages.front()->payload;
            auto peer_cluster_msg = std::get_if<proto::message::ClusterConfig>(peer_msg);
            REQUIRE(peer_cluster_msg);
            REQUIRE(*peer_cluster_msg);
            REQUIRE((*peer_cluster_msg)->folders_size() == 0);

            // share folder_1
            peer_actor->messages.clear();
            auto sha256 = peer_device->device_id().get_sha256();
            diff_builder_t(*cluster).share_folder(sha256, folder_1->get_id()).apply(*sup);

            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(peer_actor->messages.size() == 1);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                auto peer_cluster_msg = std::get_if<proto::message::ClusterConfig>(peer_msg);
                REQUIRE((peer_cluster_msg && *peer_cluster_msg));
                auto &msg = *peer_cluster_msg;
                REQUIRE(msg->folders_size() == 1);
                auto f = msg->folders(0);
                REQUIRE(f.devices_size() == 2);

                using f_t = std::remove_reference_t<std::remove_cv_t<decltype(f.devices(0))>>;
                auto f_my = (f_t *){};
                auto f_peer = (f_t *){};
                for (int i = 0; i < f.devices_size(); ++i) {
                    auto &d = f.devices(i);
                    if (d.id() == my_device->device_id().get_sha256()) {
                        f_my = &d;
                    } else if (d.id() == peer_device->device_id().get_sha256()) {
                        f_peer = &d;
                    }
                }
                REQUIRE(f_peer);
                CHECK(!f_peer->index_id());
                CHECK(f_peer->max_sequence() == 0);

                REQUIRE(f_my);
                auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                CHECK(f_my->index_id() == folder_my->get_index());
                CHECK(f_my->max_sequence() == 0);
            }

            // unshare folder_1
            auto peer_fi = folder_1->get_folder_infos().by_device(*peer_device);
            peer_actor->messages.clear();
            diff_builder_t(*cluster).unshare_folder(*peer_fi).apply(*sup);
            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(peer_actor->messages.size() == 1);
            peer_msg = &peer_actor->messages.front()->payload;
            peer_cluster_msg = std::get_if<proto::message::ClusterConfig>(peer_msg);
            REQUIRE(peer_cluster_msg);
            REQUIRE(*peer_cluster_msg);
            REQUIRE((*peer_cluster_msg)->folders_size() == 0);
        }
    };
    F(false, 10, false).run();
}

void test_initiate_peer_sharing() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            sup->do_process();

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            {
                auto device = folder->add_devices();
                device->set_id(std::string(peer_device->device_id().get_sha256()));
                device->set_max_sequence(15);
                device->set_index_id(0x12345);
            }
            {
                auto device = folder->add_devices();
                device->set_id(std::string(my_device->device_id().get_sha256()));
                device->set_max_sequence(0);
                device->set_index_id(0x0);
            }

            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
            sup->do_process();

            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            REQUIRE(peer_actor->messages.size() == 1);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                auto peer_cluster_msg = std::get_if<proto::message::ClusterConfig>(peer_msg);
                REQUIRE(peer_cluster_msg);
                REQUIRE(*peer_cluster_msg);
                REQUIRE((*peer_cluster_msg)->folders_size() == 0);
            }

            // share folder_1
            peer_actor->messages.clear();
            auto sha256 = peer_device->device_id().get_sha256();
            diff_builder_t(*cluster).share_folder(sha256, folder_1->get_id()).apply(*sup);

            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            REQUIRE(peer_actor->messages.size() == 2);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                auto peer_cluster_msg = std::get_if<proto::message::ClusterConfig>(peer_msg);
                REQUIRE((peer_cluster_msg && *peer_cluster_msg));
                auto &msg = *peer_cluster_msg;
                REQUIRE(msg->folders_size() == 1);
                auto f = msg->folders(0);
                REQUIRE(f.devices_size() == 2);

                using f_t = std::remove_reference_t<std::remove_cv_t<decltype(f.devices(0))>>;
                auto f_my = (f_t *){};
                auto f_peer = (f_t *){};
                for (int i = 0; i < f.devices_size(); ++i) {
                    auto &d = f.devices(i);
                    if (d.id() == my_device->device_id().get_sha256()) {
                        f_my = &d;
                    } else if (d.id() == peer_device->device_id().get_sha256()) {
                        f_peer = &d;
                    }
                }
                REQUIRE(f_peer);
                CHECK(f_peer->index_id() == 0x12345);
                CHECK(f_peer->max_sequence() == 0);

                REQUIRE(f_my);
                auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                CHECK(f_my->index_id() == folder_my->get_index());
                CHECK(f_my->max_sequence() == 0);

                peer_actor->messages.pop_front();
                peer_msg = &peer_actor->messages.front()->payload;
                auto &index_msg = std::get<proto::message::Index>(*peer_msg);
                CHECK(index_msg->folder() == folder_1->get_id());
                CHECK(index_msg->files_size() == 0);
            }

            // unshare folder_1
            auto peer_fi = folder_1->get_folder_infos().by_device(*peer_device);
            peer_actor->messages.clear();
            diff_builder_t(*cluster).unshare_folder(*peer_fi).apply(*sup);
            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(peer_actor->messages.size() == 1);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                auto peer_cluster_msg = std::get_if<proto::message::ClusterConfig>(peer_msg);
                REQUIRE(peer_cluster_msg);
                REQUIRE(*peer_cluster_msg);
                REQUIRE((*peer_cluster_msg)->folders_size() == 0);
            }
        }
    };
    F(false, 10, false).run();
}

void test_sending_index_updates() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_my = folder_infos.by_device(*my_device);

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            auto d_peer = folder->add_devices();
            d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
            d_peer->set_max_sequence(folder_1_peer->get_max_sequence());
            d_peer->set_index_id(folder_1_peer->get_index());

            auto d_my = folder->add_devices();
            d_my->set_id(std::string(my_device->device_id().get_sha256()));
            d_my->set_max_sequence(folder_my->get_max_sequence());
            d_my->set_index_id(folder_my->get_index());

            auto index = proto::Index{};
            auto folder_id = std::string(folder_1->get_id());
            index.set_folder(folder_id);

            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
            peer_actor->forward(proto::message::Index(new proto::Index(index)));
            sup->do_process();

            auto builder = diff_builder_t(*cluster);
            auto pr_file = proto::FileInfo();
            pr_file.set_name("a.txt");

            peer_actor->messages.clear();
            builder.local_update(folder_id, pr_file).apply(*sup);
            REQUIRE(peer_actor->messages.size() == 1);
            auto &msg = peer_actor->messages.front();
            auto &index_update = *std::get<proto::message::IndexUpdate>(msg->payload);
            REQUIRE(index_update.files_size() == 1);
            CHECK(index_update.files(0).name() == "a.txt");
        }
    };
    F(true, 10).run();
}

void test_uploading() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_my = folder_infos.by_device(*my_device);

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            auto d_peer = folder->add_devices();
            d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
            d_peer->set_max_sequence(folder_1_peer->get_max_sequence());
            d_peer->set_index_id(folder_1_peer->get_index());
            auto d_my = folder->add_devices();
            d_my->set_id(std::string(my_device->device_id().get_sha256()));
            d_my->set_max_sequence(folder_my->get_max_sequence());
            d_my->set_index_id(folder_my->get_index());

            auto pr_fi = proto::FileInfo{};
            pr_fi.set_name("data.bin");
            pr_fi.set_type(proto::FileInfoType::FILE);
            pr_fi.set_sequence(folder_1_peer->get_max_sequence());
            pr_fi.set_block_size(5);
            pr_fi.set_size(5);
            auto version = pr_fi.mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(my_device->device_id().get_uint());
            auto b1 = pr_fi.add_blocks();
            b1->set_hash(utils::sha256_digest("12345").value());
            b1->set_offset(0);
            b1->set_size(5);
            auto b = model::block_info_t::create(*b1).value();

            auto uuid = sup->sequencer->next_uuid();
            pr_fi.set_sequence(folder_my->get_max_sequence() + 1);
            auto file_info = model::file_info_t::create(uuid, pr_fi, folder_my).value();
            file_info->assign_block(b, 0);
            REQUIRE(folder_my->add_strict(file_info));

            auto req = proto::Request();
            req.set_id(1);
            req.set_folder(std::string(folder_1->get_id()));
            req.set_name("data.bin");
            req.set_offset(0);
            req.set_size(5);

            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));

            SECTION("upload regular file, no hash") {
                peer_actor->forward(proto::message::Request(new proto::Request(req)));

                auto req_ptr = proto::message::Request(new proto::Request(req));
                auto res = r::make_message<fs::payload::block_response_t>(target->get_address(), std::move(req_ptr),
                                                                          sys::error_code{}, std::string("12345"));
                block_responses.push_back(res);

                sup->do_process();
                REQUIRE(block_requests.size() == 1);
                CHECK(block_requests[0]->payload.remote_request->id() == 1);
                CHECK(block_requests[0]->payload.remote_request->name() == "data.bin");

                REQUIRE(peer_actor->uploaded_blocks.size() == 1);
                auto &peer_res = *peer_actor->uploaded_blocks.front();
                CHECK(peer_res.id() == 1);
                CHECK(peer_res.code() == proto::ErrorCode::NO_BEP_ERROR);
                CHECK(peer_res.data() == "12345");
            }
        }
    };
    F(true, 10).run();
}

void test_peer_removal() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &builder) noexcept override {
            builder.remove_peer(*peer_device).apply(*sup);
            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::SHUT_DOWN);
            CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
            CHECK(target->get_shutdown_reason()->root()->ec == utils::error_code_t::peer_has_been_removed);
        }
    };
    F(true, 10).run();
}

void test_conflicts() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            sup->do_process();

            auto builder = diff_builder_t(*cluster);
            auto sha256 = peer_device->device_id().get_sha256();

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            auto d_peer = folder->add_devices();
            d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
            d_peer->set_max_sequence(15);
            d_peer->set_index_id(12345);

            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);
            auto folder_peer = folder_1->get_folder_infos().by_device(*peer_device);
            REQUIRE(folder_peer->get_index() == d_peer->index_id());

            auto index = proto::Index{};
            index.set_folder(std::string(folder_1->get_id()));
            auto file = index.add_files();
            file->set_name("some-file.txt");
            file->set_type(proto::FileInfoType::FILE);
            file->set_sequence(154);
            file->set_block_size(5);
            file->set_size(5);

            auto version = file->mutable_version();
            auto c1 = version->add_counters();
            c1->set_id(1ul);
            c1->set_value(1ul);

            auto b1 = file->add_blocks();
            b1->set_hash(utils::sha256_digest("12345").value());
            b1->set_offset(0);
            b1->set_size(5);

            peer_actor->forward(proto::message::Index(new proto::Index(index)));
            peer_actor->push_block("12345", 0, file->name());
            sup->do_process();

            auto &folder_infos = folder_1->get_folder_infos();
            auto local_folder = folder_infos.by_device(*my_device);
            auto local_file = local_folder->get_file_infos().by_name(file->name());
            auto pr_file = local_file->as_proto(false);
            auto bi_2 = pr_file.mutable_blocks()->Add();
            bi_2->set_hash(utils::sha256_digest("67890").value());
            bi_2->set_size(5);
            pr_file.set_modified_s(1734680000);
            builder.local_update(folder_1->get_id(), pr_file);
            builder.apply(*sup);

            file->clear_blocks();
            file->set_sequence(155);
            c1->set_id(peer_device->device_id().get_uint());

            auto b3 = file->add_blocks();
            b3->set_hash(utils::sha256_digest("12346").value());
            b3->set_offset(0);
            b3->set_size(5);

            auto index_update = proto::IndexUpdate{};
            index_update.set_folder(std::string(folder_1->get_id()));

            SECTION("local win") {
                file->set_modified_s(1734670000);
                c1->set_value(local_file->get_version()->get_best().value() - 1);
                auto local_seq = local_file->get_sequence();

                *index_update.add_files() = *file;
                peer_actor->messages.clear();
                peer_actor->forward(proto::message::IndexUpdate(new proto::IndexUpdate(index_update)));
                sup->do_process();

                REQUIRE(local_folder->get_file_infos().size() == 1);
                auto lf = local_folder->get_file_infos().by_sequence(local_seq);
                REQUIRE(local_seq == lf->get_sequence());
                CHECK(cluster->get_blocks().size() == 2);

                CHECK(peer_actor->messages.size() == 0);
            }
            SECTION("remote win") {
                file->set_modified_s(1734690000);
                c1->set_value(local_file->get_version()->get_best().value() + 1);

                *index_update.add_files() = *file;
                peer_actor->push_block("12346", 0, file->name());
                peer_actor->forward(proto::message::IndexUpdate(new proto::IndexUpdate(index_update)));
                sup->do_process();

                auto local_folder = folder_infos.by_device(*my_device);
                auto local_conflict = local_folder->get_file_infos().by_name(local_file->make_conflicting_name());
                REQUIRE(local_conflict);
                CHECK(local_conflict->get_size() == 5);
                REQUIRE(local_conflict->get_blocks().size() == 1);
                CHECK(local_conflict->get_blocks()[0]->get_hash() == bi_2->hash());

                auto file = local_folder->get_file_infos().by_name(local_file->get_name());
                REQUIRE(file);
                CHECK(file->get_size() == 5);
                REQUIRE(file->get_blocks().size() == 1);
                CHECK(file->get_blocks()[0]->get_hash() == b3->hash());

                CHECK(cluster->get_blocks().size() == 2);

                auto &msg = peer_actor->messages.back();
                auto &index_update_sent = *std::get<proto::message::IndexUpdate>(msg->payload);
                REQUIRE(index_update_sent.files_size() == 2);
                CHECK(index_update_sent.files(0).name() == local_conflict->get_name());
                CHECK(index_update_sent.files(1).name() == file->get_name());
            }
        }
    };
    F(false, 10, false).run();
}

void test_download_interrupting() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        void create_hasher() noexcept override {
            hasher = sup->create_actor<managed_hasher_t>()
                         .index(1)
                         .auto_reply(hasher_auto_reply)
                         .timeout(timeout)
                         .finish()
                         .get();
        }

        void main(diff_builder_t &) noexcept override {
            sup->do_process();

            auto builder = diff_builder_t(*cluster);
            auto sha256 = peer_device->device_id().get_sha256();

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            auto d_peer = folder->add_devices();
            d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
            d_peer->set_max_sequence(15);
            d_peer->set_index_id(12345);

            peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);
            auto folder_peer = folder_1->get_folder_infos().by_device(*peer_device);
            REQUIRE(folder_peer->get_index() == d_peer->index_id());

            auto index = proto::Index{};
            index.set_folder(std::string(folder_1->get_id()));
            auto file = index.add_files();
            file->set_name("some-file");
            file->set_type(proto::FileInfoType::FILE);
            file->set_sequence(154);
            file->set_block_size(5);
            file->set_size(10);
            auto version = file->mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1ul);
            counter->set_value(1ul);

            auto b1 = file->add_blocks();
            b1->set_hash(utils::sha256_digest("12345").value());
            b1->set_offset(0);
            b1->set_size(5);

            auto b2 = file->add_blocks();
            b2->set_hash(utils::sha256_digest("67890").value());
            b2->set_offset(5);
            b2->set_size(5);

            peer_actor->forward(proto::message::Index(new proto::Index(index)));
            sup->do_process();

            SECTION("block from peer") {
                SECTION("folder is kept") {
                    SECTION("suspend folder") { builder.suspend(*folder_1).apply(*sup); }
                    SECTION("unshare folder") { builder.unshare_folder(*folder_peer).apply(*sup); }
                    peer_actor->push_block("12345", 0, file->name());
                    peer_actor->push_block("67890", 1, file->name());
                    peer_actor->process_block_requests();
                    sup->do_process();
                    auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                    CHECK(folder_my->get_file_infos().size() == 0);
                }
                SECTION("remove folder") {
                    sup->auto_ack_blocks = false;

                    peer_actor->push_block("67890", 1, file->name());
                    peer_actor->process_block_requests();
                    sup->do_process();

                    builder.remove_folder(*folder_1).apply(*sup);
                    sup->do_process();

                    hasher->process_requests();
                    sup->do_process();

                    peer_actor->push_block("12345", 0, file->name());
                    peer_actor->process_block_requests();
                    sup->do_process();
                    CHECK(peer_actor->blocks_requested == file->blocks_size());
                    CHECK(!cluster->get_folders().by_id(folder->id()));
                }
            }
            SECTION("hash validation replies") {
                SECTION("folder is kept") {
                    peer_actor->push_block("12345", 0, file->name());
                    peer_actor->process_block_requests();
                    sup->do_process();

                    SECTION("suspend folder") { builder.suspend(*folder_1).apply(*sup); }
                    SECTION("unshare folder") { builder.unshare_folder(*folder_peer).apply(*sup); }

                    hasher->process_requests();
                    auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                    CHECK(folder_my->get_file_infos().size() == 0);
                }
                SECTION("remove folder") {
                    builder.remove_folder(*folder_1).apply(*sup);
                    peer_actor->push_block("12345", 0, file->name());
                    peer_actor->process_block_requests();
                    hasher->process_requests();
                    sup->do_process();
                    CHECK(!cluster->get_folders().by_id(folder->id()));
                }
            }

            SECTION("block acks from fs") {
                sup->auto_ack_blocks = false;
                hasher->auto_reply = true;
                peer_actor->push_block("67890", 1, file->name());
                peer_actor->push_block("12345", 0, file->name());
                peer_actor->process_block_requests();
                sup->do_process();
                auto diff = sup->delayed_ack_holder;
                REQUIRE(diff);

                SECTION("suspend") {
                    builder.suspend(*folder_1);
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff));
                    builder.apply(*sup);
                    auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                    CHECK(folder_my->get_file_infos().size() == 0);
                }

                SECTION("remove") {
                    builder.remove_folder(*folder_1).apply(*sup);
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff));
                    sup->do_process();
                    CHECK(!cluster->get_folders().by_id(folder->id()));
                }
            }
        }

        bool hasher_auto_reply = false;
        managed_hasher_t *hasher;
    };
    F(false, 10, false).run();
}

int _init() {
    REGISTER_TEST_CASE(test_startup, "test_startup", "[net]");
    REGISTER_TEST_CASE(test_overwhelm, "test_overwhelm", "[net]");
    REGISTER_TEST_CASE(test_index_receiving, "test_index_receiving", "[net]");
    REGISTER_TEST_CASE(test_index_sending, "test_index_sending", "[net]");
    REGISTER_TEST_CASE(test_downloading, "test_downloading", "[net]");
    REGISTER_TEST_CASE(test_downloading_errors, "test_downloading_errors", "[net]");
    REGISTER_TEST_CASE(test_download_from_scratch, "test_download_from_scratch", "[net]");
    REGISTER_TEST_CASE(test_download_resuming, "test_download_resuming", "[net]");
    REGISTER_TEST_CASE(test_initiate_my_sharing, "test_initiate_my_sharing", "[net]");
    REGISTER_TEST_CASE(test_initiate_peer_sharing, "test_initiate_peer_sharing", "[net]");
    REGISTER_TEST_CASE(test_sending_index_updates, "test_sending_index_updates", "[net]");
    REGISTER_TEST_CASE(test_uploading, "test_uploading", "[net]");
    REGISTER_TEST_CASE(test_peer_removal, "test_peer_removal", "[net]");
    REGISTER_TEST_CASE(test_conflicts, "test_conflicts", "[net]");
    REGISTER_TEST_CASE(test_download_interrupting, "test_download_interrupting", "[net]");
    return 1;
}

static int v = _init();

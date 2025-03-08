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
        utils::bytes_t data;
        sys::error_code ec;
    };
    using allowed_index_updates_t = std::unordered_set<std::string>;
    using block_responses_t = std::list<block_response_t>;
    using block_request_t = r::intrusive_ptr_t<net::message::block_request_t>;
    using block_requests_t = std::list<block_request_t>;
    using uploaded_blocks_t = std::list<proto::Response>;

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
        auto result = proto::parse_bep(data);
        auto orig = std::move(result.value().message);
        auto type = proto::MessageType::UNKNOWN;
        auto variant = net::payload::forwarded_message_t();
        std::visit(
            [&](auto &msg) {
                using T = std::decay_t<decltype(msg)>;
                using V = net::payload::forwarded_message_t;
                type = proto::message::get_bep_type<T>();
                if constexpr (std::is_constructible_v<V, T>) {
                    variant = std::move(msg);
                } else if constexpr (std::is_same_v<T, proto::Response>) {
                    uploaded_blocks.push_back(std::move(msg));
                }
            },
            orig);
        LOG_TRACE(log, "{}, on_transfer, bytes = {}, type = {}", identity, data.size(), (int)type);
        auto fwd_msg = new net::message::forwarded_message_t(address, std::move(variant));
        messages.emplace_back(fwd_msg);

        for (auto &msg : messages) {
            auto &p = msg->payload;
            if (auto m = std::get_if<proto::Index>(&p); m) {
                auto folder = proto::get_folder(*m);
                allowed_index_updates.emplace(std::move(folder));
            }
            if (auto m = std::get_if<proto::IndexUpdate>(&p); m) {
                auto folder = std::string(proto::get_folder(*m));
                if ((allowed_index_updates.count(folder) == 0) && !auto_share) {
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

    void push_block(utils::bytes_view_t data, size_t index, std::string_view name = {}) {
        if (index == next_block) {
            index = block_responses.size();
        }
        auto bytes = utils::bytes_t(data.begin(), data.end());
        block_responses.push_back(block_response_t{std::string(name), index, std::move(bytes), {}});
    }

    void push_block(sys::error_code ec, size_t index) {
        if (index == next_block) {
            index = block_responses.size();
        }
        block_responses.push_back(block_response_t{std::string{}, index, utils::bytes_t(), ec});
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

            unsigned char digest[SZ];
            auto &data = payload.data;

            utils::digest(data.data(), data.size(), digest);
            bool eq = payload.hash == utils::bytes_view_t(digest, SZ);
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

    virtual void _tune_peer(db::Device&) noexcept {}

    virtual void run() noexcept {
        auto peer_sha256_s = "VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ";
        auto peer_id = device_id_t::from_string(peer_sha256_s).value();
        auto peer_db = db::Device();
        db::set_name(peer_db, "peer-device");
        _tune_peer(peer_db);
        peer_device = device_t::create(peer_id.get_key(), peer_db).value();

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
            REQUIRE(std::get_if<proto::ClusterConfig>(&msg));
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
            REQUIRE(std::get_if<proto::ClusterConfig>(&msg));

            peer_actor->messages.pop_front();
            CHECK(peer_actor->messages.empty());

            auto cc = proto::ClusterConfig{};
            peer_actor->forward(std::move(cc));
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
            REQUIRE(std::get_if<proto::ClusterConfig>(msg));

            peer_actor->messages.pop_front();
            CHECK(peer_actor->messages.empty());

            auto cc = proto::ClusterConfig{};
            peer_actor->forward(cc);
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
            REQUIRE(std::get_if<proto::ClusterConfig>(msg));
            peer_actor->messages.pop_front();

            peer_actor->forward(std::move(cc));
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
                peer_actor->forward(cc);
                proto::set_folder(index, "non-existing-folder");
                peer_actor->forward(index);
                sup->do_process();

                CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::SHUT_DOWN);
                CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
            }

            SECTION("index is applied") {
                auto &folder = proto::add_folders(cc);
                proto::set_id(folder, folder_1->get_id());
                auto &d_peer = proto::add_devices(folder);
                proto::set_id(d_peer, peer_device->device_id().get_sha256());
                REQUIRE(cluster->get_pending_folders().size() == 0);
                proto::set_max_sequence(d_peer, 10);
                proto::set_index_id(d_peer, folder_1_peer->get_index());
                peer_actor->forward(cc);

                proto::set_folder(index, folder_1->get_id());

                auto file_name = std::string_view("some-dir");
                auto &file = proto::add_files(index);
                proto::set_name(file, file_name);
                proto::set_type(file, proto::FileInfoType::DIRECTORY);
                proto::set_sequence(file, 10);

                auto &v = proto::get_version(file);
                proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 1));

                peer_actor->forward(index);
                sup->do_process();

                CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
                CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

                auto &folder_infos = folder_1->get_folder_infos();

                auto folder_peer = folder_infos.by_device(*peer_device);
                REQUIRE(folder_peer);
                CHECK(folder_peer->get_max_sequence() == 10ul);
                REQUIRE(folder_peer->get_file_infos().size() == 1);
                CHECK(folder_peer->get_file_infos().begin()->item->get_name() == file_name);

                auto folder_my = folder_infos.by_device(*my_device);
                REQUIRE(folder_my);
                CHECK(folder_my->get_max_sequence() == 1ul);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                CHECK(folder_my->get_file_infos().begin()->item->get_name() == file_name);

                SECTION("then index update is applied") {
                    auto index_update = proto::IndexUpdate{};
                    proto::set_folder(index_update, folder_1->get_id());
                    auto file_name = std::string_view("some-dir-2");
                    auto sequence = folder_1_peer->get_max_sequence() + 1;
                    auto &file = proto::add_files(index_update);
                    proto::set_name(file, file_name);
                    proto::set_type(file, proto::FileInfoType::DIRECTORY);
                    proto::set_sequence(file, sequence);

                    auto &v = proto::get_version(file);
                    proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 1));

                    peer_actor->forward(index_update);

                    sup->do_process();
                    CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
                    CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() ==
                          r::state_t::OPERATIONAL);

                    CHECK(folder_peer->get_max_sequence() == sequence);
                    REQUIRE(folder_peer->get_file_infos().size() == 2);
                    CHECK(folder_peer->get_file_infos().by_name(file_name));

                    CHECK(folder_my->get_max_sequence() == 2ul);
                    REQUIRE(folder_my->get_file_infos().size() == 2);
                    CHECK(folder_my->get_file_infos().by_name(file_name));
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
            auto file_name = std::string_view("link");
            proto::set_name(pr_file_info, file_name);
            proto::set_type(pr_file_info, proto::FileInfoType::SYMLINK);
            proto::set_symlink_target(pr_file_info, "/some/where");

            auto builder = diff_builder_t(*cluster);
            builder.local_update(folder_1->get_id(), pr_file_info);
            builder.apply(*sup);

            auto folder_1_my = folder_1->get_folder_infos().by_device(*my_device);

            auto cc = proto::ClusterConfig{};
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            auto &d_peer = proto::add_devices(folder);
            proto::set_id(d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(d_peer, folder_1_peer->get_max_sequence());
            proto::set_index_id(d_peer, folder_1_peer->get_index());

            SECTION("peer has outdated by sequence view") {
                auto &d_my = proto::add_devices(folder);
                proto::set_id(d_my, my_device->device_id().get_sha256());
                proto::set_max_sequence(d_my, folder_1_my->get_max_sequence() - 1);
                proto::set_index_id(d_my, folder_1_my->get_index());

                peer_actor->forward(cc);
                sup->do_process();

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() == 1);
                auto msg = &(*queue.front()).payload;
                auto &my_index_update = std::get<proto::IndexUpdate>(*msg);
                REQUIRE(proto::get_files_size(my_index_update) == 1);
            }

            SECTION("peer has outdated by index view") {
                auto &d_my = proto::add_devices(folder);
                proto::set_id(d_my, my_device->device_id().get_sha256());
                proto::set_max_sequence(d_my, folder_1_my->get_max_sequence());
                proto::set_index_id(d_my, folder_1_my->get_index() + 5);

                peer_actor->forward(cc);
                sup->do_process();

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() == 2);
                auto msg = &(*queue.front()).payload;
                auto &my_index = std::get<proto::Index>(*msg);
                REQUIRE(proto::get_files_size(my_index) == 0);
                queue.pop_front();

                msg = &(*queue.front()).payload;
                auto &my_index_update = std::get<proto::IndexUpdate>(*msg);
                REQUIRE(proto::get_files_size(my_index_update) == 1);
            }

            SECTION("peer has actual view") {
                auto &d_my = proto::add_devices(folder);
                proto::set_id(d_my, my_device->device_id().get_sha256());
                proto::set_max_sequence(d_my, folder_1_my->get_max_sequence());
                proto::set_index_id(d_my, folder_1_my->get_index());

                peer_actor->forward(cc);
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
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            auto d_peer = &proto::add_devices(folder);
            proto::set_id(*d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(*d_peer, 10);
            proto::set_index_id(*d_peer, folder_1_peer->get_index());
            auto &d_my = proto::add_devices(folder);
            proto::set_id(d_my, my_device->device_id().get_sha256());
            proto::set_max_sequence(d_my, folder_my->get_max_sequence());
            proto::set_index_id(d_my, folder_my->get_index());

            d_peer = &proto::get_devices(folder, 0);
            SECTION("cluster config & index has a new file => download it") {
                peer_actor->forward(cc);
                auto index = proto::Index{};
                proto::set_folder(index, folder_1->get_id());
                auto file_name = std::string_view("some-file");
                auto &file = proto::add_files(index);
                proto::set_name(file, file_name);
                proto::set_type(file, proto::FileInfoType::FILE);
                proto::set_sequence(file, folder_1_peer->get_max_sequence() + 1);
                proto::set_size(file, 5);
                proto::set_block_size(file, 5);

                auto &v = proto::get_version(file);
                auto &counter = proto::add_counters(v);
                proto::set_id(counter, 1);
                proto::set_value(counter, 1);

                auto data_1 = as_owned_bytes("12345");
                auto data_1_h = utils::sha256_digest(data_1).value();
                auto &b1 = proto::add_blocks(file);
                proto::set_hash(b1, data_1_h);
                proto::set_size(b1, data_1.size());

                auto folder_my = folder_infos.by_device(*my_device);
                CHECK(folder_my->get_max_sequence() == 0ul);
                CHECK(!folder_my->get_folder()->is_synchronizing());

                peer_actor->forward(index);
                sup->do_process();
                CHECK(folder_my->get_folder()->is_synchronizing());

                peer_actor->push_block(data_1, 0);
                peer_actor->process_block_requests();
                sup->do_process();

                CHECK(!folder_my->get_folder()->is_synchronizing());
                REQUIRE(folder_my);
                CHECK(folder_my->get_max_sequence() == 1ul);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == file_name);
                CHECK(f->get_size() == 5);
                CHECK(f->get_blocks().size() == 1);
                CHECK(f->is_locally_available());
                CHECK(!f->is_locked());
                CHECK(peer_actor->blocks_requested == 1);

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() > 0);

                auto msg = &(*queue.back()).payload;
                auto &my_index_update = std::get<proto::IndexUpdate>(*msg);
                REQUIRE(proto::get_files_size(my_index_update) == 1);

                SECTION("dont redownload file only if metadata has changed") {
                    auto index_update = proto::IndexUpdate{};
                    proto::set_folder(index_update, proto::get_folder(index));
                    proto::set_sequence(file, folder_1_peer->get_max_sequence() + 1);
                    proto::set_value(counter, 2);
                    proto::add_files(index_update, file);

                    peer_actor->forward(index_update);
                    sup->do_process();
                    CHECK(peer_actor->blocks_requested == 1);
                    CHECK(folder_my->get_max_sequence() == 2ul);
                    f = folder_my->get_file_infos().begin()->item;
                    CHECK(f->is_locally_available());
                    CHECK(f->get_sequence() == 2ul);
                }
            }

            SECTION("download 2 files") {
                peer_actor->forward(cc);
                auto index = proto::Index{};
                proto::set_folder(index, folder_1->get_id());
                auto file_name_1 = std::string_view("file-1");
                auto file_1 = &proto::add_files(index);
                proto::set_name(*file_1, file_name_1);
                proto::set_type(*file_1, proto::FileInfoType::FILE);
                proto::set_sequence(*file_1, folder_1_peer->get_max_sequence() + 1);
                proto::set_size(*file_1, 5);
                proto::set_block_size(*file_1, 5);

                auto &v_1 = proto::get_version(*file_1);
                auto &c_1 = proto::add_counters(v_1);
                proto::set_id(c_1, 1);
                proto::set_value(c_1, 1);

                auto data_1 = as_owned_bytes("12345");
                auto data_1_hash = utils::sha256_digest(data_1).value();

                auto data_2 = as_owned_bytes("67890");
                auto data_2_hash = utils::sha256_digest(data_2).value();

                auto &b1 = proto::add_blocks(*file_1);
                proto::set_hash(b1, data_1_hash);
                proto::set_size(b1, 5);

                auto file_name_2 = std::string_view("file-2");
                auto file_2 = &proto::add_files(index);
                proto::set_name(*file_2, file_name_2);
                proto::set_type(*file_2, proto::FileInfoType::FILE);
                proto::set_sequence(*file_2, folder_1_peer->get_max_sequence() + 2);
                proto::set_size(*file_2, 5);
                proto::set_block_size(*file_2, 5);

                auto &v_2 = proto::get_version(*file_2);
                auto &c_2 = proto::add_counters(v_2);
                proto::set_id(c_2, 1);
                proto::set_value(c_2, 2);

                file_1 = &proto::get_files(index, 0);
                SECTION("with different blocks") {
                    auto &b2 = proto::add_blocks(*file_2);
                    proto::set_hash(b2, data_2_hash);
                    proto::set_size(b2, 5);

                    auto folder_my = folder_infos.by_device(*my_device);
                    CHECK(folder_my->get_max_sequence() == 0ul);
                    CHECK(!folder_my->get_folder()->is_synchronizing());

                    peer_actor->forward(index);
                    peer_actor->push_block(data_1, 0, file_name_1);
                    peer_actor->push_block(data_2, 0, file_name_2);
                    sup->do_process();

                    CHECK(!folder_my->get_folder()->is_synchronizing());
                    CHECK(peer_actor->blocks_requested == 2);
                    REQUIRE(folder_my);
                    CHECK(folder_my->get_max_sequence() == 2ul);
                    REQUIRE(folder_my->get_file_infos().size() == 2);
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_1);
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->get_blocks().size() == 1);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_2);
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->get_blocks().size() == 1);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                }

                SECTION("with the same block") {
                    proto::add_blocks(*file_2, b1);

                    auto folder_my = folder_infos.by_device(*my_device);
                    CHECK(folder_my->get_max_sequence() == 0ul);
                    CHECK(!folder_my->get_folder()->is_synchronizing());

                    peer_actor->forward(index);
                    peer_actor->push_block(data_1, 0, file_name_1);
                    sup->do_process();

                    CHECK(!folder_my->get_folder()->is_synchronizing());
                    CHECK(peer_actor->blocks_requested == 1);
                    REQUIRE(folder_my);
                    CHECK(folder_my->get_max_sequence() == 2ul);
                    REQUIRE(folder_my->get_file_infos().size() == 2);
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_1);
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->get_blocks().size() == 1);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_2);
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
                    proto::add_blocks(*file_2, b1);
                    proto::add_blocks(*file_2, b1);
                    proto::set_size(*file_2, 10);

                    auto folder_my = folder_infos.by_device(*my_device);
                    CHECK(folder_my->get_max_sequence() == 0ul);
                    CHECK(!folder_my->get_folder()->is_synchronizing());

                    peer_actor->forward(index);
                    peer_actor->push_block(data_1, 0, file_name_1);
                    sup->do_process();

                    CHECK(!folder_my->get_folder()->is_synchronizing());
                    CHECK(peer_actor->blocks_requested == 1);
                    REQUIRE(folder_my);
                    CHECK(folder_my->get_max_sequence() == 2ul);
                    REQUIRE(folder_my->get_file_infos().size() == 2);
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_1);
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->get_blocks().size() == 1);
                        CHECK(f->is_locally_available());
                        CHECK(!f->is_locked());
                    }
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_2);
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
                auto file_name = std::string_view("some-file");
                proto::set_name(pr_fi, file_name);
                proto::set_type(pr_fi, proto::FileInfoType::FILE);
                proto::set_sequence(pr_fi, folder_1_peer->get_max_sequence() + 1);
                proto::set_size(pr_fi, 5);
                proto::set_block_size(pr_fi, 5);

                auto &version = proto::get_version(pr_fi);
                proto::add_counters(version, proto::Counter(1, 1));

                auto data_1 = as_owned_bytes("12345");
                auto data_1_hash = utils::sha256_digest(data_1).value();

                auto &b1 = proto::add_blocks(pr_fi);
                proto::set_hash(b1, data_1_hash);
                proto::set_size(b1, 5);
                auto b = model::block_info_t::create(b1).value();

                auto uuid = sup->sequencer->next_uuid();
                auto file_info = model::file_info_t::create(uuid, pr_fi, folder_peer).value();
                file_info->assign_block(b, 0);
                REQUIRE(folder_peer->add_strict(file_info));
                cluster->get_blocks().put(b);

                proto::set_max_sequence(*d_peer, folder_1_peer->get_max_sequence() + 1);
                peer_actor->forward(cc);
                sup->do_process();
                auto blocks_requested = peer_actor->blocks_requested;

                auto index_update = proto::IndexUpdate{};
                proto::set_folder(index_update, folder_1->get_id());

                auto &file = proto::add_files(index_update);
                proto::set_name(file, file_name);
                proto::set_type(file, proto::FileInfoType::FILE);
                proto::set_sequence(file, folder_1_peer->get_max_sequence() + 1);
                proto::set_deleted(file, true);

                auto &v = proto::get_version(file);
                proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 1));

                peer_actor->forward(index_update);
                sup->do_process();

                CHECK(folder_my->get_max_sequence() == 1ul);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == file_name);
                CHECK(f->get_size() == 0);
                CHECK(f->get_blocks().size() == 0);
                CHECK(f->is_locally_available());
                CHECK(f->is_deleted());
                CHECK(!f->is_locked());
                CHECK(f->get_sequence() == 1ul);
                CHECK(peer_actor->blocks_requested == blocks_requested);
            }

            SECTION("new file via index_update => download it") {
                peer_actor->forward(cc);

                auto index = proto::Index{};
                proto::set_folder(index, folder_1->get_id());
                peer_actor->forward(index);

                auto index_update = proto::IndexUpdate{};
                proto::set_folder(index_update, folder_1->get_id());

                auto file_name = std::string_view("some-file");
                auto &file = proto::add_files(index_update);
                proto::set_name(file, file_name);
                proto::set_type(file, proto::FileInfoType::FILE);
                proto::set_sequence(file, folder_1_peer->get_max_sequence() + 1);
                proto::set_block_size(file, 5);
                proto::set_size(file, 5);

                auto &v = proto::get_version(file);
                proto::add_counters(v, proto::Counter(1, 1));

                auto data_1 = as_owned_bytes("12345");
                auto data_1_h = utils::sha256_digest(data_1).value();
                auto &b1 = proto::add_blocks(file);
                proto::set_hash(b1, data_1_h);
                proto::set_size(b1, data_1.size());

                peer_actor->forward(index_update);
                peer_actor->push_block(data_1, 0);
                sup->do_process();

                auto folder_my = folder_infos.by_device(*my_device);
                CHECK(folder_my->get_max_sequence() == 1);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == file_name);
                CHECK(f->get_size() == 5);
                CHECK(f->get_blocks().size() == 1);
                CHECK(f->is_locally_available());
                CHECK(!f->is_locked());

                auto fp = folder_1_peer->get_file_infos().begin()->item;
                REQUIRE(fp);
                CHECK(!fp->is_locked());
            }

            SECTION("deleted file, has been restored => download it") {
                peer_actor->forward(cc);
                sup->do_process();

                auto index = proto::Index{};
                proto::set_folder(index, folder_1->get_id());

                auto file_name = std::string_view("file-1");
                auto &file_1 = proto::add_files(index);
                proto::set_name(file_1, file_name);
                proto::set_type(file_1, proto::FileInfoType::FILE);
                proto::set_sequence(file_1, folder_1_peer->get_max_sequence() + 1);
                proto::set_deleted(file_1, true);

                auto &v_1 = proto::get_version(file_1);
                proto::add_counters(v_1, proto::Counter(1, 1));
                peer_actor->forward(index);

                sup->do_process();
                CHECK(!folder_my->get_folder()->is_synchronizing());

                auto folder_my = folder_infos.by_device(*my_device);
                CHECK(folder_my->get_max_sequence() == 1);

                auto index_update = proto::IndexUpdate{};
                proto::set_folder(index_update, folder_1->get_id());
                auto &file_2 = proto::add_files(index_update);
                proto::set_name(file_2, file_name);
                proto::set_type(file_2, proto::FileInfoType::FILE);
                proto::set_sequence(file_2, folder_1_peer->get_max_sequence() + 1);
                proto::set_block_size(file_2, 5);
                proto::set_size(file_2, 5);

                auto &v_2 = proto::get_version(file_2);
                proto::add_counters(v_2, proto::Counter(1, 2));

                auto data_1 = as_owned_bytes("12345");
                auto data_1_hash = utils::sha256_digest(data_1).value();

                auto &b1 = proto::add_blocks(file_2);
                proto::set_hash(b1, data_1_hash);
                proto::set_size(b1, 5);

                peer_actor->forward(index_update);
                peer_actor->push_block(data_1, 0);
                sup->do_process();

                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == file_name);
                CHECK(f->get_size() == 5);
                CHECK(f->get_blocks().size() == 1);
                CHECK(f->is_locally_available());
                CHECK(!f->is_locked());
                CHECK(!f->is_deleted());
            }

            SECTION("download a file, which has the same blocks locally") {
                peer_actor->forward(cc);
                sup->do_process();

                auto index = proto::Index{};
                proto::set_folder(index, folder_1->get_id());

                auto file_name_1 = std::string_view("file-1");
                auto &file_1 = proto::add_files(index);
                proto::set_name(file_1, file_name_1);
                proto::set_type(file_1, proto::FileInfoType::FILE);
                proto::set_sequence(file_1, folder_1_peer->get_max_sequence() + 1);
                proto::set_block_size(file_1, 5);
                proto::set_size(file_1, 10);

                auto &v_1 = proto::get_version(file_1);
                proto::add_counters(v_1, proto::Counter(1, 1));

                auto data_1 = as_owned_bytes("12345");
                auto data_1_hash = utils::sha256_digest(data_1).value();

                auto &b1 = proto::add_blocks(file_1);
                proto::set_hash(b1, data_1_hash);
                proto::set_size(b1, 5);
                auto bi_1 = model::block_info_t::create(b1).value();

                auto data_2 = as_owned_bytes("67890");
                auto data_2_hash = utils::sha256_digest(data_2).value();

                auto &b2 = proto::add_blocks(file_1);
                proto::set_hash(b2, data_2_hash);
                proto::set_size(b2, 5);
                proto::set_offset(b2, 5);
                auto bi_2 = model::block_info_t::create(b2).value();

                auto &blocks = cluster->get_blocks();
                blocks.put(bi_1);
                blocks.put(bi_2);

                auto file_name_my = std::string_view("file-1.source");
                auto pr_file_my = proto::FileInfo();
                proto::set_name(pr_file_my, file_name_my);
                proto::set_type(pr_file_my, proto::FileInfoType::FILE);
                proto::set_sequence(pr_file_my, 5);
                proto::set_block_size(pr_file_my, 5);
                proto::set_size(pr_file_my, 5);

                auto &v_my = proto::get_version(pr_file_my);
                proto::add_counters(v_my, proto::Counter(my_device->device_id().get_uint(), 1));

                auto uuid = sup->sequencer->next_uuid();
                auto file_my = model::file_info_t::create(uuid, pr_file_my, folder_my).value();
                file_my->assign_block(bi_1, 0);
                file_my->mark_local_available(0);
                REQUIRE(folder_my->add_strict(file_my));

                peer_actor->forward(index);
                peer_actor->push_block(data_2, 1);
                cluster->modify_write_requests(10);
                sup->do_process();

                REQUIRE(folder_my->get_file_infos().size() == 2);
                auto f = folder_my->get_file_infos().by_name(file_name_1);
                REQUIRE(f);
                CHECK(f->get_name() == file_name_1);
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
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            auto &d_peer = proto::add_devices(folder);
            proto::set_id(d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(d_peer, folder_1_peer->get_max_sequence());
            proto::set_index_id(d_peer, folder_1_peer->get_index());
            auto &d_my = proto::add_devices(folder);
            proto::set_id(d_my, my_device->device_id().get_sha256());
            proto::set_max_sequence(d_my, folder_my->get_max_sequence());
            proto::set_index_id(d_my, folder_my->get_index());

            peer_actor->forward(cc);

            auto index = proto::Index{};
            proto::set_folder(index, folder_1->get_id());
            auto file_name = std::string_view("some-file");
            auto &file = proto::add_files(index);
            proto::set_name(file, file_name);
            proto::set_type(file, proto::FileInfoType::FILE);
            proto::set_sequence(file, folder_1_peer->get_max_sequence() + 1);
            proto::set_block_size(file, 5);
            proto::set_size(file, 15);

            auto &v = proto::get_version(file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);

            auto data_1 = as_owned_bytes("12345");
            auto data_1_hash = utils::sha256_digest(data_1).value();

            auto data_2 = as_owned_bytes("67890");
            auto data_2_hash = utils::sha256_digest(data_2).value();

            auto data_3 = as_owned_bytes("11111");
            auto data_3_hash = utils::sha256_digest(data_3).value();

            auto &b1 = proto::add_blocks(file);
            proto::set_hash(b1, data_1_hash);
            proto::set_size(b1, 5);

            auto &b2 = proto::add_blocks(file);
            proto::set_hash(b2, data_2_hash);
            proto::set_size(b2, 5);
            proto::set_offset(b2, 5);

            auto &b3 = proto::add_blocks(file);
            proto::set_hash(b3, data_2_hash);
            proto::set_size(b3, 5);
            proto::set_offset(b3, 10);

            CHECK(folder_my->get_max_sequence() == 0ul);
            peer_actor->forward(index);

            SECTION("general error, ok, do not shutdown") {
                auto ec = utils::make_error_code(utils::request_error_code_t::generic);
                peer_actor->push_block(ec, 0);
            }
            SECTION("hash mismatch, do not shutdown") {
                peer_actor->push_block(as_owned_bytes("zzz"), 0);
                peer_actor->push_block(data_2, 1); // needed to terminate/shutdown controller
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
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            {
                auto &device = proto::add_devices(folder);
                proto::set_id(device, peer_device->device_id().get_sha256());
                proto::set_max_sequence(device, 15);
                proto::set_index_id(device, 12345);
            }
            {
                auto &device = proto::add_devices(folder);
                proto::set_id(device, my_device->device_id().get_sha256());
                proto::set_max_sequence(device, 0);
                proto::set_index_id(device, 0);
            }

            peer_actor->forward(cc);
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);

            auto index = proto::Index{};
            proto::set_folder(index, folder_1->get_id());
            auto file_name = std::string_view("some-file");
            auto &file = proto::add_files(index);
            proto::set_name(file, file_name);
            proto::set_type(file, proto::FileInfoType::FILE);
            proto::set_sequence(file, 154);
            proto::set_size(file, 5);
            proto::set_block_size(file, 5);

            auto &v = proto::get_version(file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);
            proto::set_value(counter, 1);

            auto data_1 = as_owned_bytes("12345");
            auto data_1_h = utils::sha256_digest(data_1).value();
            auto &b1 = proto::add_blocks(file);
            proto::set_hash(b1, data_1_h);
            proto::set_size(b1, data_1.size());

            peer_actor->forward(index);
            peer_actor->push_block(data_1, 0, file_name);
            sup->do_process();

            auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
            CHECK(folder_my->get_max_sequence() == 1ul);
            CHECK(!folder_my->get_folder()->is_synchronizing());

            auto f = folder_my->get_file_infos().by_name(file_name);
            REQUIRE(f);
            CHECK(f->get_size() == 5);
            CHECK(f->get_blocks().size() == 1);
            CHECK(f->is_locally_available());
            CHECK(!f->is_locked());

            REQUIRE(peer_actor->messages.size() == 3);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                REQUIRE(std::get_if<proto::ClusterConfig>(peer_msg));

                peer_actor->messages.pop_front();
                peer_msg = &peer_actor->messages.front()->payload;
                REQUIRE(std::get_if<proto::Index>(peer_msg));

                peer_actor->messages.pop_front();
                peer_msg = &peer_actor->messages.front()->payload;
                REQUIRE(std::get_if<proto::IndexUpdate>(peer_msg));
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
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            auto &d_peer = proto::add_devices(folder);
            proto::set_id(d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(d_peer, 15);
            proto::set_index_id(d_peer, 12345);

            peer_actor->forward(cc);
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);
            auto folder_peer = folder_1->get_folder_infos().by_device(*peer_device);
            REQUIRE(folder_peer->get_index() == proto::get_index_id(d_peer));

            auto index = proto::Index{};
            proto::set_folder(index, folder_1->get_id());

            auto file_name = std::string_view("some-file");
            auto &file = proto::add_files(index);
            proto::set_name(file, file_name);
            proto::set_type(file, proto::FileInfoType::FILE);
            proto::set_sequence(file, 154);
            proto::set_block_size(file, 5);
            proto::set_size(file, 10);

            auto &v = proto::get_version(file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);

            auto data_1 = as_owned_bytes("12345");
            auto data_1_hash = utils::sha256_digest(data_1).value();

            auto data_2 = as_owned_bytes("67890");
            auto data_2_hash = utils::sha256_digest(data_2).value();

            auto &b1 = proto::add_blocks(file);
            proto::set_hash(b1, data_1_hash);
            proto::set_size(b1, 5);

            auto &b2 = proto::add_blocks(file);
            proto::set_hash(b2, data_2_hash);
            proto::set_size(b2, 5);
            proto::set_offset(b2, 5);

            peer_actor->forward(index);
            peer_actor->push_block(data_1, 0, file_name);
            sup->do_process();

            target->do_shutdown();
            sup->do_process();

            CHECK(!folder_1->is_synchronizing());
            for (auto &it : cluster->get_blocks()) {
                REQUIRE(!it.item->is_locked());
            }

            start_target();
            peer_actor->forward(cc);
            peer_actor->push_block(data_2, 1, file_name);
            sup->do_process();

            auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
            CHECK(folder_my->get_max_sequence() == 1ul);
            CHECK(!folder_my->get_folder()->is_synchronizing());

            auto f = folder_my->get_file_infos().by_name(file_name);
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
            peer_actor->forward(cc);

            // nothing is shared
            sup->do_process();

            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            REQUIRE(peer_actor->messages.size() == 1);
            auto peer_msg = &peer_actor->messages.front()->payload;
            auto peer_cluster_msg = std::get_if<proto::ClusterConfig>(peer_msg);
            REQUIRE(peer_cluster_msg);
            REQUIRE(proto::get_folders_size(*peer_cluster_msg) == 0);

            // share folder_1
            peer_actor->messages.clear();
            auto sha256 = peer_device->device_id().get_sha256();
            diff_builder_t(*cluster).share_folder(sha256, folder_1->get_id()).apply(*sup);

            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(peer_actor->messages.size() == 1);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                auto peer_cluster_msg = std::get_if<proto::ClusterConfig>(peer_msg);
                REQUIRE((peer_cluster_msg && peer_cluster_msg));
                auto &msg = *peer_cluster_msg;
                REQUIRE(proto::get_folders_size(msg) == 1);
                auto &f = proto::get_folders(msg, 0);
                REQUIRE(proto::get_devices_size(f) == 2);

                using f_t = const proto::Device;
                auto f_my = (f_t *){};
                auto f_peer = (f_t *){};
                for (int i = 0; i < proto::get_devices_size(f); ++i) {
                    auto &d = proto::get_devices(f, i);
                    auto id = proto::get_id(d);
                    if (id == my_device->device_id().get_sha256()) {
                        f_my = &d;
                    } else if (id == peer_device->device_id().get_sha256()) {
                        f_peer = &d;
                    }
                }
                REQUIRE(f_peer);
                CHECK(!proto::get_index_id(*f_peer));
                CHECK(proto::get_max_sequence(*f_peer) == 0);

                REQUIRE(f_my);
                auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                CHECK(proto::get_index_id(*f_my) == folder_my->get_index());
                CHECK(proto::get_max_sequence(*f_my) == 0);
            }

            // unshare folder_1
            auto peer_fi = folder_1->get_folder_infos().by_device(*peer_device);
            peer_actor->messages.clear();
            diff_builder_t(*cluster).unshare_folder(*peer_fi).apply(*sup);
            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(peer_actor->messages.size() == 1);
            peer_msg = &peer_actor->messages.front()->payload;
            peer_cluster_msg = std::get_if<proto::ClusterConfig>(peer_msg);
            REQUIRE(peer_cluster_msg);
            REQUIRE(proto::get_folders_size(*peer_cluster_msg) == 0);
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
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            {
                auto &device = proto::add_devices(folder);
                proto::set_id(device, peer_device->device_id().get_sha256());
                proto::set_max_sequence(device, 15);
                proto::set_index_id(device, 0x12345);
            }
            {
                auto &device = proto::add_devices(folder);
                proto::set_id(device, my_device->device_id().get_sha256());
                proto::set_max_sequence(device, 0);
                proto::set_index_id(device, 0);
            }

            peer_actor->forward(cc);
            sup->do_process();

            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            REQUIRE(peer_actor->messages.size() == 1);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                auto peer_cluster_msg = std::get_if<proto::ClusterConfig>(peer_msg);
                REQUIRE(peer_cluster_msg);
                REQUIRE(proto::get_folders_size(*peer_cluster_msg) == 0);
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
                auto peer_cluster_msg = std::get_if<proto::ClusterConfig>(peer_msg);
                REQUIRE((peer_cluster_msg && peer_cluster_msg));

                auto &msg = *peer_cluster_msg;
                REQUIRE(proto::get_folders_size(msg) == 1);
                auto &f = proto::get_folders(msg, 0);
                REQUIRE(proto::get_devices_size(f) == 2);

                using f_t = const proto::Device;
                auto f_my = (f_t *){};
                auto f_peer = (f_t *){};
                for (int i = 0; i < proto::get_devices_size(f); ++i) {
                    auto &d = proto::get_devices(f, i);
                    auto id = proto::get_id(d);
                    if (id == my_device->device_id().get_sha256()) {
                        f_my = &d;
                    } else if (id == peer_device->device_id().get_sha256()) {
                        f_peer = &d;
                    }
                }
                REQUIRE(f_peer);
                CHECK(proto::get_index_id(*f_peer) == 0x12345);
                CHECK(proto::get_max_sequence(*f_peer) == 0);

                REQUIRE(f_my);
                auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                CHECK(proto::get_index_id(*f_my) == folder_my->get_index());
                CHECK(proto::get_max_sequence(*f_my) == 0);

                peer_actor->messages.pop_front();
                peer_msg = &peer_actor->messages.front()->payload;
                auto &index_msg = std::get<proto::Index>(*peer_msg);
                CHECK(proto::get_folder(index_msg) == folder_1->get_id());
                CHECK(proto::get_files_size(index_msg) == 0);
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
                auto peer_cluster_msg = std::get_if<proto::ClusterConfig>(peer_msg);
                REQUIRE(peer_cluster_msg);
                REQUIRE(proto::get_folders_size(*peer_cluster_msg) == 0);
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
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            auto &d_peer = proto::add_devices(folder);
            proto::set_id(d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(d_peer, folder_1_peer->get_max_sequence());
            proto::set_index_id(d_peer, folder_1_peer->get_index());
            auto &d_my = proto::add_devices(folder);
            proto::set_id(d_my, my_device->device_id().get_sha256());
            proto::set_max_sequence(d_my, folder_my->get_max_sequence());
            proto::set_index_id(d_my, folder_my->get_index());

            auto index = proto::IndexUpdate{};
            proto::set_folder(index, folder_1->get_id());

            peer_actor->forward(cc);
            peer_actor->forward(index);
            sup->do_process();

            auto builder = diff_builder_t(*cluster);
            auto pr_file = proto::FileInfo();
            proto::set_name(pr_file, "a.txt");

            peer_actor->messages.clear();
            builder.local_update(folder_1->get_id(), pr_file).apply(*sup);
            REQUIRE(peer_actor->messages.size() == 1);
            auto &msg = peer_actor->messages.front();
            auto &index_update = std::get<proto::IndexUpdate>(msg->payload);
            REQUIRE(proto::get_files_size(index_update) == 1);
            CHECK(proto::get_name(proto::get_files(index_update, 0)) == "a.txt");
        }
    };
    F(true, 10).run();
}

void test_uploading() {
    struct F : fixture_t {

        void _tune_peer(db::Device& device) noexcept override {
            db::set_compression(device, proto::Compression::ALWAYS);
        }

        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_my = folder_infos.by_device(*my_device);

            auto cc = proto::ClusterConfig{};
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            auto &d_peer = proto::add_devices(folder);
            proto::set_id(d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(d_peer, folder_1_peer->get_max_sequence());
            proto::set_index_id(d_peer, folder_1_peer->get_index());
            auto &d_my = proto::add_devices(folder);
            proto::set_id(d_my, my_device->device_id().get_sha256());
            proto::set_max_sequence(d_my, folder_my->get_max_sequence());
            proto::set_index_id(d_my, folder_my->get_index());

            auto data_sample = "/my-folder-1/my-folder-2/my-folder-3/my-folder-4/";
            auto data = fmt::format("{0}{0}{0}{0}{0}", data_sample);
            auto data_begin = (const unsigned char*)data.data();
            auto data_end = (const unsigned char*)data.data() + data.size();

            auto file_name = std::string_view("data.bin");
            auto file = proto::FileInfo();
            proto::set_name(file, file_name);
            proto::set_type(file, proto::FileInfoType::FILE);
            proto::set_sequence(file, folder_my->get_max_sequence() + 1);
            proto::set_size(file, data.size());
            proto::set_block_size(file, data.size());

            auto &v = proto::get_version(file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);
            proto::set_value(counter, 1);

            auto data_1 = utils::bytes_t(data_begin, data_end);
            auto data_1_h = utils::sha256_digest(data_1).value();
            auto &b1 = proto::add_blocks(file);
            proto::set_hash(b1, data_1_h);
            proto::set_size(b1, data_1.size());

            auto b = model::block_info_t::create(b1).value();

            auto uuid = sup->sequencer->next_uuid();
            auto file_info = model::file_info_t::create(uuid, file, folder_my).value();
            file_info->assign_block(b, 0);
            REQUIRE(folder_my->add_strict(file_info));

            auto req = proto::Request();
            proto::set_id(req, 1);
            proto::set_folder(req, folder_1->get_id());
            proto::set_name(req, file_name);
            proto::set_size(req, data.size());

            peer_actor->forward(cc);

            SECTION("upload regular file, no hash") {
                peer_actor->forward(req);

                auto res = r::make_message<fs::payload::block_response_t>(target->get_address(), req, sys::error_code{},
                                                                          data_1);
                block_responses.push_back(res);

                sup->do_process();
                REQUIRE(block_requests.size() == 1);
                CHECK(proto::get_id(block_requests[0]->payload.remote_request) == 1);
                CHECK(proto::get_name(block_requests[0]->payload.remote_request) == file_name);

                REQUIRE(peer_actor->uploaded_blocks.size() == 1);
                auto &peer_res = peer_actor->uploaded_blocks.front();
                CHECK(proto::get_id(peer_res) == 1);
                CHECK(proto::get_code(peer_res) == proto::ErrorCode::NO_BEP_ERROR);
                CHECK(proto::get_data(peer_res) == data_1);
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

            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            auto &d_peer = proto::add_devices(folder);
            proto::set_id(d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(d_peer, 15);
            proto::set_index_id(d_peer, 12345);

            peer_actor->forward(cc);
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);
            auto folder_peer = folder_1->get_folder_infos().by_device(*peer_device);
            REQUIRE(folder_peer->get_index() == proto::get_index_id(d_peer));

            auto index = proto::Index{};
            proto::set_folder(index, folder_1->get_id());

            auto file_name = std::string_view("some-file.txt");
            auto &file = proto::add_files(index);
            proto::set_name(file, file_name);
            proto::set_type(file, proto::FileInfoType::FILE);
            proto::set_sequence(file, 154);
            proto::set_block_size(file, 5);
            proto::set_size(file, 5);

            auto &v = proto::get_version(file);
            auto &c_1 = proto::add_counters(v);
            proto::set_id(c_1, 1);
            proto::set_value(c_1, 1);

            auto data_1 = as_owned_bytes("12345");
            auto data_1_h = utils::sha256_digest(data_1).value();
            auto &b1 = proto::add_blocks(file);
            proto::set_hash(b1, data_1_h);
            proto::set_size(b1, data_1.size());

            peer_actor->forward(index);
            peer_actor->push_block(data_1, 0, file_name);
            sup->do_process();

            auto &folder_infos = folder_1->get_folder_infos();
            auto local_folder = folder_infos.by_device(*my_device);
            auto local_file = local_folder->get_file_infos().by_name(file_name);
            auto pr_file = local_file->as_proto(false);
            proto::set_modified_s(pr_file, 1734680000);

            auto data_2 = as_owned_bytes("67890");
            auto data_2_h = utils::sha256_digest(data_2).value();
            auto &b2 = proto::add_blocks(pr_file);
            proto::set_hash(b2, data_2_h);
            proto::set_size(b2, data_2.size());

            builder.local_update(folder_1->get_id(), pr_file);
            builder.apply(*sup);

            proto::clear_blocks(file);
            proto::set_sequence(file, 155);
            proto::set_id(c_1, peer_device->device_id().get_uint());

            auto data_3 = as_owned_bytes("12346");
            auto data_3_h = utils::sha256_digest(data_3).value();
            auto &b3 = proto::add_blocks(pr_file);
            proto::set_hash(b3, data_3_h);
            proto::set_size(b3, data_3.size());
            proto::add_blocks(file, b3);

            auto index_update = proto::IndexUpdate{};
            proto::set_folder(index_update, folder_1->get_id());

            SECTION("local win") {
                proto::set_modified_s(file, 1734670000);
                proto::set_value(c_1, proto::get_value(local_file->get_version()->get_best()) - 1);
                auto local_seq = local_file->get_sequence();

                proto::add_files(index_update, file);
                peer_actor->messages.clear();
                peer_actor->forward(index_update);
                sup->do_process();

                REQUIRE(local_folder->get_file_infos().size() == 1);
                auto lf = local_folder->get_file_infos().by_sequence(local_seq);
                REQUIRE(local_seq == lf->get_sequence());
                CHECK(cluster->get_blocks().size() == 2);

                CHECK(peer_actor->messages.size() == 0);
            }
            SECTION("remote win") {
                proto::set_modified_s(file, 1734690000);
                proto::set_value(c_1, proto::get_value(local_file->get_version()->get_best()) + 1);
                proto::add_files(index_update, file);
                peer_actor->push_block(data_3, 0, file_name);
                peer_actor->forward(index_update);
                sup->do_process();

                auto local_folder = folder_infos.by_device(*my_device);
                auto local_conflict = local_folder->get_file_infos().by_name(local_file->make_conflicting_name());
                REQUIRE(local_conflict);
                CHECK(local_conflict->get_size() == 5);
                REQUIRE(local_conflict->get_blocks().size() == 1);
                CHECK(local_conflict->get_blocks()[0]->get_hash() == data_2_h);

                auto file = local_folder->get_file_infos().by_name(local_file->get_name());
                REQUIRE(file);
                CHECK(file->get_size() == 5);
                REQUIRE(file->get_blocks().size() == 1);
                CHECK(file->get_blocks()[0]->get_hash() == data_3_h);

                CHECK(cluster->get_blocks().size() == 2);

                auto &msg = peer_actor->messages.back();
                auto &index_update_sent = std::get<proto::IndexUpdate>(msg->payload);
                REQUIRE(proto::get_files_size(index_update_sent) == 2);
                auto &f1 = proto::get_files(index_update_sent, 0);
                auto &f2 = proto::get_files(index_update_sent, 1);
                CHECK(proto::get_name(f1) == local_conflict->get_name());
                CHECK(proto::get_name(f2) == file->get_name());
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
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            auto &d_peer = proto::add_devices(folder);
            proto::set_id(d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(d_peer, 15);
            proto::set_index_id(d_peer, 12345);

            peer_actor->forward(cc);
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);
            auto folder_peer = folder_1->get_folder_infos().by_device(*peer_device);
            REQUIRE(folder_peer->get_index() == proto::get_index_id(d_peer));

            auto index = proto::Index{};
            proto::set_folder(index, folder_1->get_id());
            auto file_name = std::string_view("some-file");
            auto &file = proto::add_files(index);
            proto::set_name(file, file_name);
            proto::set_type(file, proto::FileInfoType::FILE);
            proto::set_sequence(file, 154);
            proto::set_block_size(file, 5);
            proto::set_size(file, 10);

            auto &v = proto::get_version(file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);

            auto data_1 = as_owned_bytes("12345");
            auto data_1_h = utils::sha256_digest(data_1).value();
            auto &b1 = proto::add_blocks(file);
            proto::set_hash(b1, data_1_h);
            proto::set_size(b1, data_1.size());

            auto data_2 = as_owned_bytes("67890");
            auto data_2_h = utils::sha256_digest(data_2).value();
            auto &b2 = proto::add_blocks(file);
            proto::set_hash(b2, data_2_h);
            proto::set_size(b2, data_2.size());

            peer_actor->forward(index);
            sup->do_process();

            SECTION("block from peer") {
                SECTION("folder is kept") {
                    SECTION("suspend folder") { builder.suspend(*folder_1).apply(*sup); }
                    SECTION("unshare folder") { builder.unshare_folder(*folder_peer).apply(*sup); }
                    peer_actor->push_block(data_1, 0, file_name);
                    peer_actor->push_block(data_2, 1, file_name);
                    peer_actor->process_block_requests();
                    sup->do_process();
                    auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                    CHECK(folder_my->get_file_infos().size() == 0);
                }
                SECTION("remove folder") {
                    sup->auto_ack_blocks = false;

                    peer_actor->push_block(data_2, 1, file_name);
                    peer_actor->process_block_requests();
                    sup->do_process();

                    builder.remove_folder(*folder_1).apply(*sup);
                    sup->do_process();

                    hasher->process_requests();
                    sup->do_process();

                    peer_actor->push_block(data_1, 0, file_name);
                    peer_actor->process_block_requests();
                    sup->do_process();
                    CHECK(peer_actor->blocks_requested == proto::get_blocks_size(file));
                    CHECK(!cluster->get_folders().by_id(proto::get_id(folder)));
                }
            }
            SECTION("hash validation replies") {
                SECTION("folder is kept") {
                    peer_actor->push_block(data_1, 0, file_name);
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
                    peer_actor->push_block(data_1, 0, file_name);
                    peer_actor->process_block_requests();
                    hasher->process_requests();
                    sup->do_process();
                    CHECK(!cluster->get_folders().by_id(proto::get_id(folder)));
                }
            }

            SECTION("block acks from fs") {
                sup->auto_ack_blocks = false;
                hasher->auto_reply = true;
                peer_actor->push_block(data_2, 1, file_name);
                peer_actor->push_block(data_1, 0, file_name);
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
                    CHECK(!cluster->get_folders().by_id(proto::get_id(folder)));
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

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"

#include "model/cluster.h"
#include "diff-builder.h"
#include "hasher/hasher_proxy_actor.h"
#include "hasher/hasher_actor.h"
#include "net/controller_actor.h"
#include "net/names.h"
#include "utils/error_code.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::hasher;

namespace {

struct sample_peer_config_t : public r::actor_config_t {
    model::device_id_t peer_device_id;
};

template <typename Actor> struct sample_peer_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&peer_device_id(const model::device_id_t &value) &&noexcept {
        parent_t::config.peer_device_id = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct sample_peer_t : r::actor_base_t {
    using config_t = sample_peer_config_t;
    template <typename Actor> using config_builder_t = sample_peer_config_builder_t<Actor>;

    using remote_message_t = r::intrusive_ptr_t<net::message::forwarded_message_t>;
    using remote_messages_t = std::list<remote_message_t>;

    struct block_response_t {
        size_t block_index;
        std::string data;
    };
    using block_responses_t = std::list<block_response_t>;
    using block_request_t = r::intrusive_ptr_t<net::message::block_request_t>;
    using block_requests_t = std::list<block_request_t>;

    sample_peer_t(config_t &config) : r::actor_base_t{config}, peer_device{config.peer_device_id} {
        log = utils::get_logger("test.sample_peer");
    }

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("sample_peer", false); });
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&sample_peer_t::on_start_reading);
            p.subscribe_actor(&sample_peer_t::on_termination);
            p.subscribe_actor(&sample_peer_t::on_forward);
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

    void on_forward(net::message::forwarded_message_t &message) noexcept {
        LOG_TRACE(log, "{}, on_forward", identity);
        messages.emplace_back(&message);
    }

    void on_block_request(net::message::block_request_t &req) noexcept {
        block_requests.push_front(&req);
        ++blocks_requested;
        log->debug("{}, requesting block # {}", identity,
                   block_requests.front()->payload.request_payload.block.block_index());
        if (block_responses.size()) {
            log->debug("{}, top response block # {}", identity, block_responses.front().block_index);
        }
        auto condition = [&]() -> bool {
            return block_requests.size() && block_responses.size() &&
                   block_requests.front()->payload.request_payload.block.block_index() ==
                       block_responses.front().block_index;
        };
        while (condition()) {
            log->debug("{}, matched, replying...", identity);
            reply_to(*block_requests.front(), block_responses.front().data);
            block_responses.pop_front();
            block_requests.pop_front();
        }
    }

    void forward(net::payload::forwarded_message_t payload) noexcept {
        send<net::payload::forwarded_message_t>(controller, std::move(payload));
    }

    static const constexpr size_t next_block = 1000000;

    void push_block(std::string_view data, size_t index) {
        if (index == next_block) {
            index = block_responses.size();
        }
        block_responses.push_back(block_response_t{index, std::string(data)});
    }

    size_t blocks_requested = 0;
    bool reading = false;
    remote_messages_t messages;
    r::address_ptr_t controller;
    model::device_id_t peer_device;
    utils::logger_t log;
    block_requests_t block_requests;
    block_responses_t block_responses;
};

struct fixture_t {
    using peer_ptr_t = r::intrusive_ptr_t<sample_peer_t>;
    using target_ptr_t = r::intrusive_ptr_t<net::controller_actor_t>;

    fixture_t(bool auto_start_, int64_t max_sequence_) noexcept : auto_start{auto_start_}, max_sequence{max_sequence_} {
        utils::set_default("trace");
    }

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
        builder.create_folder(folder_id_1, "")
            .create_folder(folder_id_2, "")
            .configure_cluster(sha256)
            .add(sha256, folder_id_1, 123, max_sequence)
            .finish()
            .share_folder(peer_id.get_sha256(), folder_id_1);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        sup->start();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);
        sup->create_actor<hasher_actor_t>().index(1).timeout(timeout).finish();
        sup->create_actor<hasher::hasher_proxy_actor_t>()
            .timeout(timeout)
            .hasher_threads(1)
            .name(net::names::hasher_proxy)
            .finish();

        peer_actor = sup->create_actor<sample_peer_t>().timeout(timeout).finish();

        builder.apply(*sup);

        auto &folders = cluster->get_folders();
        folder_1 = folders.by_id(folder_id_1);
        folder_2 = folders.by_id(folder_id_2);

        folder_1_peer = folder_1->get_folder_infos().by_device_id(peer_id.get_sha256());

        target = sup->create_actor<controller_actor_t>()
                     .peer(peer_device)
                     .peer_addr(peer_actor->get_address())
                     .request_pool(1024)
                     .cluster(cluster)
                     .timeout(timeout)
                     .request_timeout(timeout)
                     .finish();

        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
        target_addr = target->get_address();

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

    virtual void main(diff_builder_t &) noexcept {}

    bool auto_start;
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
};

} // namespace

void test_startup() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            REQUIRE(peer_actor->reading);
            REQUIRE(peer_actor->messages.size() == 1);
            auto &msg = (*peer_actor->messages.front()).payload;
            CHECK(std::get_if<proto::message::ClusterConfig>(&msg));

            auto cc = proto::ClusterConfig{};
            auto payload = proto::message::ClusterConfig(new proto::ClusterConfig(cc));
            peer_actor->forward(std::move(payload));
            sup->do_process();

            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
        }
    };
    F(false, 10).run();
}

void test_index() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &builder) noexcept override {

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
                REQUIRE(cluster->get_unknown_folders().empty());
                d_peer->set_max_sequence(folder_1_peer->get_max_sequence());
                d_peer->set_index_id(folder_1_peer->get_index());
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));

                index.set_folder(std::string(folder_1->get_id()));
                auto file = index.add_files();
                file->set_name("some-dir");
                file->set_type(proto::FileInfoType::DIRECTORY);
                file->set_sequence(folder_1_peer->get_max_sequence());
                peer_actor->forward(proto::message::Index(new proto::Index(index)));
                sup->do_process();

                CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
                CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

                auto &folder_infos = folder_1->get_folder_infos();

                auto folder_peer = folder_infos.by_device(peer_device);
                REQUIRE(folder_peer);
                CHECK(folder_peer->get_max_sequence() == 10ul);
                REQUIRE(folder_peer->get_file_infos().size() == 1);
                CHECK(folder_peer->get_file_infos().begin()->item->get_name() == file->name());

                auto folder_my = folder_infos.by_device(my_device);
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

void test_downloading() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &builder) noexcept override {
            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_my = folder_infos.by_device(my_device);

            auto cc = proto::ClusterConfig{};
            auto folder = cc.add_folders();
            folder->set_id(std::string(folder_1->get_id()));
            auto d_peer = folder->add_devices();
            d_peer->set_id(std::string(peer_device->device_id().get_sha256()));
            d_peer->set_max_sequence(folder_1_peer->get_max_sequence());
            d_peer->set_index_id(folder_1_peer->get_index());

            SECTION("cluster config & index has a new file => download it") {
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));

                auto index = proto::Index{};
                index.set_folder(std::string(folder_1->get_id()));
                auto file = index.add_files();
                file->set_name("some-file");
                file->set_type(proto::FileInfoType::FILE);
                file->set_sequence(folder_1_peer->get_max_sequence());
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

                auto folder_my = folder_infos.by_device(my_device);
                CHECK(folder_my->get_max_sequence() == 0ul);

                peer_actor->forward(proto::message::Index(new proto::Index(index)));
                peer_actor->push_block("12345", 0);
                sup->do_process();

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
            SECTION("cluster config is the same, but there are non-downloaded files") {
                auto folder_peer = folder_infos.by_device(peer_device);

                auto pr_fi = proto::FileInfo{};
                pr_fi.set_name("some-file");
                pr_fi.set_type(proto::FileInfoType::FILE);
                pr_fi.set_sequence(folder_1_peer->get_max_sequence());
                pr_fi.set_block_size(5);
                pr_fi.set_size(5);
                auto version = pr_fi.mutable_version();
                auto counter = version->add_counters();
                counter->set_id(1);
                counter->set_value(peer_device->as_uint());
                auto b1 = pr_fi.add_blocks();
                b1->set_hash(utils::sha256_digest("12345").value());
                b1->set_offset(0);
                b1->set_size(5);
                auto b = model::block_info_t::create(*b1).value();

                auto file_info = model::file_info_t::create(cluster->next_uuid(), pr_fi, folder_peer).value();
                file_info->assign_block(b, 0);
                folder_peer->add(file_info, true);

                d_peer->set_max_sequence(folder_peer->get_max_sequence());
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));

                peer_actor->push_block("12345", 0);
                sup->do_process();

                CHECK(folder_my->get_max_sequence() == 1ul);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == pr_fi.name());
                CHECK(f->get_size() == 5);
                CHECK(f->get_blocks().size() == 1);
                CHECK(f->is_locally_available());
                CHECK(!f->is_locked());
            }

            SECTION("don't attempt to download a file, which is deleted") {
                auto folder_peer = folder_infos.by_device(peer_device);
                auto pr_fi = proto::FileInfo{};
                pr_fi.set_name("some-file");
                pr_fi.set_type(proto::FileInfoType::FILE);
                pr_fi.set_sequence(folder_1_peer->get_max_sequence());
                pr_fi.set_block_size(5);
                pr_fi.set_size(5);
                auto b1 = pr_fi.add_blocks();
                b1->set_hash(utils::sha256_digest("12345").value());
                b1->set_offset(0);
                b1->set_size(5);
                auto b = model::block_info_t::create(*b1).value();

                auto file_info = model::file_info_t::create(cluster->next_uuid(), pr_fi, folder_peer).value();
                file_info->assign_block(b, 0);
                folder_peer->add(file_info, true);

                d_peer->set_max_sequence(folder_1_peer->get_max_sequence() + 1);
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                sup->do_process();

                auto index = proto::IndexUpdate{};
                index.set_folder(std::string(folder_1->get_id()));
                auto file = index.add_files();
                file->set_name("some-file");
                file->set_type(proto::FileInfoType::FILE);
                file->set_deleted(true);
                file->set_sequence(folder_1_peer->get_max_sequence() + 1);
                file->set_block_size(0);
                file->set_size(0);

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
                CHECK(peer_actor->blocks_requested == 0);
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
                counter->set_value(peer_device->as_uint());

                auto b1 = file->add_blocks();
                b1->set_hash(utils::sha256_digest("12345").value());
                b1->set_offset(0);
                b1->set_size(5);

                peer_actor->forward(proto::message::IndexUpdate(new proto::IndexUpdate(index_update)));
                peer_actor->push_block("12345", 0);
                sup->do_process();

                auto folder_my = folder_infos.by_device(my_device);
                CHECK(folder_my->get_max_sequence() == 1);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = folder_my->get_file_infos().begin()->item;
                REQUIRE(f);
                CHECK(f->get_name() == file->name());
                CHECK(f->get_size() == 5);
                CHECK(f->get_blocks().size() == 1);
                CHECK(f->is_locally_available());
                CHECK(!f->is_locked());
            }

            SECTION("deleted file, has been restored => download it") {
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                sup->do_process();

                auto index = proto::Index{};
                index.set_folder(std::string(folder_1->get_id()));
                auto file_1 = index.add_files();
                file_1->set_name("some-file");
                file_1->set_type(proto::FileInfoType::FILE);
                file_1->set_sequence(folder_1_peer->get_max_sequence());
                file_1->set_deleted(true);
                auto v1 = file_1->mutable_version();
                auto c1 = v1->add_counters();
                c1->set_id(1u);
                c1->set_value(1u);

                peer_actor->forward(proto::message::Index(new proto::Index(index)));
                sup->do_process();

                auto folder_my = folder_infos.by_device(my_device);
                CHECK(folder_my->get_max_sequence() == 1ul);

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
            }

            SECTION("download a file, which has the same blocks locally") {
                peer_actor->forward(proto::message::ClusterConfig(new proto::ClusterConfig(cc)));
                sup->do_process();

                auto index = proto::Index{};
                index.set_folder(std::string(folder_1->get_id()));
                auto file_1 = index.add_files();
                file_1->set_name("some-file");
                file_1->set_type(proto::FileInfoType::FILE);
                file_1->set_sequence(folder_1_peer->get_max_sequence());
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

                auto file_my = model::file_info_t::create(cluster->next_uuid(), pr_my, folder_my).value();
                file_my->assign_block(bi_1, 0);
                file_my->mark_local_available(0);
                folder_my->add(file_my, true);

                peer_actor->forward(proto::message::Index(new proto::Index(index)));
                peer_actor->push_block("67890", 1);
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

REGISTER_TEST_CASE(test_startup, "test_startup", "[net]");
REGISTER_TEST_CASE(test_index, "test_index", "[net]");
REGISTER_TEST_CASE(test_downloading, "test_downloading", "[net]");

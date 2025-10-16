// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"
#include "test_peer.h"
#include "managed_hasher.h"

#include "model/cluster.h"
#include "diff-builder.h"
#include "hasher/hasher_proxy_actor.h"
#include "net/controller_actor.h"
#include "net/names.h"
#include "fs/messages.h"
#include "utils/error_code.h"
#include "utils/tls.h"
#include <type_traits>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::hasher;

namespace {

struct mock_supervisor_t : supervisor_t {
    using block_responces_t = std::list<outcome::result<utils::bytes_t>>;
    using block_requests_t = std::list<fs::payload::block_request_t>;
    using io_message_ptr = r::intrusive_ptr_t<fs::message::io_commands_t>;
    using io_messages_t = std::list<io_message_ptr>;
    using appended_blocks_t = std::list<fs::payload::append_block_t>;
    using file_finishes_t = std::list<fs::payload::finish_file_t>;

    using supervisor_t::process_io;
    using supervisor_t::supervisor_t;

    void on_io(fs::message::io_commands_t &message) noexcept override {
        if (bypass_io_messages == 0) {
            io_messages.emplace_back(&message);
        } else {
            supervisor_t::on_io(message);
            if (bypass_io_messages > 0) {
                --bypass_io_messages;
            }
        }
    }

    void process_io(fs::payload::append_block_t &req) noexcept override {
        auto copy = fs::payload::append_block_t({}, req.path, req.data, req.offset, req.file_size);
        appended_blocks.emplace_back(std::move(copy));
        supervisor_t::process_io(req);
    }

    void process_io(fs::payload::finish_file_t &req) noexcept override {
        auto copy = fs::payload::finish_file_t({}, req.path, req.conflict_path, req.file_size, req.modification_s);
        file_finishes.emplace_back(std::move(copy));
        supervisor_t::process_io(req);
    }

    void process_io(fs::payload::block_request_t &req) noexcept override {
        supervisor_t::process_io(req);
        if (!block_responces.empty()) {
            auto &res = block_responces.front();
            req.result = std::move(res);
            block_responces.pop_front();
        }
        auto copy = fs::payload::block_request_t({}, req.path, req.offset, req.block_size);
        block_requests.emplace_back(std::move(copy));
    }

    mock_supervisor_t *resume_io(int count = 1) noexcept {
        int i = count;
        while (io_messages.size() && i > 0) {
            auto msg = io_messages.front();
            put(std::move(msg));
            io_messages.pop_front();
            --i;
        }
        bypass_io_messages = count;
        return this;
    }

    void intercept_io(int count) noexcept { bypass_io_messages = count; }

    block_responces_t block_responces;
    block_requests_t block_requests;
    appended_blocks_t appended_blocks;
    file_finishes_t file_finishes;
    int bypass_io_messages = -1;
    io_messages_t io_messages;
};

struct fixture_t {
    using peer_ptr_t = r::intrusive_ptr_t<test_peer_t>;
    using target_ptr_t = r::intrusive_ptr_t<net::controller_actor_t>;
    using io_commands_t = fs::message::io_commands_t;
    using io_commands_t_ptr_t = r::intrusive_ptr_t<io_commands_t>;

    fixture_t(bool auto_start_, int64_t max_sequence_, bool auto_share_ = true) noexcept
        : auto_start{auto_start_}, auto_share{auto_share_}, max_sequence{max_sequence_} {
        test::init_logging();
    }

    void _start_target(std::string_view url) {
        peer_actor = sup->create_actor<test_peer_t>()
                         .cluster(cluster)
                         .peer_device(peer_device)
                         .url(url)
                         .coordinator(sup->get_address())
                         .auto_share(auto_share)
                         .timeout(timeout)
                         .finish();

        sup->do_process();

        target = sup->create_actor<controller_actor_t>()
                     .peer(peer_device)
                     .peer_addr(peer_actor->get_address())
                     .request_pool(1024)
                     .outgoing_buffer_max(1024'000)
                     .cluster(cluster)
                     .sequencer(sup->sequencer)
                     .timeout(timeout)
                     .blocks_max_requested(get_blocks_max_requested())
                     .finish();

        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
        target_addr = target->get_address();
    }

    virtual void start_target() noexcept { _start_target("relay://1.2.3.4:5"); }

    virtual void _tune_peer(db::Device &) noexcept {}

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

        sup = create_supervisor();
        sup->cluster = cluster;
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::registry_plugin_t>(
                [&](auto &p) { p.register_name(net::names::fs_actor, sup->get_address()); });
        };
        sup->do_process();

        auto folder_id_1 = "1234-5678";
        auto folder_id_2 = "5555";
        auto builder = diff_builder_t(*cluster);
        auto sha256 = peer_id.get_sha256();
        builder.upsert_folder(folder_id_1, "")
            .upsert_folder(folder_id_2, "")
            .configure_cluster(sha256)
            .add(sha256, folder_id_1, 123, max_sequence)
            .finish()
            .apply(*sup);

        if (auto_share) {
            builder.share_folder(peer_id.get_sha256(), folder_id_1).apply(*sup);
        }

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

    virtual r::intrusive_ptr_t<mock_supervisor_t> create_supervisor() {
        return ctx.create_supervisor<mock_supervisor_t>().timeout(timeout).create_registry().finish();
    }

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
    r::intrusive_ptr_t<mock_supervisor_t> sup;
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

            _start_target("tcp://1.2.3.4:5");
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
                CHECK(folder_peer->get_file_infos().begin()->get()->get_name()->get_full_name() == file_name);

                auto folder_my = folder_infos.by_device(*my_device);
                REQUIRE(folder_my);
                CHECK(folder_my->get_max_sequence() == 1ul);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                CHECK(folder_my->get_file_infos().begin()->get()->get_name()->get_full_name() == file_name);

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

            SECTION("peer has outdated by sequence view (zero-sequence)") {
                auto &d_my = proto::add_devices(folder);
                proto::set_id(d_my, my_device->device_id().get_sha256());
                proto::set_max_sequence(d_my, 0);
                proto::set_index_id(d_my, folder_1_my->get_index());

                peer_actor->forward(cc);
                sup->do_process();

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() == 1);
                auto msg = &(*queue.back()).payload;
                auto my_index_update = std::get_if<proto::Index>(msg);
                REQUIRE(my_index_update);
                REQUIRE(proto::get_files_size(*my_index_update) == 1);
            }

            SECTION("peer has outdated by index view") {
                auto &d_my = proto::add_devices(folder);
                proto::set_id(d_my, my_device->device_id().get_sha256());
                proto::set_max_sequence(d_my, folder_1_my->get_max_sequence());
                proto::set_index_id(d_my, folder_1_my->get_index() + 5);

                peer_actor->forward(cc);
                sup->do_process();

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() == 1);
                auto msg = &(*queue.back()).payload;
                auto my_index_update = std::get_if<proto::Index>(msg);
                REQUIRE(my_index_update);
                REQUIRE(proto::get_files_size(*my_index_update) == 1);
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

                peer_actor->push_response(data_1, 0);
                peer_actor->process_block_requests();
                sup->do_process();

                CHECK(!folder_my->get_folder()->is_synchronizing());
                REQUIRE(folder_my);
                CHECK(folder_my->get_max_sequence() == 1ul);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = *folder_my->get_file_infos().begin();
                REQUIRE(f);
                CHECK(f->get_name()->get_full_name() == file_name);
                CHECK(f->get_size() == 5);
                CHECK(f->iterate_blocks().get_total() == 1);
                CHECK(f->is_locally_available());
                CHECK(peer_actor->blocks_requested == 1);

                auto &queue = peer_actor->messages;
                REQUIRE(queue.size() > 0);

                auto msg = &(*queue.back()).payload;
                auto &my_index_update = std::get<proto::Index>(*msg);
                REQUIRE(proto::get_files_size(my_index_update) == 1);

                SECTION("don't redownload file only if metadata has changed") {
                    auto index_update = proto::IndexUpdate{};
                    proto::set_folder(index_update, proto::get_folder(index));
                    proto::set_sequence(file, folder_1_peer->get_max_sequence() + 1);
                    proto::set_value(counter, 2);
                    proto::add_files(index_update, file);

                    peer_actor->forward(index_update);
                    sup->do_process();
                    CHECK(peer_actor->blocks_requested == 1);
                    CHECK(folder_my->get_max_sequence() == 2ul);
                    f = *folder_my->get_file_infos().begin();
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

                auto b2 = proto::BlockInfo();
                proto::set_hash(b2, data_2_hash);
                proto::set_size(b2, 5);
                proto::set_offset(b2, 5);

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
                    peer_actor->push_response(data_1, 0);
                    peer_actor->push_response(data_2, 1);
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
                        CHECK(f->iterate_blocks().get_total() == 1);
                        CHECK(f->is_locally_available());
                    }
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_2);
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->iterate_blocks().get_total() == 1);
                        CHECK(f->is_locally_available());
                    }
                }

                SECTION("with the same block") {
                    proto::add_blocks(*file_2, b1);

                    auto folder_my = folder_infos.by_device(*my_device);
                    CHECK(folder_my->get_max_sequence() == 0ul);
                    CHECK(!folder_my->get_folder()->is_synchronizing());

                    peer_actor->forward(index);
                    peer_actor->push_response(data_1, 0);
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
                        CHECK(f->iterate_blocks().get_total() == 1);
                        CHECK(f->is_locally_available());
                    }
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_2);
                        REQUIRE(f);
                        CHECK(f->get_size() == 5);
                        CHECK(f->iterate_blocks().get_total() == 1);
                        CHECK(f->is_locally_available());
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
                    peer_actor->push_response(data_1, 0);
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
                        CHECK(f->iterate_blocks().get_total() == 1);
                        CHECK(f->is_locally_available());
                    }
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_2);
                        REQUIRE(f);
                        CHECK(f->get_size() == 10);
                        CHECK(f->iterate_blocks().get_total() == 2);
                        CHECK(f->is_locally_available());
                    }
                    CHECK(sup->file_finishes.size() == 2);
                }
                SECTION("with multiple clones") {
                    cluster->modify_write_requests(99);
                    auto f1 = *file_1;
                    auto f2 = *file_2;
                    auto b1_copy = b1;
                    auto b2_copy = b2;

                    proto::clear_files(index);
                    proto::add_files(index, f1);

                    proto::add_blocks(f2, b1_copy);
                    proto::add_blocks(f2, b1_copy);
                    proto::add_blocks(f2, b2_copy);
                    proto::add_blocks(f2, b2_copy);
                    proto::set_size(f2, 20);

                    proto::add_blocks(*file_1, b2_copy);
                    proto::set_size(*file_1, 10);

                    auto folder_my = folder_infos.by_device(*my_device);
                    CHECK(folder_my->get_max_sequence() == 0ul);
                    CHECK(!folder_my->get_folder()->is_synchronizing());

                    peer_actor->forward(index);
                    peer_actor->push_response(data_1, 0);
                    peer_actor->push_response(data_2, 1);
                    sup->do_process();

                    CHECK(sup->file_finishes.size() == 1);
                    CHECK(!folder_my->get_folder()->is_synchronizing());
                    CHECK(peer_actor->blocks_requested == 2);
                    REQUIRE(folder_my);
                    CHECK(folder_my->get_max_sequence() == 1ul);
                    REQUIRE(folder_my->get_file_infos().size() == 1);
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_1);
                        REQUIRE(f);
                        CHECK(f->get_size() == 10);
                        CHECK(f->iterate_blocks().get_total() == 2);
                        CHECK(f->is_locally_available());
                    }

                    auto index_update = proto::IndexUpdate();
                    proto::set_folder(index_update, folder_1->get_id());
                    proto::add_files(index_update, f2);
                    peer_actor->forward(index_update);
                    sup->do_process();

                    CHECK(sup->file_finishes.size() == 2);
                    CHECK(!folder_my->get_folder()->is_synchronizing());
                    CHECK(peer_actor->blocks_requested == 2);
                    {
                        auto f = folder_my->get_file_infos().by_name(file_name_2);
                        REQUIRE(f);
                        CHECK(f->get_size() == 20);
                        CHECK(f->iterate_blocks().get_total() == 4);
                        CHECK(f->is_locally_available());
                    }
                }
            }

            SECTION("don't attempt to download a file, which is deleted") {
                auto folder_peer = folder_infos.by_device(*peer_device);
                auto file_name = std::string_view("some-file");

                proto::set_max_sequence(*d_peer, folder_1_peer->get_max_sequence() + 1);
                peer_actor->forward(cc);
                sup->do_process();

                auto index_update_0 = proto::IndexUpdate{};
                proto::set_folder(index_update_0, folder_1->get_id());
                auto &pr_fi = proto::add_files(index_update_0);
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

                peer_actor->forward(index_update_0);
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
                auto f = *folder_my->get_file_infos().begin();
                REQUIRE(f);
                CHECK(f->get_name()->get_full_name() == file_name);
                CHECK(f->get_size() == 0);
                CHECK(f->iterate_blocks().get_total() == 0);
                CHECK(f->is_locally_available());
                CHECK(f->is_deleted());
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
                peer_actor->push_response(data_1, 0);
                sup->do_process();

                auto folder_my = folder_infos.by_device(*my_device);
                CHECK(folder_my->get_max_sequence() == 1);
                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = *folder_my->get_file_infos().begin();
                REQUIRE(f);
                CHECK(f->get_name()->get_full_name() == file_name);
                CHECK(f->get_size() == 5);
                CHECK(f->iterate_blocks().get_total() == 1);
                CHECK(f->is_locally_available());

                auto fp = *folder_1_peer->get_file_infos().begin();
                REQUIRE(fp);
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
                peer_actor->push_response(data_1, 0);
                sup->do_process();

                REQUIRE(folder_my->get_file_infos().size() == 1);
                auto f = *folder_my->get_file_infos().begin();
                REQUIRE(f);
                CHECK(f->get_name()->get_full_name() == file_name);
                CHECK(f->get_size() == 5);
                CHECK(f->iterate_blocks().get_total() == 1);
                CHECK(f->is_locally_available());
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

                auto b1 = proto::BlockInfo();
                proto::set_hash(b1, data_1_hash);
                proto::set_size(b1, 5);
                proto::add_blocks(file_1, b1);
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
                proto::add_blocks(pr_file_my, b1);

                auto &v_my = proto::get_version(pr_file_my);
                proto::add_counters(v_my, proto::Counter(my_device->device_id().get_uint(), 1));

                auto uuid = sup->sequencer->next_uuid();
                auto file_my = model::file_info_t::create(uuid, pr_file_my, folder_my).value();
                file_my->assign_block(bi_1.get(), 0);
                file_my->mark_local_available(0);
                REQUIRE(folder_my->add_strict(file_my));

                peer_actor->forward(index);
                peer_actor->push_response(data_2, 0);
                cluster->modify_write_requests(10);
                sup->do_process();

                REQUIRE(folder_my->get_file_infos().size() == 2);
                auto f = folder_my->get_file_infos().by_name(file_name_1);
                REQUIRE(f);
                CHECK(f->get_name()->get_full_name() == file_name_1);
                CHECK(f->get_size() == 10);
                CHECK(f->iterate_blocks().get_total() == 2);
                CHECK(f->is_locally_available());
            }
        }
    };
    F(true, 10).run();
}

void test_downloading_errors() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        struct custom_supervisor_t : mock_supervisor_t {
            using parent_t = mock_supervisor_t;
            using parent_t::parent_t;
            using parent_t::process_io;

            bool reject_blocks = false;

            void process_io(fs::payload::append_block_t &req) noexcept {
                if (reject_blocks) {
                    // error by default;
                } else {
                    parent_t::process_io(req);
                }
            }
        };

        r::intrusive_ptr_t<mock_supervisor_t> create_supervisor() override {
            return ctx.create_supervisor<custom_supervisor_t>().timeout(timeout).create_registry().finish();
        }

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

            cluster->modify_write_requests(10);
            auto expected_state = r::state_t::OPERATIONAL;
            bool expected_unreachability = true;
            auto initial_write_pool = cluster->get_write_requests();
            SECTION("general error, ok, do not shutdown") { peer_actor->push_response(proto::ErrorCode::GENERIC, 0); }
            SECTION("hash mismatch, do not shutdown") {
                peer_actor->push_response(as_owned_bytes("123"), 0);
                peer_actor->push_response(data_2, 0); // needed to terminate/shutdown controller
            }
            SECTION("rejecting blocks") {
                auto custom_sup = static_cast<custom_supervisor_t *>(sup.get());
                custom_sup->reject_blocks = true;
                peer_actor->push_response(data_1, 0);
                peer_actor->push_response(data_2, 1);
                expected_state = r::state_t::SHUT_DOWN;
                expected_unreachability = false;
            }
            SECTION("wrong request id") {
                proto::Response res;
                proto::set_id(res, 99);
                proto::set_data(res, data_1);
                peer_actor->forward(std::move(res));
                expected_state = r::state_t::SHUT_DOWN;
                expected_unreachability = false;
            }
            SECTION("wrong request id (2)") {
                proto::Response res;
                proto::set_id(res, 0);
                proto::set_data(res, data_1);
                proto::Response res_2;
                proto::set_id(res_2, 0);
                proto::set_data(res_2, data_2);
                peer_actor->forward(res);
                peer_actor->forward(res_2);
                expected_state = r::state_t::SHUT_DOWN;
                expected_unreachability = false;
            }

            sup->do_process();

            CHECK(peer_actor->blocks_requested <= 2);
            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == expected_state);

            auto folder_peer = folder_infos.by_device(*peer_device);
            REQUIRE(folder_peer->get_file_infos().size() == 1);
            auto f = *folder_peer->get_file_infos().begin();
            REQUIRE(f);
            CHECK(f->is_unreachable() == expected_unreachability);
            CHECK(!f->is_synchronizing());

            auto f_local = folder_my->get_file_infos().by_name(f->get_name()->get_full_name());
            CHECK(!f_local);
            CHECK(!folder_my->get_folder()->is_synchronizing());

            sup->do_process();
            CHECK(cluster->get_write_requests() == initial_write_pool);
        }
    };
    F(true, 10).run();
}

void test_download_from_scratch() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            cluster->modify_write_requests(10);
            sup->do_process();
            peer_actor->messages.clear();

            auto builder = diff_builder_t(*cluster);
            auto sha256 = peer_device->device_id().get_sha256();

            auto cc = proto::ClusterConfig{};
            auto folder = proto::Folder();
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
            proto::add_folders(cc, folder);
            peer_actor->forward(cc);
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);
            REQUIRE(peer_actor->messages.size() == 1);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                auto cc = std::get_if<proto::ClusterConfig>(peer_msg);
                REQUIRE(cc);
                REQUIRE(proto::get_folders_size(*cc) == 1);
                auto &folder = proto::get_folders(*cc, 0);
                CHECK(proto::get_id(folder) == folder_1->get_id());
            }

            auto index = proto::Index{};
            proto::set_folder(index, folder_1->get_id());
            auto file_name = std::string_view("some-file");
            auto &file = proto::add_files(index);
            proto::set_name(file, file_name);
            proto::set_type(file, proto::FileInfoType::FILE);
            proto::set_sequence(file, 154);
            proto::set_size(file, 10);
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

            auto data_2 = as_owned_bytes("67890");
            auto data_2_h = utils::sha256_digest(data_2).value();
            auto &b2 = proto::add_blocks(file);
            proto::set_hash(b2, data_2_h);
            proto::set_size(b2, data_2.size());

            peer_actor->forward(index);
            peer_actor->push_response(data_1, 0);
            peer_actor->push_response(data_2, 1);
            sup->do_process();

            auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
            CHECK(folder_my->get_max_sequence() == 1ul);
            CHECK(!folder_my->get_folder()->is_synchronizing());

            auto f = folder_my->get_file_infos().by_name(file_name);
            REQUIRE(f);
            CHECK(f->get_size() == 10);
            CHECK(f->iterate_blocks().get_total() == 2);
            CHECK(f->is_locally_available());

            cc = proto::ClusterConfig{};
            folder = proto::Folder();
            proto::set_id(folder, folder_1->get_id());
            {
                auto &device = proto::add_devices(folder);
                proto::set_id(device, peer_device->device_id().get_sha256());
                proto::set_max_sequence(device, 15);
                proto::set_index_id(device, 0x12345);
            }
            {
                auto fi_my = folder_1->get_folder_infos().by_device(*my_device);
                auto &device = proto::add_devices(folder);
                proto::set_id(device, my_device->device_id().get_sha256());
                proto::set_max_sequence(device, 0);
                proto::set_index_id(device, fi_my->get_index());
            }

            peer_actor->messages.clear();
            proto::add_folders(cc, folder);
            peer_actor->forward(cc);
            sup->do_process();

            REQUIRE(peer_actor->messages.size() == 1);
            {
                auto peer_msg = &peer_actor->messages.front()->payload;
                REQUIRE(std::get_if<proto::Index>(peer_msg));
            }
            CHECK(sup->file_finishes.size() == 1);
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
            proto::set_index_id(d_peer, 0x12345);

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
            peer_actor->push_response(data_1, 0);
            sup->do_process();

            target->do_shutdown();
            sup->do_process();
            // auto fs_timer_id = sup->timers.back()->request_id;
            // sup->do_invoke_timer(fs_timer_id);
            // sup->do_process();

            CHECK(!folder_1->is_synchronizing());
            for (auto &it : cluster->get_blocks()) {
                REQUIRE(!it->is_locked());
            }

            start_target();
            peer_actor->forward(cc);
            peer_actor->push_response(data_2, 0);
            sup->do_process();

            auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
            CHECK(folder_my->get_max_sequence() == 1ul);
            CHECK(!folder_my->get_folder()->is_synchronizing());

            auto f = folder_my->get_file_infos().by_name(file_name);
            REQUIRE(f);
            CHECK(f->get_size() == 10);
            CHECK(f->iterate_blocks().get_total() == 2);
            CHECK(f->is_locally_available());
        }
    };
    F(false, 10, false).run();
}

void test_uniqueness() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_my = folder_infos.by_device(*my_device);
            auto folder_peer = folder_infos.by_device(*peer_device);

            auto builder = diff_builder_t(*cluster);
            auto folder_1_id = folder_1->get_id();
            auto sha256 = peer_device->device_id().get_sha256();
            builder.configure_cluster(sha256)
                .add(sha256, folder_1_id, folder_peer->get_index(), 10)
                .finish()
                .apply(*sup, target.get());

            auto pr_file_1 = proto::FileInfo();
            proto::set_name(pr_file_1, boost::nowide::narrow(L"ФАЙЛ"));
            proto::set_type(pr_file_1, proto::FileInfoType::FILE);
            proto::set_sequence(pr_file_1, 9);

            auto pr_file_2 = proto::FileInfo();
            proto::set_name(pr_file_2, boost::nowide::narrow(L"файл"));
            proto::set_type(pr_file_2, proto::FileInfoType::FILE);
            proto::set_sequence(pr_file_2, 10);

            builder.make_index(sha256, folder_1_id)
                .add(pr_file_1, peer_device)
                .add(pr_file_2, peer_device)
                .finish()
                .apply(*sup, target.get());

#if defined(SYNCSPIRIT_WIN) || defined(SYNCSPIRIT_MAC)
            REQUIRE(folder_my->get_file_infos().size() == 0);
#else
            REQUIRE(folder_my->get_file_infos().size() == 2);
#endif
        }
    };
    F(true, 10, true).run();
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
            auto folder = proto::Folder();
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

            proto::add_folders(cc, folder);
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

            auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
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
                CHECK(proto::get_index_id(*f_peer) == 0x12345);
                CHECK(proto::get_max_sequence(*f_peer) == 0);

                REQUIRE(f_my);
                CHECK(proto::get_index_id(*f_my) == folder_my->get_index());
                CHECK(proto::get_max_sequence(*f_my) == 0);
            }

            cc = proto::ClusterConfig{};
            folder = proto::Folder();
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
                proto::set_index_id(device, folder_my->get_index());
            }

            proto::add_folders(cc, folder);
            peer_actor->forward(cc);
            peer_actor->messages.clear();
            sup->do_process();

            CHECK(peer_actor->messages.size() == 0);

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
            auto &index_update = std::get<proto::Index>(msg->payload);
            REQUIRE(proto::get_files_size(index_update) == 1);
            CHECK(proto::get_name(proto::get_files(index_update, 0)) == "a.txt");
        }
    };
    F(true, 10).run();
}

void test_uploading() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        void _tune_peer(db::Device &device) noexcept override {
            db::set_compression(device, proto::Compression::ALWAYS);
        }

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
            auto data_begin = (const unsigned char *)data.data();
            auto data_end = (const unsigned char *)data.data() + data.size();

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
            file_info->assign_block(b.get(), 0);
            REQUIRE(folder_my->add_strict(file_info));

            auto req = proto::Request();
            proto::set_id(req, 1);
            proto::set_folder(req, folder_1->get_id());
            proto::set_name(req, file_name);
            proto::set_size(req, data.size());

            peer_actor->forward(cc);
            SECTION("upload regular file, no hash") {
                peer_actor->forward(req);
                auto res = outcome::result<utils::bytes_t>(data_1);
                sup->block_responces.emplace_back(res);

                sup->do_process();
                REQUIRE(sup->block_responces.size() == 0);
                REQUIRE(sup->block_requests.size() == 1);
                auto &req = sup->block_requests.front();
                CHECK(req.path.filename() == file_name);
                CHECK(req.offset == 0);
                CHECK(req.block_size == data_1.size());

                REQUIRE(peer_actor->in_responses.size() == 1);
                auto &peer_res = peer_actor->in_responses.front();
                CHECK(proto::get_id(peer_res) == 1);
                CHECK(proto::get_code(peer_res) == proto::ErrorCode::NO_BEP_ERROR);
                CHECK(proto::get_data(peer_res) == data_1);
            }
            SECTION("fs error") {
                peer_actor->forward(req);
                auto ec = utils::make_error_code(utils::error_code_t::fs_error);
                auto res = outcome::result<utils::bytes_t>(ec);
                sup->block_responces.emplace_back(res);

                sup->do_process();
                REQUIRE(sup->block_responces.size() == 0);
                REQUIRE(sup->block_requests.size() == 1);
                auto &req = sup->block_requests.front();
                CHECK(req.path.filename() == file_name);
                CHECK(req.offset == 0);
                CHECK(req.block_size == data_1.size());

                REQUIRE(peer_actor->in_responses.size() == 1);
                auto &peer_res = peer_actor->in_responses.front();
                CHECK(proto::get_id(peer_res) == 1);
                CHECK(proto::get_code(peer_res) == proto::ErrorCode::GENERIC);
                CHECK(proto::get_data(peer_res).size() == 0);
            }
        }
    };
    F(true, 10).run();
}

void test_overload_uploading() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        std::uint32_t get_blocks_max_requested() override { return 2; }

        void main(diff_builder_t &) noexcept override {
            static constexpr size_t BLOCKS_COUNT = 20;

            auto builder = diff_builder_t(*cluster);
            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_my = folder_infos.by_device(*my_device);
            auto folder_peer = folder_infos.by_device(*peer_device);

            auto file_name = std::string_view("data.bin");
            auto file = proto::FileInfo();
            proto::set_name(file, file_name);
            proto::set_type(file, proto::FileInfoType::FILE);
            proto::set_sequence(file, folder_my->get_max_sequence() + 1);
            proto::set_size(file, BLOCKS_COUNT * 5);
            proto::set_block_size(file, 5);

            auto pieces = std::vector<utils::bytes_t>();
            for (size_t i = 0; i < BLOCKS_COUNT; ++i) {
                auto data = std::string(5, 'a' + static_cast<char>(i));
                auto data_begin = reinterpret_cast<const unsigned char *>(data.data());
                auto data_end = data_begin + data.size();
                auto data_bytes = utils::bytes_t(data_begin, data_end);
                auto data_h = utils::sha256_digest(data_bytes).value();
                auto &b1 = proto::add_blocks(file);
                proto::set_hash(b1, data_h);
                proto::set_size(b1, data.size());
                pieces.emplace_back(data_bytes);
            }
            auto &v = proto::get_version(file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);
            proto::set_value(counter, 1);

            builder.local_update(folder_1->get_id(), file).apply(*sup);

            auto sha256 = peer_device->device_id().get_sha256();

            builder.configure_cluster(sha256)
                .add(sha256, folder_1->get_id(), folder_peer->get_index(), 1)
                .add(my_device->device_id().get_sha256(), folder_1->get_id(), folder_my->get_index(), 1)
                .finish()
                .apply(*sup);

            for (size_t i = 0; i < BLOCKS_COUNT; ++i) {
                auto req = proto::Request();
                proto::set_id(req, 1);
                proto::set_folder(req, folder_1->get_id());
                proto::set_name(req, file_name);
                proto::set_size(req, 5);
                proto::set_offset(req, i * 5);
                peer_actor->forward(req);

                auto res = outcome::result<utils::bytes_t>(pieces[i]);
                sup->block_responces.emplace_back(res);
            }
            sup->do_process();

            auto &blocks = peer_actor->in_responses;
            REQUIRE(blocks.size() == BLOCKS_COUNT);
            for (size_t i = 0; i < BLOCKS_COUNT; ++i) {
                auto &res = blocks.front();
                auto data = proto::get_data(res);
                CHECK(data == pieces[i]);
                blocks.pop_front();
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

void test_peer_down() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            sup->do_process();

            auto builder = diff_builder_t(*cluster, sup->get_address());
            auto sha256 = peer_device->device_id().get_sha256();

            auto cc = proto::ClusterConfig{};
            auto &folder = proto::add_folders(cc);
            proto::set_id(folder, folder_1->get_id());
            auto &d_peer = proto::add_devices(folder);
            proto::set_id(d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(d_peer, 15);
            proto::set_index_id(d_peer, 0x12345);

            peer_actor->forward(cc);
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);
            auto folder_peer = folder_1->get_folder_infos().by_device(*peer_device);
            REQUIRE(folder_peer->get_index() == proto::get_index_id(d_peer));

            peer_actor->shutdown_start_callback = [&]() {
                proto::FileInfo pr_file_info;
                auto file_name = std::string_view("link");
                proto::set_name(pr_file_info, file_name);
                proto::set_type(pr_file_info, proto::FileInfoType::SYMLINK);
                proto::set_symlink_target(pr_file_info, "/some/where");

                builder.local_update(folder_1->get_id(), pr_file_info).send(*sup);
            };
            peer_actor->do_shutdown();
            sup->do_process();
        }
    };
    F(false, 10, false).run();
}

void test_conflicts() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            sup->do_process();

            auto builder = diff_builder_t(*cluster);
            auto sha256 = peer_device->device_id().get_sha256();

            auto cc = proto::ClusterConfig{};

            auto folder = proto::Folder();
            proto::set_id(folder, folder_1->get_id());
            auto d_peer = proto::Device();
            proto::set_id(d_peer, peer_device->device_id().get_sha256());
            proto::set_max_sequence(d_peer, 15);
            proto::set_index_id(d_peer, 0x12345);
            proto::add_devices(folder, d_peer);

            proto::add_folders(cc, folder);
            peer_actor->forward(cc);
            sup->do_process();

            builder.share_folder(sha256, folder_1->get_id()).apply(*sup);
            auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
            auto folder_peer = folder_1->get_folder_infos().by_device(*peer_device);
            REQUIRE(folder_peer->get_index() == proto::get_index_id(d_peer));

            cc = proto::ClusterConfig{};
            folder = proto::Folder();
            proto::set_id(folder, folder_1->get_id());
            proto::add_devices(folder, d_peer);
            auto d_my = proto::Device();
            proto::set_id(d_my, my_device->device_id().get_sha256());
            proto::set_max_sequence(d_my, folder_my->get_max_sequence());
            proto::set_index_id(d_my, folder_my->get_index());
            proto::add_devices(folder, d_my);
            proto::add_folders(cc, folder);
            peer_actor->forward(cc);
            sup->do_process();

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
            peer_actor->push_response(data_1, 0);
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

            peer_actor->messages.clear();
            sup->appended_blocks.clear();
            sup->file_finishes.clear();
            SECTION("local win") {
                proto::set_modified_s(file, 1734670000);
                proto::set_value(c_1, proto::get_value(local_file->get_version().get_best()) - 1);
                auto local_seq = local_file->get_sequence();

                proto::add_files(index_update, file);
                peer_actor->forward(index_update);
                sup->do_process();

                REQUIRE(local_folder->get_file_infos().size() == 1);
                auto lf = local_folder->get_file_infos().by_sequence(local_seq);
                REQUIRE(local_seq == lf->get_sequence());
                CHECK(cluster->get_blocks().size() == 2);

                CHECK(peer_actor->messages.size() == 0);
                CHECK(sup->appended_blocks.size() == 0);
                CHECK(sup->file_finishes.size() == 0);
            }
            SECTION("remote win (non-empty)") {
                proto::set_modified_s(file, 1734690000);
                proto::set_value(c_1, proto::get_value(local_file->get_version().get_best()) + 1);
                proto::add_files(index_update, file);
                peer_actor->push_response(data_3, 0);
                peer_actor->forward(index_update);
                sup->do_process();

                auto local_folder = folder_infos.by_device(*my_device);
                auto local_conflict = local_folder->get_file_infos().by_name(local_file->make_conflicting_name());
                REQUIRE(local_conflict);
                CHECK(local_conflict->get_size() == 5);
                REQUIRE(local_conflict->iterate_blocks().get_total() == 1);
                CHECK(local_conflict->iterate_blocks(0).next()->get_hash() == data_2_h);

                auto file = local_folder->get_file_infos().by_name(local_file->get_name()->get_full_name());
                REQUIRE(file);
                CHECK(file->get_size() == 5);
                REQUIRE(file->iterate_blocks().get_total() == 1);
                CHECK(file->iterate_blocks(0).next()->get_hash() == data_3_h);

                CHECK(cluster->get_blocks().size() == 2);

                auto &msg = peer_actor->messages.back();
                REQUIRE(peer_actor->messages.size() == 1);
                auto &index_update_sent = std::get<proto::IndexUpdate>(msg->payload);
                REQUIRE(proto::get_files_size(index_update_sent) == 2);
                auto &f1 = proto::get_files(index_update_sent, 0);
                auto &f2 = proto::get_files(index_update_sent, 1);
                CHECK(proto::get_name(f1) == local_conflict->get_name()->get_full_name());
                CHECK(proto::get_name(f2) == file->get_name()->get_full_name());

                REQUIRE(sup->appended_blocks.size() == 1);
                auto &appended_block = sup->appended_blocks.front();
                CHECK(appended_block.path == file->get_path(*local_folder));
                CHECK(appended_block.file_size == 5);
                CHECK(appended_block.offset == 0);
                CHECK(appended_block.data == data_3);

                REQUIRE(sup->file_finishes.size() == 1);
                auto &file_finish = sup->file_finishes.front();
                CHECK(file_finish.path == file->get_path(*local_folder));
                CHECK(file_finish.conflict_path == local_conflict->get_path(*local_folder));
                CHECK(file_finish.file_size == 5);
                CHECK(file_finish.modification_s == file->get_modified_s());
            }
            SECTION("remote win (empty, new file type: directory)") {
                proto::set_modified_s(file, 1734690000);
                proto::set_value(c_1, proto::get_value(local_file->get_version().get_best()) + 1);
                proto::clear_blocks(file);
                proto::set_type(file, proto::FileInfoType::DIRECTORY);

                proto::add_files(index_update, file);
                peer_actor->forward(index_update);
                sup->do_process();

                auto local_folder = folder_infos.by_device(*my_device);
                auto local_conflict = local_folder->get_file_infos().by_name(local_file->make_conflicting_name());
                REQUIRE(local_conflict);
                CHECK(local_conflict->get_size() == 5);
                REQUIRE(local_conflict->iterate_blocks().get_total() == 1);
                CHECK(local_conflict->iterate_blocks(0).next()->get_hash() == data_2_h);

                auto file = local_folder->get_file_infos().by_name(local_file->get_name()->get_full_name());
                REQUIRE(file);
                CHECK(file->get_size() == 0);
                REQUIRE(file->iterate_blocks().get_total() == 0);
                CHECK(cluster->get_blocks().size() == 1);

                auto &msg = peer_actor->messages.back();
                REQUIRE(peer_actor->messages.size() == 1);
                auto &index_update_sent = std::get<proto::IndexUpdate>(msg->payload);
                REQUIRE(proto::get_files_size(index_update_sent) == 2);
                auto &f1 = proto::get_files(index_update_sent, 0);
                auto &f2 = proto::get_files(index_update_sent, 1);
                CHECK(proto::get_name(f1) == local_conflict->get_name()->get_full_name());
                CHECK(proto::get_name(f2) == file->get_name()->get_full_name());

                REQUIRE(sup->appended_blocks.size() == 0);
                REQUIRE(sup->file_finishes.size() == 0);
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
            proto::set_index_id(d_peer, 0x12345);

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
                    peer_actor->push_response(data_1, 0);
                    peer_actor->push_response(data_2, 1);
                    peer_actor->process_block_requests();
                    sup->do_process();
                    auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                    CHECK(folder_my->get_file_infos().size() == 0);
                }
                SECTION("remove folder") {
                    sup->auto_ack_io = false;

                    peer_actor->push_response(data_2, 0);
                    peer_actor->process_block_requests();
                    sup->do_process();

                    builder.remove_folder(*folder_1).apply(*sup);
                    sup->do_process();

                    hasher->process_requests();
                    sup->do_process();

                    peer_actor->push_response(data_1, 0);
                    peer_actor->process_block_requests();
                    sup->do_process();
                    CHECK(peer_actor->blocks_requested == proto::get_blocks_size(file));
                    CHECK(!cluster->get_folders().by_id(proto::get_id(folder)));
                }
            }
            SECTION("hash validation replies") {
                SECTION("folder is kept") {
                    peer_actor->push_response(data_1, 0);
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
                    peer_actor->push_response(data_1, 0);
                    peer_actor->process_block_requests();
                    hasher->process_requests();
                    sup->do_process();
                    CHECK(!cluster->get_folders().by_id(proto::get_id(folder)));
                }
            }
            SECTION("block acks from fs") {
                auto write_requests = cluster->get_write_requests();
                sup->intercept_io(0);
                hasher->auto_reply = true;
                peer_actor->push_response(data_1, 0);
                peer_actor->push_response(data_2, 1);
                peer_actor->process_block_requests();
                sup->do_process();
                REQUIRE(!sup->io_messages.empty());

                SECTION("suspend") {
                    builder.suspend(*folder_1);
                    sup->resume_io(1);
                    builder.apply(*sup);
                    auto folder_my = folder_1->get_folder_infos().by_device(*my_device);
                    CHECK(folder_my->get_file_infos().size() == 0);
                }
                SECTION("remove") {
                    builder.remove_folder(*folder_1).apply(*sup);
                    sup->resume_io(1)->do_process();
                    CHECK(!cluster->get_folders().by_id(proto::get_id(folder)));
                    CHECK(!sup->io_messages.empty());
                }
                SECTION("down controller") {
                    target->do_shutdown();
                    sup->do_process();
                    CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() ==
                          r::state_t::SHUTTING_DOWN);

                    SECTION("ack upon shutdown") { sup->resume_io(2)->do_process(); }
                    SECTION("no ack, timeout trigger") {
                        auto fs_timer_id = sup->timers.back()->request_id;
                        sup->do_invoke_timer(fs_timer_id);
                        sup->do_process();
                    }

                    CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::SHUT_DOWN);
                    CHECK(cluster->get_write_requests() == write_requests);
                }
                sup->resume_io(2)->do_process();
            }
        }

        bool hasher_auto_reply = false;
        managed_hasher_t *hasher;
    };
    F(false, 10, false).run();
}

void test_change_folder_type() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        std::uint32_t get_blocks_max_requested() override { return 1; }

        void main(diff_builder_t &builder) noexcept override {
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
            sup->do_process();

            SECTION("send & receive -> send only") {
                auto index = proto::Index{};
                proto::set_folder(index, folder_1->get_id());
                auto file_name_1 = std::string_view("some-file-1");
                auto file_name_2 = std::string_view("some-file-2");

                auto &file_1 = proto::add_files(index);
                proto::set_name(file_1, file_name_1);
                proto::set_type(file_1, proto::FileInfoType::FILE);
                proto::set_sequence(file_1, folder_1_peer->get_max_sequence() + 1);
                proto::set_block_size(file_1, 5);
                proto::set_size(file_1, 5);

                auto &v_1 = proto::get_version(file_1);
                auto &counter_1 = proto::add_counters(v_1);
                proto::set_id(counter_1, 1);

                auto data_1 = as_owned_bytes("12345");
                auto data_1_hash = utils::sha256_digest(data_1).value();

                auto data_2 = as_owned_bytes("67890");
                auto data_2_hash = utils::sha256_digest(data_2).value();

                auto &b1 = proto::add_blocks(file_1);
                proto::set_hash(b1, data_1_hash);
                proto::set_size(b1, 5);

                auto &file_2 = proto::add_files(index);
                proto::set_name(file_2, file_name_2);
                proto::set_type(file_2, proto::FileInfoType::FILE);
                proto::set_sequence(file_2, folder_1_peer->get_max_sequence() + 2);
                proto::set_block_size(file_2, 5);
                proto::set_size(file_2, 5);

                auto &v_2 = proto::get_version(file_2);
                auto &counter_2 = proto::add_counters(v_2);
                proto::set_id(counter_2, 1);

                auto &b2 = proto::add_blocks(file_2);
                proto::set_hash(b2, data_2_hash);
                proto::set_size(b2, 5);

                CHECK(folder_my->get_max_sequence() == 0ul);
                peer_actor->forward(index);
                sup->do_process();

                REQUIRE(peer_actor->blocks_requested == 1);

                SECTION("folder type is kept as send/receive") {
                    peer_actor->push_response(data_1, 0);
                    peer_actor->process_block_requests();
                    sup->do_process();
                    REQUIRE(peer_actor->blocks_requested == 2);
                }
                SECTION("folder type changed to send only") {
                    auto db_folder = db::Folder();
                    folder_1->serialize(db_folder);
                    db::set_folder_type(db_folder, db::FolderType::send);
                    builder.upsert_folder(db_folder, folder_my->get_index()).apply(*sup);

                    peer_actor->push_response(data_1, 0);
                    peer_actor->process_block_requests();
                    sup->do_process();

                    REQUIRE(peer_actor->blocks_requested == 1);

                    SECTION("folder type is send & receive again") {
                        db::set_folder_type(db_folder, db::FolderType::send_and_receive);
                        builder.upsert_folder(db_folder, folder_my->get_index()).apply(*sup);
                        REQUIRE(peer_actor->blocks_requested == 2);
                    }
                }
            }
            SECTION("send & receive -> recv only") {
                proto::FileInfo pr_file_1;
                auto file_name_1 = std::string_view("file-name.1");
                proto::set_name(pr_file_1, file_name_1);

                builder.local_update(folder_1->get_id(), pr_file_1);
                builder.apply(*sup);
                REQUIRE(peer_actor->messages.size() >= 1);
                auto &last_message = *peer_actor->messages.back();
                auto &index_update_1 = std::get<proto::Index>(last_message.payload);
                CHECK(proto::get_files_size(index_update_1) == 1);

                peer_actor->messages.clear();
                proto::FileInfo pr_file_2;
                auto file_name_2 = std::string_view("file-name.2");
                proto::set_name(pr_file_2, file_name_2);

                SECTION("folder type is kept as send/receive") {
                    builder.local_update(folder_1->get_id(), pr_file_1);
                    builder.apply(*sup);
                    REQUIRE(peer_actor->messages.size() == 1);
                    auto &last_message = *peer_actor->messages.back();
                    auto &index_update_2 = std::get<proto::IndexUpdate>(last_message.payload);
                    CHECK(proto::get_files_size(index_update_2) == 1);
                }
                SECTION("folder type changed to send only") {
                    auto db_folder = db::Folder();
                    folder_1->serialize(db_folder);
                    db::set_folder_type(db_folder, db::FolderType::receive);
                    builder.upsert_folder(db_folder, folder_my->get_index()).apply(*sup);

                    builder.local_update(folder_1->get_id(), pr_file_1).apply(*sup);
                    REQUIRE(peer_actor->messages.size() == 0);

                    SECTION("folder type is send & receive again") {
                        db::set_folder_type(db_folder, db::FolderType::send_and_receive);
                        builder.upsert_folder(db_folder, folder_my->get_index()).apply(*sup);

                        REQUIRE(peer_actor->messages.size() == 1);
                        auto &last_message = *peer_actor->messages.back();
                        auto &index = std::get<proto::Index>(last_message.payload);
                        CHECK(proto::get_files_size(index) == 1);
                    }
                }
            }
        }
    };
    F(true, 10).run();
}

void test_pausing() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            auto builder = diff_builder_t(*cluster);
            auto sha256 = peer_device->device_id().get_sha256();

            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_1_id = folder_1->get_id();
            auto folder_my = folder_infos.by_device(*my_device);
            auto folder_peer = folder_infos.by_device(*peer_device);

            auto max_seq = folder_peer->get_max_sequence() + 10;

            builder.configure_cluster(sha256)
                .add(sha256, folder_1_id, folder_peer->get_index(), max_seq)
                .finish()
                .apply(*sup);

            auto file_name = std::string_view("some-file");
            auto pr_file = proto::FileInfo();
            proto::set_name(pr_file, file_name);
            proto::set_type(pr_file, proto::FileInfoType::FILE);
            proto::set_sequence(pr_file, max_seq - 1);
            proto::set_size(pr_file, 5);
            proto::set_block_size(pr_file, 5);

            auto &v = proto::get_version(pr_file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);
            proto::set_value(counter, 1);

            auto data = as_owned_bytes("12345");
            auto data_h = utils::sha256_digest(data).value();
            auto &b1 = proto::add_blocks(pr_file);
            proto::set_hash(b1, data_h);
            proto::set_size(b1, data.size());

            builder.make_index(sha256, folder_1_id).add(pr_file, peer_device).finish().apply(*sup);

            target->do_shutdown();
            sup->do_process();

            peer_actor->in_requests = {};
            peer_actor->out_requests = {};

            start_target();
            sup->do_process();

            CHECK(peer_actor->in_requests.size() == 0);
            builder.configure_cluster(sha256).add(sha256, folder_1_id, 0, 0).finish().apply(*sup);
            CHECK(peer_actor->in_requests.size() == 0);
        }
    };
    F(true, 10).run();
}

void test_races() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main(diff_builder_t &) noexcept override {
            auto builder = diff_builder_t(*cluster);
            auto sha256 = peer_device->device_id().get_sha256();

            auto &folder_infos = folder_1->get_folder_infos();
            auto folder_1_id = folder_1->get_id();
            auto folder_my = folder_infos.by_device(*my_device);
            auto folder_peer = folder_infos.by_device(*peer_device);

            auto max_seq = folder_peer->get_max_sequence() + 10;

            builder.configure_cluster(sha256)
                .add(sha256, folder_1_id, folder_peer->get_index(), max_seq)
                .finish()
                .apply(*sup, target.get());

            auto file_name = std::string_view("some-file");
            auto pr_file = proto::FileInfo();
            proto::set_name(pr_file, file_name);
            proto::set_type(pr_file, proto::FileInfoType::FILE);
            proto::set_sequence(pr_file, max_seq - 1);
            proto::set_size(pr_file, 10);
            proto::set_block_size(pr_file, 5);

            auto &v = proto::get_version(pr_file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);
            proto::set_value(counter, 1);

            auto data_1 = as_owned_bytes("12345");
            auto data_1_h = utils::sha256_digest(data_1).value();
            auto &b1 = proto::add_blocks(pr_file);
            proto::set_hash(b1, data_1_h);
            proto::set_size(b1, data_1.size());

            auto data_2 = as_owned_bytes("67890");
            auto data_2_h = utils::sha256_digest(data_2).value();
            auto &b2 = proto::add_blocks(pr_file);
            proto::set_hash(b2, data_2_h);
            proto::set_size(b2, data_2.size());
            proto::set_offset(b2, 5);

            builder.make_index(sha256, folder_1_id).add(pr_file, peer_device).finish().apply(*sup, target.get());

            SECTION("make file externally available before blocks arrive") {
                builder.local_update(folder_1_id, pr_file).apply(*sup);
                peer_actor->push_response(data_1, 0);
                peer_actor->push_response(data_2, 0);
                peer_actor->process_block_requests();
                sup->do_process();
            }
            SECTION("make file externally available before file finishes") {
                cluster->modify_write_requests(10);
                sup->intercept_io(0);
                peer_actor->push_response(data_1, 0);
                peer_actor->push_response(data_2, 0);
                peer_actor->process_block_requests();
                sup->do_process();

                builder.local_update(folder_1_id, pr_file).apply(*sup);

                sup->resume_io(2)->do_process();
            }
            auto file = folder_my->get_file_infos().by_name(file_name);
            REQUIRE(file);
            CHECK(file->is_locally_available());
            CHECK(file->get_size() == 10);
        }
    };
    F(true, 10).run();
};

int _init() {
    REGISTER_TEST_CASE(test_startup, "test_startup", "[net]");
    REGISTER_TEST_CASE(test_overwhelm, "test_overwhelm", "[net]");
    REGISTER_TEST_CASE(test_index_receiving, "test_index_receiving", "[net]");
    REGISTER_TEST_CASE(test_index_sending, "test_index_sending", "[net]");
    REGISTER_TEST_CASE(test_downloading, "test_downloading", "[net]");
    REGISTER_TEST_CASE(test_downloading_errors, "test_downloading_errors", "[net]");
    REGISTER_TEST_CASE(test_download_from_scratch, "test_download_from_scratch", "[net]");
    REGISTER_TEST_CASE(test_download_resuming, "test_download_resuming", "[net]");
    REGISTER_TEST_CASE(test_uniqueness, "test_uniqueness", "[net]");
    REGISTER_TEST_CASE(test_initiate_my_sharing, "test_initiate_my_sharing", "[net]");
    REGISTER_TEST_CASE(test_initiate_peer_sharing, "test_initiate_peer_sharing", "[net]");
    REGISTER_TEST_CASE(test_sending_index_updates, "test_sending_index_updates", "[net]");
    REGISTER_TEST_CASE(test_uploading, "test_uploading", "[net]");
    REGISTER_TEST_CASE(test_overload_uploading, "test_overload_uploading", "[net]");
    REGISTER_TEST_CASE(test_peer_down, "test_peer_down", "[net]");
    REGISTER_TEST_CASE(test_peer_removal, "test_peer_removal", "[net]");
    REGISTER_TEST_CASE(test_conflicts, "test_conflicts", "[net]");
    REGISTER_TEST_CASE(test_download_interrupting, "test_download_interrupting", "[net]");
    REGISTER_TEST_CASE(test_change_folder_type, "test_change_folder_type", "[net]");
    REGISTER_TEST_CASE(test_pausing, "test_pausing", "[net]");
    REGISTER_TEST_CASE(test_races, "test_races", "[net]");
    return 1;
}

static int v = _init();

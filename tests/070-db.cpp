#include "catch.hpp"
#include "test-utils.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/peer/update_folder.h"
#include "model/diff/peer/cluster_remove.h"
#include "model/diff/aggregate.h"
#include "model/misc/version_utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "model/cluster.h"
#include "db/utils.h"
#include "net/db_actor.h"
#include "access.h"
#include <boost/filesystem.hpp>

using namespace syncspirit;
using namespace syncspirit::db;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

namespace fs = boost::filesystem;

namespace {
struct env {};
} // namespace

namespace syncspirit::net {

template <> inline auto &db_actor_t::access<env>() noexcept { return env; }

} // namespace syncspirit::net

namespace {

struct fixture_t {
    using msg_t = net::message::load_cluster_response_t;
    using msg_ptr_t = r::intrusive_ptr_t<msg_t>;

    fixture_t() noexcept : root_path{bfs::unique_path()}, path_quard{root_path} { utils::set_default("trace"); }

    virtual supervisor_t::configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>(
                [&](auto &p) { p.subscribe_actor(r::lambda<msg_t>([&](msg_t &msg) { reply = &msg; })); });
        };
    }

    cluster_ptr_t make_cluster() noexcept {
        auto my_id =
            device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        my_device = device_t::create(my_id, "my-device").value();

        return cluster_ptr_t(new cluster_t(my_device, 1));
    }

    virtual void run() noexcept {
        auto peer_id =
            device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        cluster = make_cluster();

        auto root_path = bfs::unique_path();
        bfs::create_directory(root_path);
        auto root_path_guard = path_guard_t(root_path);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;
        sup->configure_callback = configure();

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        db_actor =
            sup->create_actor<db_actor_t>().cluster(cluster).db_dir(root_path.string()).timeout(timeout).finish();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(db_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        db_addr = db_actor->get_address();
        main();
        reply.reset();

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    r::address_ptr_t db_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    device_ptr_t peer_device;
    device_ptr_t my_device;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<net::db_actor_t> db_actor;
    bfs::path root_path;
    path_guard_t path_quard;
    r::system_context_t ctx;
    msg_ptr_t reply;
};

} // namespace

void test_db_migration() {
    struct F : fixture_t {
        void main() noexcept override {
            auto &db_env = db_actor->access<env>();
            auto txn_opt = db::make_transaction(db::transaction_type_t::RW, db_env);
            REQUIRE(txn_opt);
            auto &txn = txn_opt.value();
            auto load_opt = db::load(db::prefix::device, txn);
            REQUIRE(load_opt);
            auto &values = load_opt.value();
            REQUIRE(values.size() == 1);
        }
    };
    F().run();
}

void test_loading_empty_db() {
    struct F : fixture_t {

        void main() noexcept override {
            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);

            auto diff = reply->payload.res.diff;
            REQUIRE(diff->apply(*cluster));

            auto devices = cluster->get_devices();
            REQUIRE(devices.size() == 1);
            REQUIRE(devices.by_sha256(cluster->get_device()->device_id().get_sha256()));
        }
    };

    F().run();
}

void test_folder_creation() {
    struct F : fixture_t {
        void main() noexcept override {
            db::Folder db_folder;
            db_folder.set_id("1234-5678");
            db_folder.set_label("my-label");
            db_folder.set_path("/my/path");
            auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
            sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();

            auto folder = cluster->get_folders().by_id(db_folder.id());
            REQUIRE(folder);
            REQUIRE(folder->get_folder_infos().by_device(cluster->get_device()));

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

            auto folder_clone = cluster_clone->get_folders().by_id(folder->get_id());
            REQUIRE(folder_clone);
            REQUIRE(folder.get() != folder_clone.get());
            REQUIRE(folder_clone->get_label() == db_folder.label());
            REQUIRE(folder_clone->get_path().string() == db_folder.path());
            REQUIRE(folder_clone->get_folder_infos().size() == 1);
            REQUIRE(folder_clone->get_folder_infos().by_device(cluster->get_device()));
        }
    };

    F().run();
}

void test_peer_updating() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            db::Device db_device;
            db_device.set_cert_name("some-cn");
            db_device.set_name("some_name");
            db_device.set_auto_accept(true);

            auto diff = diff::cluster_diff_ptr_t(new diff::modify::update_peer_t(db_device, sha256));
            sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();

            auto device = cluster->get_devices().by_sha256(sha256);
            REQUIRE(device);
            CHECK(device->get_name() == "some_name");
            CHECK(device->get_cert_name() == "some-cn");

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

            REQUIRE(cluster_clone->get_devices().size() == 2);
            auto device_clone = cluster_clone->get_devices().by_sha256(sha256);
            REQUIRE(device_clone);
            REQUIRE(device.get() != device_clone.get());
            CHECK(device_clone->get_name() == "some_name");
            CHECK(device_clone->get_cert_name() == "some-cn");
        }
    };

    F().run();
}

void test_folder_sharing() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            db::Device db_device;
            db_device.set_cert_name("some-cn");
            db_device.set_name("some_name");
            db_device.set_auto_accept(true);

            auto diffs = diff::aggregate_t::diffs_t{};
            diffs.push_back(new diff::modify::update_peer_t(db_device, sha256));

            db::Folder db_folder;
            db_folder.set_id("1234-5678");
            db_folder.set_label("my-label");
            db_folder.set_path("/my/path");
            diffs.push_back(new diff::modify::create_folder_t(db_folder));

            diffs.push_back(new diff::modify::share_folder_t(sha256, db_folder.id(), 5));

            auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
            sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();
            CHECK(static_cast<r::actor_base_t *>(db_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

            auto peer_device = cluster_clone->get_devices().by_sha256(sha256);
            REQUIRE(peer_device);
            auto folder = cluster_clone->get_folders().by_id(db_folder.id());
            REQUIRE(folder);
            REQUIRE(folder->get_folder_infos().size() == 2);
            auto fi = folder->get_folder_infos().by_device(peer_device);
            REQUIRE(fi);
            CHECK(fi->get_index() == 5);
            CHECK(fi->get_max_sequence() == 0);
        }
    };

    F().run();
}

void test_cluster_update_and_remove() {
    struct F : fixture_t {
        void main() noexcept override {
            using keys_t = diff::peer::cluster_remove_t::keys_t;

            std::uint64_t peer_index{5};
            auto diffs = diff::aggregate_t::diffs_t{};
            db::Folder db_folder;
            db_folder.set_id("1234-5678");
            db_folder.set_label("my-label");
            db_folder.set_path("/my/path");
            diffs.emplace_back(new diff::modify::create_folder_t(db_folder));

            auto peer_id = peer_device->device_id().get_sha256();
            db::Device db_peer;
            db_peer.set_name("some_name");
            diffs.emplace_back(new diff::modify::update_peer_t(db_peer, peer_id));
            diffs.emplace_back(new diff::modify::share_folder_t(peer_id, db_folder.id(), peer_index));

            auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
            sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();

            proto::Index idx;
            idx.set_folder(db_folder.id());
            auto file = idx.add_files();
            file->set_name("a.txt");
            file->set_size(5ul);
            file->set_block_size(5ul);
            file->set_sequence(6ul);
            auto b = file->add_blocks();
            b->set_size(5ul);
            b->set_hash(utils::sha256_digest("12345").value());
            diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx).value();

            sup->send<model::payload::model_update_t>(sup->get_address(), diff, nullptr);
            sup->do_process();

            REQUIRE(cluster->get_blocks().size() == 1);
            auto block = cluster->get_blocks().get(b->hash());
            REQUIRE(block);
            auto peer_folder_info =
                cluster->get_folders().by_id(db_folder.id())->get_folder_infos().by_device(peer_device);
            REQUIRE(peer_folder_info);
            CHECK(peer_folder_info->get_max_sequence() == 6ul);
            REQUIRE(peer_folder_info->get_file_infos().size() == 1);
            auto peer_file = peer_folder_info->get_file_infos().by_name("a.txt");
            REQUIRE(peer_file);

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            REQUIRE(!reply->payload.ee);

            auto cluster_clone = make_cluster();
            {
                REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                REQUIRE(cluster_clone->get_blocks().size() == 1);
                CHECK(cluster_clone->get_blocks().get(b->hash()));
                auto peer_folder_info =
                    cluster_clone->get_folders().by_id(db_folder.id())->get_folder_infos().by_device(peer_device);
                REQUIRE(peer_folder_info);
                REQUIRE(peer_folder_info->get_file_infos().size() == 1);
                REQUIRE(peer_folder_info->get_file_infos().by_name("a.txt"));
            }

            keys_t updated_folders{std::string(db_folder.id())};
            keys_t removed_folder_infos{std::string(peer_folder_info->get_key())};
            keys_t removed_files{std::string(peer_file->get_key())};
            keys_t removed_blocks{std::string(block->get_key())};
            diff = new diff::peer::cluster_remove_t(peer_id, updated_folders, removed_folder_infos, removed_files,
                                                    removed_blocks);
            sup->send<model::payload::model_update_t>(sup->get_address(), diff, nullptr);
            sup->do_process();

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            REQUIRE(!reply->payload.ee);

            cluster_clone = make_cluster();
            {
                REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                REQUIRE(cluster_clone->get_blocks().size() == 0);
                auto &fis = cluster_clone->get_folders().by_id(db_folder.id())->get_folder_infos();
                REQUIRE(fis.size() == 1);
                REQUIRE(!fis.by_device(peer_device));
                REQUIRE(fis.by_device(cluster->get_device()));
            }
        }
    };
    F().run();
}

void test_clone_file() {
    struct F : fixture_t {
        void main() noexcept override {
            db::Folder db_folder;
            db_folder.set_id("some-id");
            db_folder.set_label("some-label");

            auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
            sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();

            uint64_t peer_index = 5;

            auto diffs = diff::aggregate_t::diffs_t{};
            auto peer_id = peer_device->device_id().get_sha256();
            db::Device db_peer;
            db_peer.set_name("some_name");
            diffs.emplace_back(new diff::modify::update_peer_t(db_peer, peer_id));
            diffs.emplace_back(new diff::modify::share_folder_t(peer_id, db_folder.id(), peer_index));

            diff = new diff::aggregate_t(std::move(diffs));
            sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();

            proto::Index idx;
            idx.set_folder(db_folder.id());
            auto file = idx.add_files();
            file->set_name("a.bin");
            file->set_sequence(6ul);
            auto version = file->mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(peer_device->as_uint());

            SECTION("file without blocks") {
                diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx).value();
                sup->send<model::payload::model_update_t>(sup->get_address(), diff, nullptr);
                sup->do_process();

                auto folder = cluster->get_folders().by_id(db_folder.id());
                auto folder_my = folder->get_folder_infos().by_device(my_device);
                auto folder_peer = folder->get_folder_infos().by_device(peer_device);
                auto file_peer = folder_peer->get_file_infos().by_name(file->name());
                REQUIRE(file_peer);
                diff = new diff::modify::clone_file_t(*file_peer);
                sup->send<model::payload::model_update_t>(sup->get_address(), diff, nullptr);
                sup->do_process();

                sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
                sup->do_process();
                REQUIRE(reply);
                REQUIRE(!reply->payload.ee);

                auto cluster_clone = make_cluster();
                {
                    REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                    REQUIRE(cluster_clone->get_blocks().size() == 0);
                    auto &fis = cluster_clone->get_folders().by_id(db_folder.id())->get_folder_infos();
                    REQUIRE(fis.size() == 2);
                    auto folder_info_clone = fis.by_device(cluster_clone->get_device());
                    auto file_clone = folder_info_clone->get_file_infos().by_name(file->name());
                    REQUIRE(file_clone);
                    REQUIRE(file_clone->get_name() == file->name());
                    REQUIRE(file_clone->get_blocks().size() == 0);
                    REQUIRE(file_clone->get_sequence() == 1);
                    REQUIRE(!file_clone->get_source());
                    REQUIRE(folder_info_clone->get_max_sequence() == 1);
                }
            }

            SECTION("file with blocks") {
                file->set_size(5ul);
                file->set_block_size(5ul);
                auto b = file->add_blocks();
                b->set_size(5ul);
                b->set_hash(utils::sha256_digest("12345").value());

                diff = diff::peer::update_folder_t::create(*cluster, *peer_device, idx).value();
                sup->send<model::payload::model_update_t>(sup->get_address(), diff, nullptr);
                sup->do_process();

                auto folder = cluster->get_folders().by_id(db_folder.id());
                auto folder_my = folder->get_folder_infos().by_device(my_device);
                auto folder_peer = folder->get_folder_infos().by_device(peer_device);
                auto file_peer = folder_peer->get_file_infos().by_name(file->name());
                REQUIRE(file_peer);
                diff = new diff::modify::clone_file_t(*file_peer);
                sup->send<model::payload::model_update_t>(sup->get_address(), diff, nullptr);
                sup->do_process();
                REQUIRE(folder_my->get_max_sequence() == 0);

                {
                    sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
                    sup->do_process();
                    REQUIRE(reply);
                    REQUIRE(!reply->payload.ee);

                    auto cluster_clone = make_cluster();
                    REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                    REQUIRE(cluster_clone->get_blocks().size() == 1);
                    auto &fis = cluster_clone->get_folders().by_id(db_folder.id())->get_folder_infos();
                    REQUIRE(fis.size() == 2);
                    auto folder_info_clone = fis.by_device(cluster_clone->get_device());
                    auto file_clone = folder_info_clone->get_file_infos().by_name(file->name());
                    REQUIRE(file_clone);
                    REQUIRE(file_clone->get_name() == file->name());
                    REQUIRE(file_clone->get_blocks().size() == 1);
                    REQUIRE(file_clone->get_sequence() == 0);
                    REQUIRE(folder_info_clone->get_max_sequence() == 0);
                }

                file_peer = folder_peer->get_file_infos().by_name(file->name());
                file_peer->mark_local_available(0);
                REQUIRE(file_peer->is_locally_available());

                auto file_my = folder_my->get_file_infos().by_name(file->name());
                diff = new diff::modify::finish_file_t(*file_my);
                sup->send<model::payload::model_update_t>(sup->get_address(), diff, nullptr);
                sup->do_process();

                {
                    sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
                    sup->do_process();
                    REQUIRE(reply);
                    REQUIRE(!reply->payload.ee);

                    auto cluster_clone = make_cluster();
                    REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                    REQUIRE(cluster_clone->get_blocks().size() == 1);
                    auto &fis = cluster_clone->get_folders().by_id(db_folder.id())->get_folder_infos();
                    REQUIRE(fis.size() == 2);
                    auto folder_info_clone = fis.by_device(cluster_clone->get_device());
                    auto file_clone = folder_info_clone->get_file_infos().by_name(file->name());
                    REQUIRE(file_clone);
                    REQUIRE(file_clone->get_name() == file->name());
                    REQUIRE(file_clone->get_blocks().size() == 1);
                    REQUIRE(file_clone->get_blocks().at(0));
                    REQUIRE(file_clone->get_sequence() == 1);
                    REQUIRE(folder_info_clone->get_max_sequence() == 1);
                }
            }
        }
    };
    F().run();
}

REGISTER_TEST_CASE(test_db_migration, "test_db_migration", "[db]");
REGISTER_TEST_CASE(test_loading_empty_db, "test_loading_empty_db", "[db]");
REGISTER_TEST_CASE(test_folder_creation, "test_folder_creation", "[db]");
REGISTER_TEST_CASE(test_peer_updating, "test_peer_updating", "[db]");
REGISTER_TEST_CASE(test_folder_sharing, "test_folder_sharing", "[db]");
REGISTER_TEST_CASE(test_cluster_update_and_remove, "test_cluster_update_and_remove", "[db]");
REGISTER_TEST_CASE(test_clone_file, "test_clone_file", "[db]");

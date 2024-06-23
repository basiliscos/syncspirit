// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "test-utils.h"
#include "diff-builder.h"
#include "model/diff/peer/cluster_update.h"
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

        return cluster_ptr_t(new cluster_t(my_device, 1, 1));
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

        auto db_config = config::db_config_t{1024 * 1024, 0};
        db_actor = sup->create_actor<db_actor_t>()
                       .cluster(cluster)
                       .db_dir(root_path.string())
                       .db_config(db_config)
                       .timeout(timeout)
                       .finish();
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
            auto folder_id = "1234-5678";

            auto builder = diff_builder_t(*cluster);
            builder.create_folder(folder_id, "/my/path", "my-label").apply(*sup);

            auto folder = cluster->get_folders().by_id(folder_id);
            REQUIRE(folder);
            REQUIRE(folder->get_folder_infos().by_device(*cluster->get_device()));

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

            auto folder_clone = cluster_clone->get_folders().by_id(folder->get_id());
            REQUIRE(folder_clone);
            REQUIRE(folder.get() != folder_clone.get());
            REQUIRE(folder_clone->get_label() == "my-label");
            REQUIRE(folder_clone->get_path().string() == "/my/path");
            REQUIRE(folder_clone->get_folder_infos().size() == 1);
            REQUIRE(folder_clone->get_folder_infos().by_device(*cluster->get_device()));
        }
    };

    F().run();
}

void test_miscellaneous() {
    struct F : fixture_t {
        void main() noexcept override {
            auto d_id1 =
                device_id_t::from_string("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW").value();
            auto d_id2 =
                device_id_t::from_string("XBOWTOU-Y7H6RM6-D7WT3UB-7P2DZ5G-R6GNZG6-T5CCG54-SGVF3U5-LBM7RQB").value();

            db::SomeDevice sd_1;
            sd_1.set_label("x1");
            auto unknown_device = unknown_device_t::create(d_id1, sd_1).value();

            db::SomeDevice sd_2;
            sd_2.set_label("x2");
            auto ignored_device = ignored_device_t::create(d_id2, sd_2).value();

            cluster->get_unknown_devices().put(unknown_device);
            cluster->get_ignored_devices().put(ignored_device);

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

            REQUIRE(cluster_clone->get_unknown_devices().by_sha256(d_id1.get_sha256()));
            REQUIRE(cluster_clone->get_ignored_devices().by_sha256(d_id2.get_sha256()));
        }
    };
    F().run();
}

void test_peer_updating() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(sha256, "some_name", "some-cn", true).apply(*sup);

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
            auto folder_id = "1234-5678";
            auto builder = diff_builder_t(*cluster);
            builder.update_peer(sha256)
                .apply(*sup)
                .create_folder(folder_id, "/my/path")
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, 4)
                .finish()
                .share_folder(sha256, folder_id)
                .apply(*sup);

            CHECK(static_cast<r::actor_base_t *>(db_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

            auto peer_device = cluster_clone->get_devices().by_sha256(sha256);
            REQUIRE(peer_device);
            auto folder = cluster_clone->get_folders().by_id(folder_id);
            REQUIRE(folder);
            REQUIRE(folder->get_folder_infos().size() == 2);
            auto fi = folder->get_folder_infos().by_device(*peer_device);
            REQUIRE(fi);
            CHECK(fi->get_index() == 5);
            CHECK(fi->get_max_sequence() == 4);
        }
    };

    F().run();
}

void test_cluster_update_and_remove() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            auto folder_id = "1234-5678";
            auto unknown_folder_id = "5678-999";

            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_size(5ul);
            file.set_block_size(5ul);
            file.set_sequence(6ul);
            auto b = file.add_blocks();
            b->set_size(5ul);
            b->set_hash(utils::sha256_digest("12345").value());

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(sha256)
                .apply(*sup)
                .create_folder(folder_id, "/my/path")
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, file.sequence())
                .add(sha256, unknown_folder_id, 5, 5)
                .finish()
                .share_folder(sha256, folder_id)
                .apply(*sup)
                .make_index(sha256, folder_id)
                .add(file)
                .finish()
                .apply(*sup);

            REQUIRE(cluster->get_blocks().size() == 1);
            auto block = cluster->get_blocks().get(b->hash());
            REQUIRE(block);

            auto folder = cluster->get_folders().by_id(folder_id);
            auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);

            REQUIRE(peer_folder_info);
            CHECK(peer_folder_info->get_max_sequence() == 6ul);
            REQUIRE(peer_folder_info->get_file_infos().size() == 1);
            auto peer_file = peer_folder_info->get_file_infos().by_name("a.txt");
            REQUIRE(peer_file);

            auto &unknown_folders = cluster->get_unknown_folders();
            CHECK(std::distance(unknown_folders.begin(), unknown_folders.end()) == 1);

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            REQUIRE(!reply->payload.ee);

            auto cluster_clone = make_cluster();
            {
                REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                REQUIRE(cluster_clone->get_blocks().size() == 1);
                CHECK(cluster_clone->get_blocks().get(b->hash()));
                auto folder = cluster_clone->get_folders().by_id(folder_id);
                auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);
                REQUIRE(peer_folder_info);
                REQUIRE(peer_folder_info->get_file_infos().size() == 1);
                REQUIRE(peer_folder_info->get_file_infos().by_name("a.txt"));
                REQUIRE(!cluster_clone->get_unknown_folders().empty());
            }

            auto pr_msg = proto::ClusterConfig();
            auto pr_f = pr_msg.add_folders();
            pr_f->set_id(folder_id);
            auto pr_device = pr_f->add_devices();
            pr_device->set_id(std::string(peer_device->device_id().get_sha256()));
            pr_device->set_max_sequence(1);
            pr_device->set_index_id(peer_folder_info->get_index() + 1);
            auto diff = diff::peer::cluster_update_t::create(*cluster, *peer_device, pr_msg).value();

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
                auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                REQUIRE(fis.size() == 2);
                auto folder_info = fis.by_device(*peer_device);
                REQUIRE(folder_info);
                REQUIRE(fis.by_device(*cluster->get_device()));
                REQUIRE(cluster_clone->get_unknown_folders().empty());
            }
        }
    };
    F().run();
}

void test_unsharing_folder() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            auto folder_id = "1234-5678";

            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_size(5ul);
            file.set_block_size(5ul);
            file.set_sequence(6ul);
            auto b = file.add_blocks();
            b->set_size(5ul);
            b->set_hash(utils::sha256_digest("12345").value());

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(sha256)
                .apply(*sup)
                .create_folder(folder_id, "/my/path")
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, file.sequence())
                .finish()
                .share_folder(sha256, folder_id)
                .apply(*sup)
                .make_index(sha256, folder_id)
                .add(file)
                .finish()
                .apply(*sup);

            REQUIRE(cluster->get_blocks().size() == 1);
            auto block = cluster->get_blocks().get(b->hash());
            REQUIRE(block);

            auto folder = cluster->get_folders().by_id(folder_id);
            auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);

            REQUIRE(peer_folder_info);
            CHECK(peer_folder_info->get_max_sequence() == 6ul);
            REQUIRE(peer_folder_info->get_file_infos().size() == 1);
            auto peer_file = peer_folder_info->get_file_infos().by_name("a.txt");
            REQUIRE(peer_file);

            builder.unshare_folder(*peer_folder_info).apply(*sup);

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            REQUIRE(!reply->payload.ee);

            auto cluster_clone = make_cluster();
            {
                REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                REQUIRE(fis.size() == 1);
                REQUIRE(!fis.by_device(*peer_device));
                REQUIRE(fis.by_device(*cluster->get_device()));
                REQUIRE(cluster_clone->get_blocks().size() == 0);
            }
        }
    };
    F().run();
}

void test_clone_file() {
    struct F : fixture_t {
        void main() noexcept override {

            auto sha256 = peer_device->device_id().get_sha256();
            auto folder_id = "1234-5678";

            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(6ul);
            auto version = file.mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(peer_device->as_uint());

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(sha256)
                .apply(*sup)
                .create_folder(folder_id, "/my/path")
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, file.sequence())
                .finish()
                .share_folder(sha256, folder_id)
                .apply(*sup);

            auto folder = cluster->get_folders().by_id(folder_id);
            auto folder_my = folder->get_folder_infos().by_device(*my_device);
            auto folder_peer = folder->get_folder_infos().by_device(*peer_device);

            SECTION("file without blocks") {
                builder.make_index(sha256, folder_id).add(file).finish().apply(*sup);

                auto file_peer = folder_peer->get_file_infos().by_name(file.name());
                REQUIRE(file_peer);
                builder.clone_file(*file_peer).apply(*sup);

                sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
                sup->do_process();
                REQUIRE(reply);
                REQUIRE(!reply->payload.ee);

                auto cluster_clone = make_cluster();
                {
                    REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                    REQUIRE(cluster_clone->get_blocks().size() == 0);
                    auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                    REQUIRE(fis.size() == 2);
                    auto folder_info_clone = fis.by_device(*cluster_clone->get_device());
                    auto file_clone = folder_info_clone->get_file_infos().by_name(file.name());
                    REQUIRE(file_clone);
                    REQUIRE(file_clone->get_name() == file.name());
                    REQUIRE(file_clone->get_blocks().size() == 0);
                    REQUIRE(file_clone->get_sequence() == 1);
                    REQUIRE(!file_clone->get_source());
                    REQUIRE(folder_info_clone->get_max_sequence() == 1);
                }
            }

            SECTION("file with blocks") {
                file.set_size(5ul);
                file.set_block_size(5ul);
                auto b = file.add_blocks();
                b->set_size(5ul);
                b->set_hash(utils::sha256_digest("12345").value());

                builder.make_index(sha256, folder_id).add(file).finish().apply(*sup);

                auto folder = cluster->get_folders().by_id(folder_id);
                auto folder_my = folder->get_folder_infos().by_device(*my_device);
                auto folder_peer = folder->get_folder_infos().by_device(*peer_device);
                auto file_peer = folder_peer->get_file_infos().by_name(file.name());
                REQUIRE(file_peer);

                builder.clone_file(*file_peer).apply(*sup);
                REQUIRE(folder_my->get_max_sequence() == 0);

                {
                    sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
                    sup->do_process();
                    REQUIRE(reply);
                    REQUIRE(!reply->payload.ee);

                    auto cluster_clone = make_cluster();
                    REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                    REQUIRE(cluster_clone->get_blocks().size() == 1);
                    auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                    REQUIRE(fis.size() == 2);
                    auto folder_info_clone = fis.by_device(*cluster_clone->get_device());
                    auto file_clone = folder_info_clone->get_file_infos().by_name(file.name());
                    REQUIRE(file_clone);
                    REQUIRE(file_clone->get_name() == file.name());
                    REQUIRE(file_clone->get_blocks().size() == 1);
                    REQUIRE(file_clone->get_sequence() == 0);
                    REQUIRE(folder_info_clone->get_max_sequence() == 0);
                }

                file_peer = folder_peer->get_file_infos().by_name(file.name());
                file_peer->mark_local_available(0);
                REQUIRE(file_peer->is_locally_available());

                auto file_my = folder_my->get_file_infos().by_name(file.name());
                builder.finish_file_ack(*file_my).apply(*sup);

                {
                    sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
                    sup->do_process();
                    REQUIRE(reply);
                    REQUIRE(!reply->payload.ee);

                    auto cluster_clone = make_cluster();
                    REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                    REQUIRE(cluster_clone->get_blocks().size() == 1);
                    auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                    REQUIRE(fis.size() == 2);
                    auto folder_info_clone = fis.by_device(*cluster_clone->get_device());
                    auto file_clone = folder_info_clone->get_file_infos().by_name(file.name());
                    REQUIRE(file_clone);
                    REQUIRE(file_clone->get_name() == file.name());
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

void test_local_update() {
    struct F : fixture_t {
        void main() noexcept override {

            auto folder_id = "1234-5678";

            auto pr_file = proto::FileInfo();
            pr_file.set_name("a.txt");
            pr_file.set_size(5ul);

            auto hash = utils::sha256_digest("12345").value();
            auto pr_block = pr_file.add_blocks();
            pr_block->set_weak_hash(12);
            pr_block->set_size(5);
            pr_block->set_hash(hash);

            auto builder = diff_builder_t(*cluster);
            builder.create_folder(folder_id, "/my/path").apply(*sup).local_update(folder_id, pr_file).apply(*sup);

            SECTION("check saved file with new blocks") {
                sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
                sup->do_process();
                REQUIRE(reply);
                REQUIRE(!reply->payload.ee);
                auto cluster_clone = make_cluster();
                REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

                auto folder = cluster_clone->get_folders().by_id(folder_id);
                auto folder_my = folder->get_folder_infos().by_device(*my_device);
                auto file = folder_my->get_file_infos().by_name("a.txt");
                REQUIRE(file);
                CHECK(cluster_clone->get_blocks().size() == 1);
                CHECK(file->get_blocks().size() == 1);
            }

            pr_file.set_deleted(true);
            pr_file.set_size(0);
            pr_file.clear_blocks();
            builder.local_update(folder_id, pr_file).apply(*sup);

            SECTION("check deleted blocks") {
                sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
                sup->do_process();
                REQUIRE(reply);
                REQUIRE(!reply->payload.ee);
                auto cluster_clone = make_cluster();
                REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

                auto folder = cluster_clone->get_folders().by_id(folder_id);
                auto folder_my = folder->get_folder_infos().by_device(*my_device);
                auto file = folder_my->get_file_infos().by_name("a.txt");
                REQUIRE(file);
                CHECK(file->is_deleted());
                CHECK(cluster_clone->get_blocks().size() == 0);
                CHECK(file->get_blocks().size() == 0);
            }
        }
    };
    F().run();
};

void test_peer_going_offline() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(sha256).apply(*sup);

            db::Device db_peer;
            auto peer = cluster->get_devices().by_sha256(sha256);
            REQUIRE(db_peer.last_seen() == 0);
            peer->update_state(device_state_t::online);

            builder.update_state(*peer, {}, device_state_t::offline).apply(*sup);

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);

            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
            auto peer_clone = cluster_clone->get_devices().by_sha256(sha256);
            db::Device db_peer_clone;
            peer_clone->serialize(db_peer_clone);
            CHECK((peer->get_last_seen() - peer_clone->get_last_seen()).total_seconds() < 2);
            CHECK(db_peer_clone.last_seen() != 0);
        }
    };
    F().run();
};

void test_remove_peer() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            auto folder_id = "1234-5678";
            auto unknown_folder_id = "5678-999";

            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_size(5ul);
            file.set_block_size(5ul);
            file.set_sequence(6ul);
            auto b = file.add_blocks();
            b->set_size(5ul);
            b->set_hash(utils::sha256_digest("12345").value());

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(sha256)
                .apply(*sup)
                .create_folder(folder_id, "/my/path")
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, file.sequence())
                .add(sha256, unknown_folder_id, 5, 5)
                .finish()
                .share_folder(sha256, folder_id)
                .apply(*sup)
                .make_index(sha256, folder_id)
                .add(file)
                .finish()
                .apply(*sup);

            REQUIRE(!cluster->get_unknown_folders().empty());

            REQUIRE(cluster->get_blocks().size() == 1);
            auto block = cluster->get_blocks().get(b->hash());
            REQUIRE(block);

            auto folder = cluster->get_folders().by_id(folder_id);
            auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);

            REQUIRE(peer_folder_info);
            CHECK(peer_folder_info->get_max_sequence() == 6ul);
            REQUIRE(peer_folder_info->get_file_infos().size() == 1);
            auto peer_file = peer_folder_info->get_file_infos().by_name("a.txt");
            REQUIRE(peer_file);

            builder.remove_peer(*peer_device).apply(*sup);

            sup->request<net::payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            REQUIRE(!reply->payload.ee);

            auto cluster_clone = make_cluster();
            {
                REQUIRE(reply->payload.res.diff->apply(*cluster_clone));
                CHECK(cluster_clone->get_unknown_folders().empty());
                CHECK(cluster_clone->get_devices().size() == 1);
                REQUIRE(cluster_clone->get_blocks().size() == 0);
            }
        }
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_loading_empty_db, "test_loading_empty_db", "[db]");
    REGISTER_TEST_CASE(test_miscellaneous, "test_miscellaneous", "[db]");
    REGISTER_TEST_CASE(test_folder_creation, "test_folder_creation", "[db]");
    REGISTER_TEST_CASE(test_peer_updating, "test_peer_updating", "[db]");
    REGISTER_TEST_CASE(test_folder_sharing, "test_folder_sharing", "[db]");
    REGISTER_TEST_CASE(test_cluster_update_and_remove, "test_cluster_update_and_remove", "[db]");
    REGISTER_TEST_CASE(test_unsharing_folder, "test_unsharing_folder", "[db]");
    REGISTER_TEST_CASE(test_clone_file, "test_clone_file", "[db]");
    REGISTER_TEST_CASE(test_local_update, "test_local_update", "[db]");
    REGISTER_TEST_CASE(test_peer_going_offline, "test_peer_going_offline", "[db]");
    REGISTER_TEST_CASE(test_remove_peer, "test_remove_peer", "[db]");
    return 1;
}

static int v = _init();

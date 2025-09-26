// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "db/error_code.h"
#include "model/diff/iterative_controller.h"
#include "test-utils.h"
#include "diff-builder.h"
#include "model/diff/load/commit.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/contact/ignored_connected.h"
#include "model/diff/contact/unknown_connected.h"
#include "proto/proto-helpers-impl.hpp"
#include "test_supervisor.h"
#include "access.h"
#include "model/cluster.h"
#include "db/utils.h"
#include "net/db_actor.h"
#include "net/names.h"
#include <filesystem>
#include <thread>

using namespace syncspirit;
using namespace syncspirit::db;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

namespace fs = std::filesystem;

namespace {
struct env {};
} // namespace

namespace syncspirit::net {

template <> inline auto &db_actor_t::access<env>() noexcept { return env; }

} // namespace syncspirit::net

namespace {

struct db_supervisor_t : supervisor_t {
    using parent_t = supervisor_t;
    using parent_t::parent_t;

    void on_package(bouncer::message::package_t &msg) noexcept {
        LOG_TRACE(log, "on package");
        ++packages;
        put(std::move(msg.payload));
    }

    int packages = 0;
};

struct fixture_t {
    using load_cluster_success_t = net::message::load_cluster_success_t;
    using load_cluster_fail_t = net::message::load_cluster_fail_t;
    using stats_msg_t = net::message::db_info_response_t;
    using stats_msg_ptr_t = r::intrusive_ptr_t<stats_msg_t>;
    using supervisor_ptr_t = r::intrusive_ptr_t<db_supervisor_t>;

    fixture_t() noexcept : root_path{unique_path()}, path_quard{root_path} {
        test::init_logging();
        bfs::create_directory(root_path);
    }
    fixture_t(fixture_t &source) = delete;

    fixture_t(fixture_t &&source) noexcept
        : root_path(std::move(source.root_path)), path_quard(std::move(source.path_quard)) {}

    virtual supervisor_t::configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<load_cluster_success_t>([&](load_cluster_success_t &msg) {
                    load_diff = msg.payload.diff;
                    spdlog::info("received load cluster diff");
                    sup->send<model::payload::model_update_t>(db_addr, load_diff, nullptr);
                }));
                p.subscribe_actor(r::lambda<load_cluster_fail_t>([&](load_cluster_fail_t &msg) {
                    ee = msg.payload.ee;
                    spdlog::warn("fail loading cluster: {}", ee->message({}));
                }));
                p.subscribe_actor(r::lambda<stats_msg_t>([&](stats_msg_t &msg) { stats_reply = &msg; }));
            });
        };
    }

    cluster_ptr_t make_cluster(bool add_peer = true) noexcept {
        auto my_id = "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD";
        auto my_device_id = device_id_t::from_string(my_id).value();
        my_device = device_t::create(my_device_id, "my-device").value();

        auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
        cluster->get_devices().put(my_device);

        auto peer_id = "VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ";
        auto peer_device_id = device_id_t::from_string(peer_id).value();
        peer_device = device_t::create(peer_device_id, "peer-device").value();

        if (add_peer) {
            cluster->get_devices().put(peer_device);
        }
        return cluster;
    }

    virtual config::db_config_t make_config() noexcept { return config::db_config_t{1024 * 1024, 0, 64, 32}; }

    virtual supervisor_ptr_t make_supervisor(r::system_context_t &ctx) noexcept {
        return ctx.create_supervisor<db_supervisor_t>().timeout(timeout).create_registry().finish();
    }

    virtual fixture_t &run() noexcept {
        cluster = make_cluster();

        r::system_context_t ctx;
        sup = make_supervisor(ctx);
        sup->cluster = cluster;
        sup->configure_callback = configure();

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        launch_db();
        main();
        load_diff.reset();
        CHECK(get_reading_txn() == 0);

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        return *this;
    }

    int get_reading_txn() {
        int counter = 0;
        auto state = static_cast<r::actor_base_t *>(db_actor.get())->access<to::state>();
        if (state != r::state_t::SHUT_DOWN) {
            auto &db_env = db_actor->access<env>();
            auto reader = [](void *ctx, int, int, mdbx_pid_t, mdbx_tid_t, uint64_t, uint64_t, size_t,
                             size_t) noexcept -> int {
                ++(*reinterpret_cast<int *>(ctx));
                return 0;
            };
            auto r = mdbx_reader_list(db_env, reader, &counter);
            CHECK(((r == MDBX_RESULT_TRUE) || (r == 0)));
            if (r > 0) {
                auto ec = db::make_error_code(r);
                auto log = utils::get_logger("");
                LOG_INFO(log, "mdbx_reader_list, error: {}", ec.message());
            }
        }

        return counter;
    }

    virtual void launch_db() {
        db_actor = sup->create_actor<db_actor_t>()
                       .bouncer_address(sup->get_address())
                       .cluster(cluster)
                       .db_dir(root_path)
                       .db_config(make_config())
                       .timeout(timeout)
                       .finish();
        db_addr = db_actor->get_address();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(db_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
    }

    virtual void main() noexcept {}

    r::address_ptr_t db_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    device_ptr_t peer_device;
    device_ptr_t my_device;
    supervisor_ptr_t sup;
    r::intrusive_ptr_t<net::db_actor_t> db_actor;
    bfs::path root_path;
    test::path_guard_t path_quard;
    r::system_context_t ctx;
    model::diff::cluster_diff_ptr_t load_diff;
    r::extended_error_ptr_t ee;
    stats_msg_ptr_t stats_reply;
};

} // namespace

void test_db_population() {
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
            CHECK(get_reading_txn() == 1);

            REQUIRE(load_diff);
            REQUIRE(load_diff->apply(*sup, {}));

            sup->do_process();
            load_diff.reset();
            CHECK(get_reading_txn() == 0);

            auto devices = cluster->get_devices();
            REQUIRE(devices.size() == 2);
            REQUIRE(devices.by_sha256(my_device->device_id().get_sha256()));
            REQUIRE(devices.by_sha256(peer_device->device_id().get_sha256()));

            sup->request<net::payload::db_info_request_t>(sup->get_address()).send(timeout);
            sup->do_process();
            REQUIRE(stats_reply);
            auto &stats = stats_reply->payload.res;
            CHECK(stats.entries > 0);
            CHECK(stats.page_size > 0);
        }
    };

    F().run();
}

void test_forget_to_commit_other_thread() {
    struct F : fixture_t {
        void main() noexcept override {
            using namespace model::diff;

            struct V : diff::cluster_visitor_t {
                MDBX_txn *txn;
                outcome::result<void> operator()(const load::commit_t &diff, void *custom) noexcept {
                    auto msg = static_cast<db_actor_t::commit_message_t *>(diff.commit_message.get());
                    txn = msg->payload.txn.txn;
                    return outcome::success();
                }
            };

            CHECK(get_reading_txn() == 1);

            REQUIRE(load_diff);
            auto visitor = V();
            REQUIRE(load_diff->visit(visitor, {}));
            REQUIRE(visitor.txn);

            auto diff = std::move(load_diff);
            std::thread([diff = std::move(diff)]() mutable { diff.reset(); }).join();
            CHECK(get_reading_txn() == 1);
            mdbx_txn_commit(visitor.txn);
            sup->do_process();
        }
    };
    F().run();
}

void test_forget_to_commit_own_thread() {
    struct F : fixture_t {
        void main() noexcept override {
            CHECK(get_reading_txn() == 1);

            load_diff.reset();
            CHECK(get_reading_txn() == 0);
            sup->do_process();
        }
    };
    F().run();
}

void test_folder_upserting() {
    struct F : fixture_t {
        void main() noexcept override {
            auto folder_id = "1234-5678";

            auto builder = diff_builder_t(*cluster);
            builder.upsert_folder(folder_id, "/my/path", "my-label").apply(*sup);

            auto folder = cluster->get_folders().by_id(folder_id);
            REQUIRE(folder);
            REQUIRE(folder->get_folder_infos().by_device(*cluster->get_device()));

            {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));

                auto folder_clone = cluster_clone->get_folders().by_id(folder->get_id());
                REQUIRE(folder_clone);
                REQUIRE(folder.get() != folder_clone.get());
                REQUIRE(folder_clone->get_label() == "my-label");
                REQUIRE(folder_clone->get_path().string() == "/my/path");
                REQUIRE(folder_clone->get_folder_infos().size() == 1);
                REQUIRE(folder_clone->get_folder_infos().by_device(*cluster->get_device()));
            }

            SECTION("upserting") {
                builder.upsert_folder(folder_id, "/my/path", "my-label-2").apply(*sup);
                REQUIRE(folder->get_label() == "my-label-2");

                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);

                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                auto folder_clone = cluster_clone->get_folders().by_id(folder->get_id());
                REQUIRE(folder_clone->get_label() == "my-label-2");
            }
        }
    };

    F().run();
}

void test_unknown_and_ignored_devices_1() {
    struct F : fixture_t {
        void main() noexcept override {
            auto d_id1 =
                device_id_t::from_string("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW").value();
            auto d_id2 =
                device_id_t::from_string("XBOWTOU-Y7H6RM6-D7WT3UB-7P2DZ5G-R6GNZG6-T5CCG54-SGVF3U5-LBM7RQB").value();

            db::SomeDevice sd_1;
            db::set_name(sd_1, "x1");
            auto unknown_device = pending_device_t::create(d_id1, sd_1).value();

            db::SomeDevice sd_2;
            db::set_name(sd_2, "x2");
            auto ignored_device = ignored_device_t::create(d_id2, sd_2).value();

            auto builder = diff_builder_t(*cluster);
            builder.add_unknown_device(d_id1, sd_1).add_ignored_device(d_id2, sd_2).apply(*sup);

            REQUIRE(cluster->get_pending_devices().size() == 1);
            REQUIRE(cluster->get_ignored_devices().size() == 1);

            {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                CHECK(cluster_clone->get_pending_devices().by_sha256(d_id1.get_sha256()));
                CHECK(cluster_clone->get_ignored_devices().by_sha256(d_id2.get_sha256()));
            }

            db::set_name(sd_1, "x1_2");
            db::set_name(sd_2, "x2_2");
            auto diff = model::diff::cluster_diff_ptr_t{};
            diff = new model::diff::contact::unknown_connected_t(*cluster, d_id1, sd_1);
            sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            diff = new model::diff::contact::ignored_connected_t(*cluster, d_id2, sd_2);
            sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();

            {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                REQUIRE(cluster_clone->get_pending_devices().size() == 1);
                REQUIRE(cluster_clone->get_ignored_devices().size() == 1);
                auto unknown = cluster_clone->get_pending_devices().by_sha256(d_id1.get_sha256());
                auto ignored = cluster_clone->get_ignored_devices().by_sha256(d_id2.get_sha256());
                REQUIRE(unknown);
                REQUIRE(ignored);
                CHECK(unknown->get_name() == "x1_2");
                CHECK(ignored->get_name() == "x2_2");
            }

            builder.remove_unknown_device(*unknown_device).remove_ignored_device(*ignored_device).apply(*sup);
            REQUIRE(cluster->get_pending_devices().size() == 0);
            REQUIRE(cluster->get_ignored_devices().size() == 0);

            {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                REQUIRE(cluster_clone->get_pending_devices().size() == 0);
                REQUIRE(cluster_clone->get_ignored_devices().size() == 0);
            }
        }
    };
    F().run();
}

void test_unknown_and_ignored_devices_2() {
    struct F : fixture_t {
        void main() noexcept override {
            auto d_id =
                device_id_t::from_string("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW").value();

            db::SomeDevice sd;
            db::set_name(sd, "x1");
            auto builder = diff_builder_t(*cluster);

            builder.add_unknown_device(d_id, sd).apply(*sup);
            {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                CHECK(cluster_clone->get_pending_devices().by_sha256(d_id.get_sha256()));
                CHECK(!cluster_clone->get_ignored_devices().by_sha256(d_id.get_sha256()));
            }

            builder.add_ignored_device(d_id, sd).apply(*sup);
            {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                CHECK(!cluster_clone->get_pending_devices().by_sha256(d_id.get_sha256()));
                CHECK(cluster_clone->get_ignored_devices().by_sha256(d_id.get_sha256()));
            }
        }
    };
    F().run();
}

void test_peer_updating() {
    struct F : fixture_t {
        void main() noexcept override {

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(peer_device->device_id(), "some_name", "some-cn", true).apply(*sup);

            auto sha256 = peer_device->device_id().get_sha256();
            auto device = cluster->get_devices().by_sha256(sha256);
            REQUIRE(device);
            CHECK(device->get_name() == "some_name");
            CHECK(device->get_cert_name() == "some-cn");

            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);
            auto cluster_clone = make_cluster();
            auto controller = make_apply_controller(cluster_clone);
            REQUIRE(load_diff->apply(*controller, {}));

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
            builder.update_peer(peer_device->device_id())
                .apply(*sup)
                .upsert_folder(folder_id, "/my/path")
                .apply(*sup)
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, 4)
                .finish()
                .apply(*sup);
            REQUIRE(cluster->get_pending_folders().size() == 1);

            builder.share_folder(sha256, folder_id).apply(*sup);
            REQUIRE(cluster->get_pending_folders().size() == 0);

            CHECK(static_cast<r::actor_base_t *>(db_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);
            auto cluster_clone = make_cluster();
            auto controller = make_apply_controller(cluster_clone);
            REQUIRE(load_diff->apply(*controller, {}));

            auto peer_device = cluster_clone->get_devices().by_sha256(sha256);
            REQUIRE(peer_device);
            auto folder = cluster_clone->get_folders().by_id(folder_id);
            REQUIRE(folder);
            REQUIRE(folder->get_folder_infos().size() == 2);
            auto fi = folder->get_folder_infos().by_device(*peer_device);
            REQUIRE(fi);
            CHECK(fi->get_index() == 5);
            CHECK(fi->get_max_sequence() == 0);
            REQUIRE(cluster_clone->get_pending_folders().size() == 0);
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
            proto::set_name(file, "a.txt");
            proto::set_size(file, 5ul);
            proto::set_block_size(file, 5ul);
            proto::set_sequence(file, 6ul);

            auto b_hash = utils::sha256_digest(as_bytes("12345")).value();
            auto &b = proto::add_blocks(file);
            proto::set_size(b, 5ul);
            proto::set_hash(b, b_hash);

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(peer_device->device_id())
                .apply(*sup)
                .upsert_folder(folder_id, "/my/path")
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, proto::get_sequence(file))
                .add(sha256, unknown_folder_id, 5, 5)
                .finish()
                .apply(*sup)
                .share_folder(sha256, folder_id)
                .apply(*sup)
                .make_index(sha256, folder_id)
                .add(file, peer_device)
                .finish()
                .apply(*sup);

            REQUIRE(cluster->get_blocks().size() == 1);
            auto block = cluster->get_blocks().by_hash(b_hash);
            REQUIRE(block);

            auto folder = cluster->get_folders().by_id(folder_id);
            auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);

            REQUIRE(peer_folder_info);
            CHECK(peer_folder_info->get_max_sequence() == 6ul);
            REQUIRE(peer_folder_info->get_file_infos().size() == 1);
            auto peer_file = peer_folder_info->get_file_infos().by_name("a.txt");
            REQUIRE(peer_file);

            auto &unknown_folders = cluster->get_pending_folders();
            CHECK(std::distance(unknown_folders.begin(), unknown_folders.end()) == 1);

            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);
            {
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                REQUIRE(cluster_clone->get_blocks().size() == 1);
                CHECK(cluster_clone->get_blocks().by_hash(b_hash));
                auto folder = cluster_clone->get_folders().by_id(folder_id);
                auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);
                REQUIRE(peer_folder_info);
                REQUIRE(peer_folder_info->get_file_infos().size() == 1);
                REQUIRE(peer_folder_info->get_file_infos().by_name("a.txt"));
                REQUIRE(cluster_clone->get_pending_folders().size() == 1);
            }

            auto pr_msg = proto::ClusterConfig();
            auto &pr_f = proto::add_folders(pr_msg);
            proto::set_id(pr_f, folder_id);
            auto &pr_device = proto::add_devices(pr_f);
            proto::set_id(pr_device, peer_device->device_id().get_sha256());
            proto::set_max_sequence(pr_device, 1);
            proto::set_index_id(pr_device, peer_folder_info->get_index() + 1);
            auto diff =
                diff::peer::cluster_update_t::create({}, *cluster, *sup->sequencer, *peer_device, pr_msg).value();

            sup->send<model::payload::model_update_t>(sup->get_address(), diff, nullptr);
            sup->do_process();

            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);
            {
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                REQUIRE(cluster_clone->get_blocks().size() == 0);
                auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                REQUIRE(fis.size() == 2);
                auto folder_info = fis.by_device(*peer_device);
                REQUIRE(folder_info);
                REQUIRE(fis.by_device(*cluster->get_device()));
                REQUIRE(cluster_clone->get_pending_folders().size() == 0);
            }
        }
    };
    F().run();
}

void test_unshare_and_remove_folder() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            auto folder_id = "1234-5678";

            auto file = proto::FileInfo();
            proto::set_name(file, "a.txt");
            proto::set_size(file, 5ul);
            proto::set_block_size(file, 5ul);
            proto::set_sequence(file, 6ul);

            auto b_hash = utils::sha256_digest(as_bytes("12345")).value();
            auto &b = proto::add_blocks(file);
            proto::set_size(b, 5ul);
            proto::set_hash(b, b_hash);

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(peer_device->device_id())
                .apply(*sup)
                .upsert_folder(folder_id, "/my/path")
                .apply(*sup)
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, proto::get_sequence(file))
                .finish()
                .share_folder(sha256, folder_id)
                .apply(*sup)
                .make_index(sha256, folder_id)
                .add(file, peer_device)
                .finish()
                .apply(*sup);

            REQUIRE(cluster->get_blocks().size() == 1);
            auto block = cluster->get_blocks().by_hash(b_hash);
            REQUIRE(block);

            auto folder = cluster->get_folders().by_id(folder_id);
            auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);

            REQUIRE(peer_folder_info);
            CHECK(peer_folder_info->get_max_sequence() == 6ul);
            REQUIRE(peer_folder_info->get_file_infos().size() == 1);
            auto peer_file = peer_folder_info->get_file_infos().by_name("a.txt");
            REQUIRE(peer_file);

            SECTION("unshare") {
                builder.unshare_folder(*peer_folder_info).apply(*sup);

                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);

                {
                    auto cluster_clone = make_cluster();
                    auto controller = make_apply_controller(cluster_clone);
                    REQUIRE(load_diff->apply(*controller, {}));
                    auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                    REQUIRE(fis.size() == 1);
                    REQUIRE(!fis.by_device(*peer_device));
                    REQUIRE(fis.by_device(*cluster->get_device()));
                    REQUIRE(cluster_clone->get_blocks().size() == 0);
                }
            }

            SECTION("remove") {
                builder.remove_folder(*folder).apply(*sup);

                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);

                {
                    auto cluster_clone = make_cluster();
                    auto controller = make_apply_controller(cluster_clone);
                    REQUIRE(load_diff->apply(*controller, {}));
                    REQUIRE(cluster_clone->get_folders().size() == 0);
                    REQUIRE(cluster_clone->get_blocks().size() == 0);
                }
            }
        }
    };
    F().run();
}

void test_remote_copy() {
    struct F : fixture_t {
        void main() noexcept override {

            auto sha256 = peer_device->device_id().get_sha256();
            auto folder_id = "1234-5678";

            auto file = proto::FileInfo();
            proto::set_name(file, "a.txt");
            proto::set_sequence(file, 6ul);

            auto &v = proto::get_version(file);
            proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 1));

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(peer_device->device_id())
                .apply(*sup)
                .upsert_folder(folder_id, "/my/path")
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, proto::get_sequence(file))
                .finish()
                .apply(*sup)
                .share_folder(sha256, folder_id)
                .apply(*sup);

            auto folder = cluster->get_folders().by_id(folder_id);
            auto folder_my = folder->get_folder_infos().by_device(*my_device);
            auto folder_peer = folder->get_folder_infos().by_device(*peer_device);

            SECTION("file without blocks") {
                builder.make_index(sha256, folder_id).add(file, peer_device).finish().apply(*sup);

                auto file_peer = folder_peer->get_file_infos().by_name(proto::get_name(file));
                REQUIRE(file_peer);
                builder.remote_copy(*file_peer, *folder_peer).apply(*sup);

                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                {
                    auto cluster_clone = make_cluster();
                    auto controller = make_apply_controller(cluster_clone);
                    REQUIRE(load_diff->apply(*controller, {}));
                    REQUIRE(cluster_clone->get_blocks().size() == 0);
                    auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                    REQUIRE(fis.size() == 2);
                    auto folder_info_clone = fis.by_device(*cluster_clone->get_device());
                    auto file_clone = folder_info_clone->get_file_infos().by_name(proto::get_name(file));
                    REQUIRE(file_clone);
                    REQUIRE(file_clone->get_name()->get_full_name() == proto::get_name(file));
                    REQUIRE(file_clone->iterate_blocks().get_total() == 0);
                    REQUIRE(file_clone->get_sequence() == 1);
                    REQUIRE(folder_info_clone->get_max_sequence() == 1);
                }
            }

            SECTION("file with blocks") {
                proto::set_size(file, 5ul);
                proto::set_block_size(file, 5ul);

                auto b_hash = utils::sha256_digest(as_bytes("12345")).value();
                auto &b = proto::add_blocks(file);
                proto::set_size(b, 5ul);
                proto::set_hash(b, b_hash);

                builder.make_index(sha256, folder_id).add(file, peer_device).finish().apply(*sup);

                auto folder = cluster->get_folders().by_id(folder_id);
                auto folder_my = folder->get_folder_infos().by_device(*my_device);
                auto folder_peer = folder->get_folder_infos().by_device(*peer_device);
                auto file_peer = folder_peer->get_file_infos().by_name(proto::get_name(file));
                REQUIRE(file_peer);

                builder.remote_copy(*file_peer, *folder_peer).apply(*sup);
                REQUIRE(folder_my->get_max_sequence() == 1);

                {
                    load_diff = {};
                    sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                    sup->do_process();
                    REQUIRE(load_diff);

                    auto cluster_clone = make_cluster();
                    auto controller = make_apply_controller(cluster_clone);
                    REQUIRE(load_diff->apply(*controller, {}));
                    REQUIRE(cluster_clone->get_blocks().size() == 1);
                    auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                    REQUIRE(fis.size() == 2);
                    auto folder_info_clone = fis.by_device(*cluster_clone->get_device());
                    auto file_clone = folder_info_clone->get_file_infos().by_name(proto::get_name(file));
                    REQUIRE(file_clone);
                    REQUIRE(file_clone->get_name()->get_full_name() == proto::get_name(file));
                    REQUIRE(file_clone->iterate_blocks().get_total() == 1);
                    REQUIRE(file_clone->get_sequence() == 1);
                    REQUIRE(folder_info_clone->get_max_sequence() == 1);
                }

                file_peer = folder_peer->get_file_infos().by_name(proto::get_name(file));
                file_peer->mark_local_available(0);
                REQUIRE(file_peer->is_locally_available());

                builder.remote_copy(*file_peer, *folder_peer).apply(*sup);

                {
                    load_diff = {};
                    sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                    sup->do_process();
                    REQUIRE(load_diff);

                    auto cluster_clone = make_cluster();
                    auto controller = make_apply_controller(cluster_clone);
                    REQUIRE(load_diff->apply(*controller, {}));
                    REQUIRE(cluster_clone->get_blocks().size() == 1);
                    auto &fis = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                    REQUIRE(fis.size() == 2);
                    auto folder_info_clone = fis.by_device(*cluster_clone->get_device());
                    auto file_clone = folder_info_clone->get_file_infos().by_name(proto::get_name(file));
                    REQUIRE(file_clone);
                    REQUIRE(file_clone->get_name()->get_full_name() == proto::get_name(file));
                    REQUIRE(file_clone->iterate_blocks().get_total() == 1);
                    REQUIRE(file_clone->iterate_blocks().next());
                    REQUIRE(file_clone->get_sequence() == 2);
                    REQUIRE(folder_info_clone->get_max_sequence() == 2);
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
            proto::set_name(pr_file, "a.txt");
            proto::set_size(pr_file, 5ul);

            auto hash = utils::sha256_digest(as_bytes("12345")).value();
            auto &pr_block = proto::add_blocks(pr_file);
            proto::set_size(pr_block, 5ul);
            proto::set_hash(pr_block, hash);

            auto builder = diff_builder_t(*cluster);
            builder.upsert_folder(folder_id, "/my/path").apply(*sup).local_update(folder_id, pr_file).apply(*sup);

            SECTION("check saved file with new blocks") {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));

                auto folder = cluster_clone->get_folders().by_id(folder_id);
                auto folder_my = folder->get_folder_infos().by_device(*my_device);
                auto file = folder_my->get_file_infos().by_name("a.txt");
                REQUIRE(file);
                CHECK(cluster_clone->get_blocks().size() == 1);
                CHECK(file->iterate_blocks().get_total() == 1);
            }

            proto::set_deleted(pr_file, true);
            proto::set_size(pr_file, 0);
            proto::clear_blocks(pr_file);
            builder.local_update(folder_id, pr_file).apply(*sup);

            SECTION("check deleted blocks") {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));

                auto folder = cluster_clone->get_folders().by_id(folder_id);
                auto folder_my = folder->get_folder_infos().by_device(*my_device);
                auto file = folder_my->get_file_infos().by_name("a.txt");
                REQUIRE(file);
                CHECK(file->is_deleted());
                CHECK(cluster_clone->get_blocks().size() == 0);
                CHECK(file->iterate_blocks().get_total() == 0);
            }
        }
    };
    F().run();
};

void test_peer_going_offline() {
    struct F : fixture_t {
        void main() noexcept override {
            auto builder = diff_builder_t(*cluster);
            builder.update_peer(peer_device->device_id()).apply(*sup);

            auto sha256 = peer_device->device_id().get_sha256();
            db::Device db_peer;
            auto peer = cluster->get_devices().by_sha256(sha256);
            REQUIRE(db::get_last_seen(db_peer) == 0);
            peer->update_state(peer->get_state().connecting().connected().online("tcp://1.1.1.1:1"));

            builder.update_state(*peer, {}, peer->get_state().offline()).apply(*sup);

            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);

            auto cluster_clone = make_cluster();
            auto controller = make_apply_controller(cluster_clone);
            REQUIRE(load_diff->apply(*controller, {}));
            auto peer_clone = cluster_clone->get_devices().by_sha256(sha256);
            db::Device db_peer_clone;
            peer_clone->serialize(db_peer_clone);
            CHECK((peer->get_last_seen() - peer_clone->get_last_seen()).total_seconds() < 2);
            REQUIRE(db::get_last_seen(db_peer_clone) != 0);
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
            proto::set_name(file, "a.txt");
            proto::set_size(file, 5ul);
            proto::set_block_size(file, 5ul);
            proto::set_sequence(file, 6ul);

            auto b_hash = utils::sha256_digest(as_bytes("12345")).value();
            auto &b = proto::add_blocks(file);
            proto::set_size(b, 5ul);
            proto::set_hash(b, b_hash);

            auto builder = diff_builder_t(*cluster);
            builder.update_peer(peer_device->device_id())
                .apply(*sup)
                .upsert_folder(folder_id, "/my/path")
                .configure_cluster(sha256)
                .add(sha256, folder_id, 5, proto::get_sequence(file))
                .add(sha256, unknown_folder_id, 5, 5)
                .finish()
                .apply(*sup)
                .share_folder(sha256, folder_id)
                .apply(*sup)
                .make_index(sha256, folder_id)
                .add(file, peer_device)
                .finish()
                .apply(*sup);

            CHECK(cluster->get_pending_folders().size() == 1);

            REQUIRE(cluster->get_blocks().size() == 1);
            auto block = cluster->get_blocks().by_hash(b_hash);
            REQUIRE(block);

            auto folder = cluster->get_folders().by_id(folder_id);
            auto peer_folder_info = folder->get_folder_infos().by_device(*peer_device);

            REQUIRE(peer_folder_info);
            CHECK(peer_folder_info->get_max_sequence() == 6ul);
            REQUIRE(peer_folder_info->get_file_infos().size() == 1);
            auto peer_file = peer_folder_info->get_file_infos().by_name("a.txt");
            REQUIRE(peer_file);

            builder.remove_peer(*peer_device).apply(*sup);

            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);
            {
                auto cluster_clone = make_cluster(false);
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                CHECK(cluster_clone->get_pending_folders().size() == 0);
                CHECK(cluster_clone->get_devices().size() == 1);
                REQUIRE(cluster_clone->get_blocks().size() == 0);
            }
        }
    };
    F().run();
}

void test_update_peer() {
    struct F : fixture_t {
        void main() noexcept override {
            auto builder = diff_builder_t(*cluster);

            db::SomeDevice db;
            db::set_name(db, "x1");

            SECTION("unknown device is removed") {
                builder.add_unknown_device(peer_device->device_id(), db)
                    .apply(*sup)
                    .update_peer(peer_device->device_id(), "p1")
                    .apply(*sup);
            }

            SECTION("ignored device is removed") {
                builder.add_ignored_device(peer_device->device_id(), db)
                    .apply(*sup)
                    .update_peer(peer_device->device_id(), "p1")
                    .apply(*sup);
            }

            {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));
                CHECK(cluster_clone->get_pending_devices().size() == 0);
                CHECK(cluster_clone->get_ignored_devices().size() == 0);
                CHECK(cluster_clone->get_devices().size() == 2);
            }
        }
    };
    F().run();
}

void test_peer_3_folders_6_files() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            auto f1_id = "123";
            auto f2_id = "356";
            auto f3_id = "789";

            auto next_sequence = 6ul;

            auto make_file = [&](std::string name) {
                auto file = proto::FileInfo();
                proto::set_name(file, name);
                proto::set_sequence(file, ++next_sequence);
                auto &v = proto::get_version(file);
                proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 1));
                return file;
            };

            auto builder = diff_builder_t(*cluster);

            // clang-format off
            builder
                .update_peer(peer_device->device_id())
                .apply(*sup)
                .upsert_folder(f1_id, "/my/path1", "my-label1")
                .upsert_folder(f2_id, "/my/path2", "my-label2")
                .upsert_folder(f3_id, "/my/path3", "my-label3")
                .configure_cluster(sha256)
                    .add(sha256, f1_id, 10, 5)
                    .add(sha256, f2_id, 11, 5)
                    .add(sha256, f3_id, 12, 5)
                .finish()
                .apply(*sup)
                    .share_folder(sha256, f1_id)
                    .share_folder(sha256, f2_id)
                    .share_folder(sha256, f3_id)
                .apply(*sup)
                    .make_index(sha256, f1_id).add(make_file("f1.1"), peer_device).add(make_file("f1.2"), peer_device).finish()
                    .make_index(sha256, f2_id).add(make_file("f2.1"), peer_device).add(make_file("f2.2"), peer_device).finish()
                    .make_index(sha256, f3_id).add(make_file("f3.1"), peer_device).add(make_file("f3.2"), peer_device).finish()
                .apply(*sup);
            // clang-format on

            {
                auto get_peer_file =
                    [&](std::string_view folder_id,
                        std::string_view name) -> std::pair<model::file_info_ptr_t, model::folder_info_t *> {
                    auto folder = cluster->get_folders().by_id(folder_id);
                    auto folder_info = folder->get_folder_infos().by_device(*peer_device);
                    auto file = folder_info->get_file_infos().by_name(name);
                    return {file, folder_info.get()};
                };

                auto [file_11, fi_11] = get_peer_file(f1_id, "f1.1");
                auto [file_12, fi_12] = get_peer_file(f1_id, "f1.2");
                auto [file_21, fi_21] = get_peer_file(f2_id, "f2.1");
                auto [file_22, fi_22] = get_peer_file(f2_id, "f2.2");
                auto [file_31, fi_31] = get_peer_file(f3_id, "f3.1");
                auto [file_32, fi_32] = get_peer_file(f3_id, "f3.2");

                REQUIRE(file_11);
                REQUIRE(file_12);
                REQUIRE(file_21);
                REQUIRE(file_22);
                REQUIRE(file_31);
                REQUIRE(file_32);

                // clang-format off
                builder
                    .remote_copy(*file_11, *fi_11).remote_copy(*file_12, *fi_12)
                    .remote_copy(*file_21, *fi_21).remote_copy(*file_22, *fi_22)
                    .remote_copy(*file_31, *fi_31).remote_copy(*file_32, *fi_32)
                .apply(*sup);
                // clang-format on
            }

            {
                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));

                auto get_my_file = [&](std::string_view folder_id, std::string_view name) {
                    auto folder = cluster->get_folders().by_id(folder_id);
                    auto folder_info = folder->get_folder_infos().by_device(*my_device);
                    auto file = folder_info->get_file_infos().by_name(name);
                    return file;
                };

                auto file_11 = get_my_file(f1_id, "f1.1");
                auto file_12 = get_my_file(f1_id, "f1.2");
                auto file_21 = get_my_file(f2_id, "f2.1");
                auto file_22 = get_my_file(f2_id, "f2.2");
                auto file_31 = get_my_file(f3_id, "f3.1");
                auto file_32 = get_my_file(f3_id, "f3.2");

                REQUIRE(file_11);
                REQUIRE(file_12);
                REQUIRE(file_21);
                REQUIRE(file_22);
                REQUIRE(file_31);
                REQUIRE(file_32);
            }
        }
    };
    F().run();
}

void test_db_migration_1_2() {
    static constexpr auto folder_id = "1234-5678";

    struct F1 : fixture_t {
        void main() noexcept override {

            auto sha256 = peer_device->device_id().get_sha256();
            auto builder = diff_builder_t(*cluster);
            builder.update_peer(peer_device->device_id())
                .apply(*sup)
                .upsert_folder(folder_id, "/my/path")
                .apply(*sup)
                .share_folder(sha256, folder_id)
                .apply(*sup);

            auto &folder_infos = cluster->get_folders().by_id(folder_id)->get_folder_infos();
            auto fi_my = folder_infos.by_device(*my_device);
            auto fi_peer = folder_infos.by_device(*peer_device);

            REQUIRE(fi_my->is_introduced_by(my_device->device_id()));
            REQUIRE(fi_peer->is_introduced_by(my_device->device_id()));

            auto &db_env = db_actor->access<env>();
            auto txn_opt = db::make_transaction(db::transaction_type_t::RW, db_env);
            REQUIRE(txn_opt);
            auto &txn = txn_opt.value();
            REQUIRE(db::save_version(1, txn));

            auto save = [&](const model::folder_info_t &fi) -> outcome::result<void> {
                auto fi_db = db::FolderInfo();
                auto fi_key = fi.get_key();
                fi.serialize(fi_db);
                db::set_introducer_device_key(fi_db, {});
                auto fi_data = db::encode(fi_db);
                return db::save({fi_key, fi_data}, txn);
            };

            REQUIRE(save(*fi_my));
            REQUIRE(save(*fi_peer));
            REQUIRE(txn.commit());
        }
    };
    struct F2 : fixture_t {
        F2(fixture_t &other) : fixture_t(std::move(other)) {}
        void main() noexcept override {
            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);
            auto cluster_clone = make_cluster();
            auto controller = make_apply_controller(cluster_clone);
            REQUIRE(load_diff->apply(*controller, {}));

            auto f_cloned = cluster_clone->get_folders().by_id(folder_id);
            auto &folder_infos = f_cloned->get_folder_infos();
            auto fi_my = folder_infos.by_device(*my_device);
            auto fi_peer = folder_infos.by_device(*peer_device);

            CHECK(fi_my->is_introduced_by(my_device->device_id()));
            CHECK(fi_peer->is_introduced_by(my_device->device_id()));
        }
    };
    F2(F1().run()).run();
}

void test_db_migration_2_3() {
    static constexpr auto folder_id = "1234-5678";

    struct F1 : fixture_t {
        void main() noexcept override {
            // clang-format off
            using BlockInfoEx = pp::message<
               pp::uint32_field <"weak_hash", 1>,
               pp::int32_field  <"size",      2>
            >;
            // clang-format on

            auto bi = BlockInfoEx{1, 0x1234};
            auto new_value = syncspirit::details::generic_encode(bi);
            unsigned char key[model::block_info_t::digest_length + 1] = {0};
            key[0] = db::prefix::block_info;

            auto &db_env = db_actor->access<env>();
            auto txn_opt = db::make_transaction(db::transaction_type_t::RW, db_env);
            REQUIRE(txn_opt);
            auto &txn = txn_opt.value();
            REQUIRE(db::save_version(2, txn));
            REQUIRE(db::save({key, new_value}, txn));
            REQUIRE(txn.commit());
        }
    };
    struct F2 : fixture_t {
        F2(fixture_t &other) : fixture_t(std::move(other)) {}
        void main() noexcept override {
            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);
            auto cluster_clone = make_cluster();
            auto controller = make_apply_controller(cluster_clone);
            REQUIRE(load_diff->apply(*controller, {}));

            auto &blocks = cluster_clone->get_blocks();
            REQUIRE(blocks.size() == 1);
            auto b = *blocks.begin();
            CHECK(b->get_size() == 0x1234);
        }
    };
    F2(F1().run()).run();
}

void test_corrupted_file() {
    struct F : fixture_t {
        void main() noexcept override {
            auto folder_id = "1234-5678";
            auto builder = diff_builder_t(*cluster);
            builder.upsert_folder(folder_id, "/my/path").apply(*sup);
            CHECK(static_cast<r::actor_base_t *>(db_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            auto pr_file = proto::FileInfo();
            proto::set_name(pr_file, "a.txt");
            proto::set_size(pr_file, 5ul);

            auto hash = utils::sha256_digest(as_bytes("12345")).value();
            auto &pr_block = proto::add_blocks(pr_file);
            proto::set_size(pr_block, 5ul);
            proto::set_hash(pr_block, hash);

            builder.local_update(folder_id, pr_file).apply(*sup);
            auto block = *cluster->get_blocks().begin();

            auto folder = cluster->get_folders().by_id(folder_id);
            auto fi = folder->get_folder_infos().by_device(*my_device);

            auto file = fi->get_file_infos().by_name("a.txt");
            REQUIRE(file);

            auto &db_env = db_actor->access<env>();
            auto txn_opt = db::make_transaction(db::transaction_type_t::RW, db_env);
            REQUIRE(txn_opt);
            auto &txn = txn_opt.value();

            auto diff = model::diff::cluster_diff_ptr_t();
            SECTION("missing block") {
                auto key = test::make_key(block);
                REQUIRE(db::remove(key, txn));
                REQUIRE(!txn.commit().has_error());

                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);

                auto cluster_clone = make_cluster(false);
                auto controller = make_apply_controller(cluster_clone);
                diff = load_diff;
                REQUIRE(diff->apply(*controller, {}));
                auto &folder_infos = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                auto folder_info = folder_infos.by_device(*my_device);
                CHECK(folder_info->get_file_infos().size() == 0);
                auto &blocks = cluster_clone->get_blocks();
                CHECK(blocks.size() == 0);
            }

            SECTION("missing folder") {
                auto key = fi->get_key();
                REQUIRE(db::remove(key, txn));
                REQUIRE(!txn.commit().has_error());

                load_diff = {};
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);

                auto cluster_clone = make_cluster(false);
                auto controller = make_apply_controller(cluster_clone);
                diff = load_diff;
                REQUIRE(diff->apply(*controller, {}));
                auto &folder_infos = cluster_clone->get_folders().by_id(folder_id)->get_folder_infos();
                REQUIRE(folder_infos.size() == 0);
                auto &blocks = cluster_clone->get_blocks();
                CHECK(blocks.size() == 1);
            }

            REQUIRE(diff);
            sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff));
            sup->do_process();

            txn_opt = db::make_transaction(db::transaction_type_t::RW, db_env);
            REQUIRE(txn_opt);

            auto files_opt = db::load(db::prefix::file_info, txn);
            REQUIRE(files_opt);
            CHECK(files_opt.value().size() == 0);
        }
    };

    F().run();
}

void test_flush_on_shutdown() {
    struct F : fixture_t {

        config::db_config_t make_config() noexcept override { return config::db_config_t{1024 * 1024, 100, 64, 32}; }

        void main() noexcept override {

            auto folder_id = "1234-5678";

            auto pr_file = proto::FileInfo();
            proto::set_name(pr_file, "a.txt");
            proto::set_size(pr_file, 5ul);

            auto hash = utils::sha256_digest(as_bytes("12345")).value();
            auto &pr_block = proto::add_blocks(pr_file);
            proto::set_size(pr_block, 5ul);
            proto::set_hash(pr_block, hash);

            auto builder = diff_builder_t(*cluster);
            builder.upsert_folder(folder_id, "/my/path").apply(*sup).local_update(folder_id, pr_file).apply(*sup);

            load_diff = {};
            db_actor->do_shutdown();
            sup->do_process();

            launch_db();

            SECTION("loaded data") {
                sup->send<net::payload::load_cluster_trigger_t>(db_addr);
                sup->do_process();
                REQUIRE(load_diff);
                auto cluster_clone = make_cluster();
                auto controller = make_apply_controller(cluster_clone);
                REQUIRE(load_diff->apply(*controller, {}));

                auto folder = cluster_clone->get_folders().by_id(folder_id);
                auto folder_my = folder->get_folder_infos().by_device(*my_device);
                auto file = folder_my->get_file_infos().by_name("a.txt");
                REQUIRE(file);
                CHECK(file->iterate_blocks().get_total() == 1);
                CHECK(cluster_clone->get_blocks().size() == 1);
            }
        }
    };
    F().run();
};

void test_iterative_loading() {
    struct F : fixture_t {

        config::db_config_t make_config() noexcept override { return config::db_config_t{1024 * 1024, 0, 1, 1}; }

        void main() noexcept override {

            auto builder = diff_builder_t(*cluster);
            auto folder_id = "1234-5678";

            builder.upsert_folder(folder_id, "/my/path").apply(*sup);

            for (int i = 0; i < 20; ++i) {
                auto pr_file = proto::FileInfo();
                proto::set_name(pr_file, fmt::format("file-{:03}.bin", i));
                builder.local_update(folder_id, pr_file);
            }
            builder.apply(*sup);

            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);
            auto cluster_clone = make_cluster();
            auto controller = make_apply_controller(cluster_clone);
            REQUIRE(load_diff->apply(*controller, {}));

            auto folder = cluster_clone->get_folders().by_id(folder_id);
            auto folder_my = folder->get_folder_infos().by_device(*my_device);
            CHECK(folder_my->get_file_infos().size() == 20);
            CHECK(sup->packages >= 21);
        }
    };
    F().run();
};

void test_iterative_loading_interrupt() {
    struct F : fixture_t {

        config::db_config_t make_config() noexcept override { return config::db_config_t{1024 * 1024, 0, 1, 1}; }

        supervisor_ptr_t make_supervisor(r::system_context_t &ctx) noexcept override {
            struct sup_t : db_supervisor_t {
                using parent_t = db_supervisor_t;
                using parent_t::parent_t;

                void on_package(bouncer::message::package_t &msg) noexcept {
                    parent_t::on_package(msg);
                    if (packages == 5) {
                        fixture->db_actor->do_shutdown();
                    }
                }

                fixture_t *fixture = {};
            };

            auto sup = ctx.create_supervisor<sup_t>().timeout(timeout).create_registry().finish();
            sup->fixture = this;
            return sup;
        }

        void main() noexcept override {

            auto builder = diff_builder_t(*cluster);
            auto folder_id = "1234-5678";

            builder.upsert_folder(folder_id, "/my/path").apply(*sup);

            for (int i = 0; i < 20; ++i) {
                auto pr_file = proto::FileInfo();
                proto::set_name(pr_file, fmt::format("file-{:03}.bin", i));
                builder.local_update(folder_id, pr_file);
            }
            builder.apply(*sup);

            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(!load_diff);
            REQUIRE(ee);
            CHECK(sup->packages < 10);
        }
    };
    F().run();
};

template <typename T> using my_controller_base_t = model::diff::iterative_controller_t<T, r::actor_base_t>;
void test_iterative_application_interrupt() {
    struct F : fixture_t {

        config::db_config_t make_config() noexcept override { return config::db_config_t{1024 * 1024, 0, 1, 1}; }

        void main() noexcept override {

            struct my_controller_t : my_controller_base_t<my_controller_t> {
                using parent_t = my_controller_base_t<my_controller_t>;
                using parent_t::bouncer;
                using parent_t::cluster;
                using parent_t::resources;

                my_controller_t(r::actor_config_t &cfg) noexcept : parent_t(this, 0, cfg) {}

                void configure(r::plugin::plugin_base_t &plugin) noexcept override {
                    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
                        p.set_identity("my.controller", false);
                        log = utils::get_logger(identity);
                    });
                    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
                        p.discover_name(net::names::coordinator, coordinator, true)
                            .link(false)
                            .callback([&](auto phase, auto &ee) {
                                if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                                    auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                                    auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                                    plugin->subscribe_actor(&my_controller_t::on_model_update, address);
                                }
                            });
                    });
                    plugin.with_casted<r::plugin::starter_plugin_t>(
                        [&](auto &p) { p.subscribe_actor(&my_controller_t::on_model_interrupt); },
                        r::plugin::config_phase_t::PREINIT);
                }

                void process(model::diff::cluster_diff_t &diff, apply_context_t &context) noexcept override {
                    parent_t::process(diff, context);
                    auto &folder = cluster->get_folders().begin()->item;
                    auto &fi = folder->get_folder_infos().begin()->item;
                    if (fi->get_file_infos().size() == 10) {
                        do_shutdown();
                        resources->acquire(0);
                    }
                }
            };

            auto builder = diff_builder_t(*cluster);
            auto folder_id = "1234-5678";

            builder.upsert_folder(folder_id, "/my/path").apply(*sup);

            for (int i = 0; i < 20; ++i) {
                auto pr_file = proto::FileInfo();
                proto::set_name(pr_file, fmt::format("file-{:03}.bin", i));
                builder.local_update(folder_id, pr_file);
            }
            builder.apply(*sup);

            load_diff = {};
            sup->send<net::payload::load_cluster_trigger_t>(db_addr);
            sup->do_process();
            REQUIRE(load_diff);
            auto cluster_clone = make_cluster();

            auto controller = sup->create_actor<my_controller_t>().timeout(timeout).finish();
            controller->bouncer = sup->get_address();
            auto addr = controller->get_address();
            sup->do_process();
            controller->cluster = cluster_clone;
            sup->send<model::payload::model_update_t>(addr, std::move(load_diff));
            sup->do_process();

            auto folder = cluster_clone->get_folders().by_id(folder_id);
            auto folder_my = folder->get_folder_infos().by_device(*my_device);
            auto files_sz = folder_my->get_file_infos().size();
            CHECK(files_sz >= 10);
            CHECK(files_sz <= 12);

            CHECK(static_cast<r::actor_base_t *>(controller.get())->access<to::state>() == r::state_t::SHUTTING_DOWN);
            controller->resources->release(0);
            sup->do_process();
            CHECK(static_cast<r::actor_base_t *>(controller.get())->access<to::state>() == r::state_t::SHUT_DOWN);

            CHECK(get_reading_txn() == 0);
        }
    };
    F().run();
};

int _init() {
    REGISTER_TEST_CASE(test_db_population, "test_db_population", "[db]");
    REGISTER_TEST_CASE(test_loading_empty_db, "test_loading_empty_db", "[db]");
    REGISTER_TEST_CASE(test_forget_to_commit_other_thread, "test_forget_to_commit_other_thread", "[db]");
    REGISTER_TEST_CASE(test_forget_to_commit_own_thread, "test_forget_to_commit_own_thread", "[db]");
    REGISTER_TEST_CASE(test_unknown_and_ignored_devices_1, "test_unknown_and_ignored_devices_1", "[db]");
    REGISTER_TEST_CASE(test_unknown_and_ignored_devices_2, "test_unknown_and_ignored_devices_2", "[db]");
    REGISTER_TEST_CASE(test_folder_upserting, "test_folder_upserting", "[db]");
    REGISTER_TEST_CASE(test_peer_updating, "test_peer_updating", "[db]");
    REGISTER_TEST_CASE(test_folder_sharing, "test_folder_sharing", "[db]");
    REGISTER_TEST_CASE(test_cluster_update_and_remove, "test_cluster_update_and_remove", "[db]");
    REGISTER_TEST_CASE(test_unshare_and_remove_folder, "test_unshare_and_remove_folder", "[db]");
    REGISTER_TEST_CASE(test_remote_copy, "test_remote_copy", "[db]");
    REGISTER_TEST_CASE(test_local_update, "test_local_update", "[db]");
    REGISTER_TEST_CASE(test_peer_going_offline, "test_peer_going_offline", "[db]");
    REGISTER_TEST_CASE(test_remove_peer, "test_remove_peer", "[db]");
    REGISTER_TEST_CASE(test_update_peer, "test_update_peer", "[db]");
    REGISTER_TEST_CASE(test_peer_3_folders_6_files, "test_peer_3_folders_6_files", "[db]");
    REGISTER_TEST_CASE(test_db_migration_1_2, "test_db_migration_1_2", "[db]");
    REGISTER_TEST_CASE(test_db_migration_2_3, "test_db_migration_2_3", "[db]");
    REGISTER_TEST_CASE(test_corrupted_file, "test_corrupted_file", "[db]");
    REGISTER_TEST_CASE(test_flush_on_shutdown, "test_flush_on_shutdown", "[db]");
    REGISTER_TEST_CASE(test_iterative_loading, "test_iterative_loading", "[db]");
    REGISTER_TEST_CASE(test_iterative_loading_interrupt, "test_iterative_loading_interrupt", "[db]");
    REGISTER_TEST_CASE(test_iterative_application_interrupt, "test_iterative_application_interrupt", "[db]");
    return 1;
}

static int v = _init();

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/local/scan_start.h"
#include "fs/scan_scheduler.h"
#include "fs/messages.h"
#include "net/names.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<fs::scan_scheduler_t>;
    using msg_t = fs::message::scan_folder_t;
    using msg_ptr_t = r::intrusive_ptr_t<msg_t>;
    using messages_t = std::vector<msg_ptr_t>;

    fixture_t() noexcept { utils::set_default("trace"); }

    void run(bool create_folder) noexcept {
        auto my_id =
            device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        my_device = device_t::create(my_id, "my-device").value();

        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>(
                [&](auto &p) { p.subscribe_actor(r::lambda<msg_t>([&](msg_t &msg) { messages.push_back(&msg); })); });
            plugin.with_casted<r::plugin::registry_plugin_t>(
                [&](auto &p) { p.register_name(names::fs_scanner, sup->get_address()); });
        };

        sup->start();
        sup->do_process();

        folder_id = "1234-5678";

        if (create_folder) {
            diff_builder_t(*cluster).upsert_folder(folder_id, "/some/path").apply(*sup);
            folder = cluster->get_folders().by_id(folder_id);
        }

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);
        sup->do_process();

        target =
            sup->create_actor<fs::scan_scheduler_t>().timeout(timeout).cluster(cluster).time_scale(0.0001).finish();
        sup->do_process();

        REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
        main();

        sup->do_process();
        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    r::pt::time_duration timeout = r::pt::millisec{10};
    r::intrusive_ptr_t<supervisor_t> sup;
    cluster_ptr_t cluster;
    device_ptr_t my_device;
    std::string folder_id;
    target_ptr_t target;
    model::folder_ptr_t folder;
    messages_t messages;
};

void test_model_update() {
    struct F : fixture_t {
        void main() noexcept override {
            REQUIRE(messages.size() == 0);

            auto db_folder = db::Folder();
            db_folder.set_id(folder_id);

            auto builder = diff_builder_t(*cluster);

            SECTION("zero rescan time => no scan") {
                builder.upsert_folder(db_folder).apply(*sup);
                REQUIRE(messages.size() == 0);
            }

            SECTION("non-zero rescan time") {
                db_folder.set_rescan_interval(3600);
                builder.upsert_folder(db_folder).apply(*sup);

                REQUIRE(messages.size() == 1);
                CHECK(messages.front()->payload.folder_id == folder_id);

                SECTION("scan start/finish") {
                    messages.resize(0);
                    auto diff = model::diff::cluster_diff_ptr_t{};
                    diff = new model::diff::local::scan_start_t(folder_id, r::pt::microsec_clock::local_time());
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    builder.upsert_folder(db_folder).apply(*sup);
                    REQUIRE(messages.size() == 0);

                    diff = new model::diff::local::scan_finish_t(folder_id, r::pt::microsec_clock::local_time());
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    builder.upsert_folder(db_folder).apply(*sup);

                    REQUIRE(sup->timers.size() == 1);
                    sup->do_invoke_timer((*sup->timers.begin())->request_id);
                    sup->do_process();

                    REQUIRE(messages.size() == 1);
                    CHECK(messages.front()->payload.folder_id == folder_id);
                }
            }
        }
    };
    F().run(false);
};

int _init() {
    REGISTER_TEST_CASE(test_model_update, "test_model_update", "[fs]");
    return 1;
}

static int v = _init();

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "net/scheduler.h"
#include "net/names.h"
#include <chrono>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

namespace {
namespace ns {
namespace to {
struct scan_queue {};
} // namespace to
} // namespace ns
} // namespace

namespace syncspirit::net {

template <> inline auto &scheduler_t::access<ns::to::scan_queue>() noexcept { return scan_queue; }

} // namespace syncspirit::net

using Clock = r::pt::microsec_clock;

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<net::scheduler_t>;

    fixture_t() noexcept {
        test::init_logging();
        log = utils::get_logger("fixture");
    }

    void run() noexcept {
        auto my_id =
            device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        my_device = device_t::create(my_id, "my-device").value();

        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        sup->start();
        sup->do_process();

        folder_id = "1234-5678";

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);
        sup->do_process();

        target = sup->create_actor<net::scheduler_t>().timeout(timeout).finish();
        sup->do_process();

        sup->send<syncspirit::model::payload::thread_ready_t>(sup->get_address(), cluster, std::this_thread::get_id());
        sup->do_process();

        REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
        main();

        sup->do_process();
        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    utils::logger_t log;
    r::pt::time_duration timeout = r::pt::millisec{10};
    r::intrusive_ptr_t<supervisor_t> sup;
    cluster_ptr_t cluster;
    device_ptr_t my_device;
    std::string folder_id;
    target_ptr_t target;
    model::folder_ptr_t folder;
};

void test_1_folder() {
    struct F : fixture_t {
        void main() noexcept override {
            auto db_folder = db::Folder();
            db::set_id(db_folder, folder_id);

            auto builder = diff_builder_t(*cluster);

            SECTION("zero rescan time => no scan") {
                builder.upsert_folder(db_folder).apply(*sup);
                auto folder = cluster->get_folders().by_id(folder_id);
                CHECK(!folder->is_scanning());
            }

            SECTION("non-zero rescan time") {
                db::set_rescan_interval(db_folder, 1);
                builder.upsert_folder(db_folder).apply(*sup);

                auto folder = cluster->get_folders().by_id(folder_id);
                CHECK(folder->is_scanning());

                SECTION("scan start/finish") {
                    builder.scan_finish(folder_id).apply(*sup);
                    REQUIRE(!folder->is_scanning());

                    REQUIRE(sup->timers.size() == 1);
                    sup->do_invoke_timer((*sup->timers.begin())->request_id);
                    REQUIRE(!folder->is_scanning());

                    REQUIRE(sup->timers.size() == 1);
                    std::this_thread::sleep_for(std::chrono::milliseconds{2100});

                    sup->do_invoke_timer((*sup->timers.begin())->request_id);
                    sup->do_process();
                    REQUIRE(folder->is_scanning());
                }

                SECTION("synchronization start/finish") {
                    builder.scan_finish(folder_id).synchronization_start(folder_id).scan_request(folder_id).apply(*sup);
                    REQUIRE(!folder->is_scanning());
                    REQUIRE(sup->timers.size() == 0);

                    builder.synchronization_finish(folder_id).apply(*sup);
                    REQUIRE(sup->timers.size() == 1);
                    std::this_thread::sleep_for(std::chrono::milliseconds{2100});
                    sup->do_invoke_timer((*sup->timers.begin())->request_id);
                    sup->do_process();
                    REQUIRE(folder->is_scanning());
                }

                SECTION("subdir coverage") {
                    REQUIRE(sup->timers.size() == 0);
                    REQUIRE(target->access<ns::to::scan_queue>().size() == 0);

                    builder.scan_request(folder_id, "a-dir").apply(*sup);
                    REQUIRE(target->access<ns::to::scan_queue>().size() == 1);

                    builder.scan_request(folder_id, "a-dir/b-subdir").apply(*sup);
                    REQUIRE(target->access<ns::to::scan_queue>().size() == 1);
                    CHECK(target->access<ns::to::scan_queue>().front().sub_dir == "a-dir");

                    builder.scan_request(folder_id, "").apply(*sup);
                    REQUIRE(target->access<ns::to::scan_queue>().size() == 1);
                    CHECK(target->access<ns::to::scan_queue>().front().sub_dir == "");
                }
            }

            SECTION("suspending") {
                db::set_rescan_interval(db_folder, 3600);
                builder.upsert_folder(db_folder).apply(*sup);

                auto folder = cluster->get_folders().by_id(folder_id);
                CHECK(folder->is_scanning());
                builder.suspend(*folder).scan_finish(folder_id).apply(*sup);
                REQUIRE(sup->timers.size() == 0);
            }
        }
    };
    F().run();
};

void test_2_folders() {
    struct F : fixture_t {
        using C = r::pt::microsec_clock;
        void main() noexcept override {
            auto f1_id = "1111";
            auto f2_id = "2222";
            auto db_folder_1 = db::Folder();
            auto db_folder_2 = db::Folder();
            db::set_id(db_folder_1, f1_id);
            db::set_id(db_folder_2, f2_id);

            auto rescan_min = std::uint32_t(2);
            db::set_rescan_interval(db_folder_1, 4);
            db::set_rescan_interval(db_folder_2, rescan_min);

            auto builder = diff_builder_t(*cluster);
            builder.upsert_folder(db_folder_1)
                .upsert_folder(db_folder_2)
                .apply(*sup)
                .scan_start(f2_id, {}, C::local_time() - r::pt::seconds{rescan_min + 10})
                .scan_finish(f2_id, C::local_time() - r::pt::seconds{rescan_min + 1})
                .apply(*sup)
                .scan_start(f1_id, {}, C::local_time() - r::pt::seconds{rescan_min + 10})
                .scan_finish(f1_id, C::local_time() - r::pt::seconds{rescan_min + 1})
                .apply(*sup);

            REQUIRE(sup->timers.size() == 1);

            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            sup->do_invoke_timer((*sup->timers.begin())->request_id);
            sup->do_process();

            auto f1 = cluster->get_folders().by_id(f1_id);
            auto f2 = cluster->get_folders().by_id(f2_id);

            REQUIRE(!f1->is_scanning());
            REQUIRE(f2->is_scanning());
            builder.scan_finish(f2_id, C::local_time() + r::pt::seconds{rescan_min + 1}).apply(*sup);
            REQUIRE(!f1->is_scanning());
            REQUIRE(!f2->is_scanning());

            REQUIRE(sup->timers.size() == 1);
            std::this_thread::sleep_for(std::chrono::milliseconds{1011});
            sup->do_invoke_timer((*sup->timers.begin())->request_id);
            sup->do_process();
            REQUIRE(f1->is_scanning());
            REQUIRE(!f2->is_scanning());
        }
    };
    F().run();
};

int _init() {
    REGISTER_TEST_CASE(test_1_folder, "test_1_folder", "[fs]");
    REGISTER_TEST_CASE(test_2_folders, "test_2_folders", "[fs]");
    return 1;
}

static int v = _init();

#include "catch.hpp"
#include "test-utils.h"
#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;


TEST_CASE("opt_for_synch", "[model]") {

    auto d1_id = model::device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto d2_id = model::device_id_t::from_string("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7").value();

    config::device_config_t d1_cfg{
        d1_id.get_value(),
        "d1",
        config::compression_t::all,
        "d1-cert_name",
        false,
        false,
        false,
        false,
        {},
        {}
    };
    config::device_config_t d2_cfg{
        d2_id.get_value(),
        "d2",
        config::compression_t::all,
        "d2-cert_name",
        false,
        false,
        false,
        false,
        {},
        {}
    };
    device_ptr_t d1 = new device_t(d1_cfg);
    device_ptr_t d2 = new device_t(d2_cfg);

    config::folder_config_t f1_cfg{
        "f1",
        "f1",
        "/some/path/d1",
        {d1_id.get_value(), d2_id.get_value()},
        config::folder_type_t::send_and_receive,
        3600,
        config::pull_order_t::random,
        true,
        true,
        false,
        false,
        false,
        false,
    };

    config::folder_config_t f2_cfg{
        "f2",
        "f2",
        "/some/path/d1",
        {d1_id.get_value(), d2_id.get_value()},
        config::folder_type_t::send_and_receive,
        3600,
        config::pull_order_t::random,
        true,
        true,
        false,
        false,
        false,
        false,
    };

    folder_ptr_t f1 = new folder_t(f1_cfg, d1);
    folder_ptr_t f2 = new folder_t(f2_cfg, d1);

    cluster_ptr_t cluster = new cluster_t(d1);
    cluster->add_folder(f1);
    cluster->add_folder(f2);

    SECTION("no need of update") {
        f1->devices.emplace(folder_device_t{d1, 0, 5});
        f1->devices.emplace(folder_device_t{d2, 0, 4});
        f2->devices.emplace(folder_device_t{d1, 0, 5});
        f2->devices.emplace(folder_device_t{d2, 0, 4});
        auto f = cluster->opt_for_synch(d2);
        CHECK(!f);
    }

    SECTION("f1 & f2 needs update, but f1 has higher score") {
        f1->devices.emplace(folder_device_t{d1, 0, 5});
        f1->devices.emplace(folder_device_t{d2, 0, 10});
        f2->devices.emplace(folder_device_t{d1, 0, 5});
        f2->devices.emplace(folder_device_t{d2, 0, 8});
        auto f = cluster->opt_for_synch(d2);
        CHECK(f == f1);
    }

    SECTION("f1 & f2 needs update, but f2 has higher score") {
        f1->devices.emplace(folder_device_t{d1, 0, 5});
        f1->devices.emplace(folder_device_t{d2, 0, 10});
        f2->devices.emplace(folder_device_t{d1, 0, 5});
        f2->devices.emplace(folder_device_t{d2, 0, 18});
        auto f = cluster->opt_for_synch(d2);
        CHECK(f == f2);
    }

}

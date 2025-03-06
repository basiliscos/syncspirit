// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "test-utils.h"
#include "model/version.h"

using namespace syncspirit;
using namespace syncspirit::model;

TEST_CASE("version ", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto proto_v = proto::Vector();
    auto c0 = proto::Counter(1, 2);
    auto c1 = proto::Counter(my_device->device_id().get_uint(), 10);
    proto::add_counters(proto_v, c0);
    proto::add_counters(proto_v, c1);

    auto v1 = version_ptr_t(new version_t(proto_v));
    auto v1_copy = version_ptr_t(new version_t(proto_v));
    v1->update(*my_device);

    CHECK(v1->contains(*v1_copy));
    CHECK(v1->contains(*v1));
    CHECK(!v1_copy->contains(*v1));
    CHECK(v1_copy->contains(*v1_copy));
}

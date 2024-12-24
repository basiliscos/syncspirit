// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "test-utils.h"
#include "model/version.h"

using namespace syncspirit;
using namespace syncspirit::model;

TEST_CASE("version ", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto proto_v = proto::Vector();
    auto c0 = proto_v.mutable_counters()->Add();
    c0->set_id(1);
    c0->set_value(2);

    auto c1 = proto_v.mutable_counters()->Add();
    c1->set_id(my_device->as_uint());
    c1->set_value(10);

    auto v1 = version_ptr_t(new version_t(proto_v));
    auto v1_copy = version_ptr_t(new version_t(proto_v));
    v1->update(*my_device);

    CHECK(v1->contains(*v1_copy));
    CHECK(v1->contains(*v1));
    CHECK(!v1_copy->contains(*v1));
    CHECK(v1_copy->contains(*v1_copy));
}

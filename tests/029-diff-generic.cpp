// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "model/diff/cluster_diff.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

struct fail_diff_t : diff::cluster_diff_t {
    outcome::result<void> apply_impl(diff::apply_controller_t &, void *) const noexcept override {
        auto ec = make_error_code(error_code_t::source_device_not_exists);
        return outcome::failure(ec);
    }
};

TEST_CASE("generic cluster diff", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto controller = make_apply_controller(cluster);
    auto diff = diff::cluster_diff_ptr_t(new fail_diff_t());
    CHECK(!cluster->is_tainted());
    CHECK(!diff->apply(*controller, {}));
    CHECK(cluster->is_tainted());
}

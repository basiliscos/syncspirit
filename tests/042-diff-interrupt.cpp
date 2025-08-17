// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "model/diff/load/interrupt.h"
#include "test-utils.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "model/misc/sequencer.h"
#include "model/diff/diffs_fwd.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

struct apply_context_t {
    model::diff::cluster_diff_ptr_t next;
};

struct apply_controler_t final : model::diff::apply_controller_t {
    apply_controler_t(model::cluster_ptr_t cluster_) { cluster = cluster_; }
    outcome::result<void> apply(const diff::load::interrupt_t &diff, void *custom) noexcept override {
        auto ctx = reinterpret_cast<apply_context_t *>(custom);
        ctx->next = diff.sibling;
        return outcome::success();
    }
};

TEST_CASE("diff interrupt", "[model]") {
    test::init_logging();

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();

    auto my_device = device_ptr_t{};
    my_device = new model::local_device_t(my_id, "my-device", "my-device");

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    auto &devices = cluster->get_devices();
    devices.put(my_device);

    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "/my/path").apply());

    auto pr_file = proto::FileInfo();
    proto::set_block_size(pr_file, 0);

    auto &v1 = proto::get_version(pr_file);
    proto::add_counters(v1, proto::Counter(my_device->device_id().get_uint(), 1));

    proto::set_name(pr_file, "a.bin");
    builder.local_update("1234-5678", pr_file).interrupt();

    proto::set_name(pr_file, "b.bin");
    builder.local_update("1234-5678", pr_file).interrupt();

    proto::set_name(pr_file, "c.bin");
    builder.local_update("1234-5678", pr_file).interrupt();

    auto folder = cluster->get_folders().by_id("1234-5678");
    auto folder_my = folder->get_folder_infos().by_device(*my_device);
    auto &files = folder_my->get_file_infos();

    auto ctx = apply_context_t(builder.extract());
    auto controller = apply_controler_t(cluster);

    SECTION("generic applicator") {
        auto controller = make_apply_controller(cluster);
        REQUIRE(ctx.next->apply(*controller, {}));
        REQUIRE(files.size() == 3);
        REQUIRE(files.by_name("a.bin"));
        REQUIRE(files.by_name("b.bin"));
        REQUIRE(files.by_name("c.bin"));
    }

    SECTION("custom applicator") {

        REQUIRE(ctx.next);
        REQUIRE(files.size() == 0);

        REQUIRE(ctx.next->apply(controller, &ctx));
        REQUIRE(files.size() == 1);
        REQUIRE(files.by_name("a.bin"));
        REQUIRE(ctx.next);

        REQUIRE(ctx.next->apply(controller, &ctx));
        REQUIRE(files.size() == 2);
        REQUIRE(files.by_name("b.bin"));
        REQUIRE(ctx.next);

        REQUIRE(ctx.next->apply(controller, &ctx));
        REQUIRE(files.size() == 3);
        REQUIRE(files.by_name("c.bin"));
        REQUIRE(!ctx.next);
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();

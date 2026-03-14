// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include <rotor.hpp>
#include <rotor/thread.hpp>
#include "model/diff/iterative_controller.h"
#include "bouncer/messages.hpp"
#include "net/names.h"
#include "test-utils.h"
#include "access.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::test;

namespace r = rotor;
namespace rth = r::thread;

namespace {
namespace resource {
r::plugin::resource_id_t interrupt = 0;
} // namespace resource
} // namespace

template <typename T> using sample_supervisor_base_t = model::diff::iterative_controller_t<T, rth::supervisor_thread_t>;

static const r::pt::time_duration timeout = r::pt::millisec{10};
static constexpr size_t I = 50;
static constexpr size_t N = 15'000;

struct sample_supervisor_t : sample_supervisor_base_t<sample_supervisor_t> {
    using parent_t = sample_supervisor_base_t<sample_supervisor_t>;
    using config_t = typename parent_t::config_t;
    using parent_t::cluster;

    sample_supervisor_t(config_t &cfg) : parent_t(this, resource::interrupt, cfg) {
        log = utils::get_logger("sample-sup");
    }

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        parent_t::configure(plugin);
        plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
            p.set_identity("sample-sup", false);
            bouncer = address;
        });
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.register_name(names::coordinator, get_address()); });
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&sample_supervisor_t::on_model_interrupt);
            p.subscribe_actor(&sample_supervisor_t::on_model_update);
            p.subscribe_actor(&sample_supervisor_t::on_package);
        });
    }

    void on_package(bouncer::message::package_t &msg) noexcept {
        LOG_TRACE(log, "on_package");
        put(std::move(msg.payload));
        ++bounces;
    }

    std::uint32_t bounces = 0;
};

struct fixture_t {
    void run() {
        auto local_id_str = "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD";
        auto local_id = device_id_t::from_string(local_id_str).value();
        auto local_device = device_t::create(local_id, "local-device").value();
        cluster = model::cluster_ptr_t(new cluster_t(local_device, 1));
        cluster->get_devices().put(local_device);

        auto ctx = rth::system_context_ptr_t(new rth::system_context_thread_t());
        sup = ctx->create_supervisor<sample_supervisor_t>().timeout(timeout).create_registry().finish().get();
        sup->cluster = cluster;
        sup->start();
        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(sup)->access<to::state>() == r::state_t::OPERATIONAL);

        auto builder = diff_builder_t(*cluster);
        folder_id = "1234-5678";

        builder.upsert_folder(folder_id, "").apply(*sup);
        auto &local_folder = *cluster->get_folders().by_id(folder_id);
        local_files = &local_folder.get_folder_infos().by_device(*local_device)->get_file_infos();

        main(builder);

        sup->do_shutdown();
        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(sup)->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main(diff_builder_t &builder) noexcept = 0;

    sample_supervisor_t *sup;
    cluster_ptr_t cluster;
    std::string_view folder_id;
    file_infos_map_t *local_files;
};

void test_large_diffs_chain() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        void main(diff_builder_t &builder) noexcept override {
            auto pr_file = proto::FileInfo();
            proto::set_name(pr_file, "root");
            proto::set_type(pr_file, proto::FileInfoType::DIRECTORY);

            builder.local_update(folder_id, pr_file);
            for (size_t i = 0; i < N; ++i) {
                auto name = fmt::format("subdir-{}", i);
                proto::set_name(pr_file, name);
                builder.local_update(folder_id, pr_file);
                if (i % I == 0) {
                    builder.interrupt();
                }
            }
            builder.apply(*sup);
            CHECK(local_files->size() == N + 1);
            CHECK(sup->bounces == N / I);
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_large_diffs_chain, "test_large_diffs_chain", "[model]");
    return 1;
}

static int v = _init();

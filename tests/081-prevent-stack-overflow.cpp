// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include <rotor.hpp>
#include <rotor/thread.hpp>
#include "model/diff/apply_controller.h"
#include "model/diff/diff_assembler.h"
#include "model/diff/iterative_controller.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/cluster_visitor.h"
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

static const r::pt::time_duration timeout = r::pt::millisec{10};
static constexpr size_t I = 50;
static constexpr size_t N = 2500;

struct my_local_update_t : diff::advance::local_update_t {
    using parent_t = diff::advance::local_update_t;
    using parent_t::parent_t;

    outcome::result<void> apply_impl(diff::apply_controller_t &controller, void *custom) const noexcept override {
        char buff[1024 * 100] = {0};
        return parent_t::apply_impl(controller, custom);
    }
};

struct my_diff_builder_t : diff_builder_t {
    using parent_t = diff_builder_t;
    using parent_t::parent_t;

    diff_builder_t &local_update(std::string_view folder_id, const proto::FileInfo &file_) noexcept override {
        return assign(new my_local_update_t(*cluster, *sequencer, file_, folder_id));
    }
};

template <typename T>
using sample_supervisor_base_t =
    model::diff::iterative_controller_t<T, rth::supervisor_thread_t, model::diff::cluster_visitor_t>;

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
        sup = create_sup(ctx.get());
        sup->cluster = cluster;
        sup->start();
        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(sup)->access<to::state>() == r::state_t::OPERATIONAL);

        auto builder = my_diff_builder_t(*cluster, sup->get_address());
        folder_id = "1234-5678";

        builder.upsert_folder(folder_id, "").apply(*sup);
        auto &local_folder = *cluster->get_folders().by_id(folder_id);
        local_files = &local_folder.get_folder_infos().by_device(*local_device)->get_file_infos();

        main(builder);

        sup->do_shutdown();
        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(sup)->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual sample_supervisor_t *create_sup(r::system_context_t *ctx) noexcept {
        return ctx->create_supervisor<sample_supervisor_t>().timeout(timeout).create_registry().finish().get();
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

            SECTION("via builder") {
                builder.local_update(folder_id, pr_file).apply(*sup);
                for (size_t i = 0; i < N; ++i) {
                    auto name = fmt::format("subdir-{}", i);
                    proto::set_name(pr_file, name);
                    builder.local_update(folder_id, pr_file);
                    if (i % I == 0) {
                        builder.interrupt();
                    }
                }
                builder.apply(*sup);
            }
            SECTION("via assembler") {
                auto assember = model::diff::diff_assember_t(I);
                assember.push_back(builder.local_update(folder_id, pr_file).extract().get());
                for (size_t i = 0; i < N; ++i) {
                    auto name = fmt::format("subdir-{}", i);
                    proto::set_name(pr_file, name);
                    auto diff = builder.local_update(folder_id, pr_file).extract();
                    assember.push_back(diff.get());
                }
                sup->send<model::payload::model_update_t>(sup->get_address(), assember.consume(), nullptr);
                sup->do_process();
            }
            CHECK(local_files->size() == N + 1);
            CHECK(sup->bounces == N / I);
        }
    };
    F().run();
}

void test_updates_intermixture() {
    static constexpr size_t I = 2;
    static constexpr size_t N = 10;

    struct sup_t : sample_supervisor_t {
        using parent_t = sample_supervisor_t;
        using parent_t::parent_t;

        void process(diff::cluster_diff_t &diff, model::payload::apply_context_t &context) noexcept override {
            parent_t::process(diff, context);

            auto r = diff.visit(*this, {});
            CHECK(r);
        }

        outcome::result<void> operator()(const diff::advance::local_update_t &diff, void *custom) noexcept override {
            ++update_visits;
            return parent_t::operator()(diff, custom);
        }

        int update_visits = 0;
    };

    struct F : fixture_t {
        using fixture_t::fixture_t;

        sample_supervisor_t *create_sup(r::system_context_t *ctx) noexcept override {
            return ctx->create_supervisor<sup_t>().timeout(timeout).create_registry().finish().get();
        }

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

            builder.send(*sup);
            proto::set_name(pr_file, "n1");
            builder.local_update(folder_id, pr_file).send(*sup);
            proto::set_name(pr_file, "n2");
            builder.local_update(folder_id, pr_file).apply(*sup);
            CHECK(local_files->size() == N + 3);
            CHECK(sup->bounces == N / I);

            auto impl = static_cast<sup_t *>(sup);
            CHECK(impl->update_visits == local_files->size());
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_large_diffs_chain, "test_large_diffs_chain", "[model]");
    REGISTER_TEST_CASE(test_updates_intermixture, "test_updates_intermixture", "[model]");
    return 1;
}

static int v = _init();

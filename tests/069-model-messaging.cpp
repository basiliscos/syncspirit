// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "test-utils.h"
#include "net/names.h"

#include "model/cluster.h"
#include "model/diff/modify/add_blocks.h"
#include "model/diff/iterative_controller.h"
#include "model/diff/diff_assembler.h"
#include "model/diff/cluster_visitor.h"
#include "net/model_actor.hpp"
#include "bouncer/messages.hpp"

#include <rotor.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t interrupt = 0;
} // namespace resource
} // namespace

namespace to {
struct queue {};
struct on_timer_trigger {};
} // namespace to

template <> inline auto &rotor::supervisor_t::access<to::queue>() noexcept { return queue; }

namespace rotor {

template <>
inline auto rotor::actor_base_t::access<to::on_timer_trigger, request_id_t, bool>(request_id_t request_id,
                                                                                  bool cancelled) noexcept {
    on_timer_trigger(request_id, cancelled);
}

} // namespace rotor

struct supervisor_impl_t : r::supervisor_t {
    using parent_t = r::supervisor_t;
    using parent_t::parent_t;
    using timers_t = std::list<r::timer_handler_base_t *>;

    void do_start_timer(const r::pt::time_duration &interval, r::timer_handler_base_t &handler) noexcept override;
    void do_invoke_timer(r::request_id_t timer_id) noexcept;
    void do_cancel_timer(r::request_id_t timer_id) noexcept override;
    void start() noexcept override;
    void shutdown() noexcept override;
    void enqueue(r::message_ptr_t message) noexcept override;

    timers_t timers;
};

struct my_controller_config_t : supervisor_impl_t::config_t {
    model::cluster_ptr_t cluster;
};

// using sample_diff_t = model::diff::modify::add_blocks_t;

template <typename Actor> struct my_controller_config_builder_t : supervisor_impl_t::config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = supervisor_impl_t::config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
        parent_t::config.cluster = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

template <typename T> using controller_base_t = model::diff::iterative_controller_t<T, supervisor_impl_t>;

struct my_controller_t final : controller_base_t<my_controller_t> {
    using parent_t = controller_base_t<my_controller_t>;
    using config_t = my_controller_config_t;
    template <typename Actor> using config_builder_t = my_controller_config_builder_t<Actor>;

    explicit my_controller_t(config_t &cfg);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void process(diff::cluster_diff_t &diff, payload::apply_context_t &context) noexcept override;

    void on_package(bouncer::message::package_t &msg) noexcept {
        LOG_TRACE(log, "on package");
        put(std::move(msg.payload));
    }
};

struct my_visitor_actor_t final : r::actor_base_t, private model::diff::cluster_visitor_t {
    using parent_t = r::actor_base_t;
    using parent_t::parent_t;

    outcome::result<void> operator()(const model::diff::modify::add_blocks_t &diff, void *custom) noexcept override;

    void visit(const model::diff::cluster_diff_t &, model::payload::apply_context_t &) noexcept;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_start() noexcept override;

    int add_blocks_counter = 0;
    r::address_ptr_t coordinator;
    utils::logger_t log;
};

struct my_2nd_visitor_actor_t final : net::model_actor_t<r::actor_base_t>, private model::diff::cluster_visitor_t {
    using parent_t = net::model_actor_t<r::actor_base_t>;
    using parent_t::parent_t;

    outcome::result<void> operator()(const model::diff::modify::add_blocks_t &diff, void *custom) noexcept override;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void visit(const model::diff::cluster_diff_t &, model::payload::apply_context_t &) noexcept override;

    int add_blocks_counter = 0;
};

// impl

void supervisor_impl_t::do_start_timer(const r::pt::time_duration &interval,
                                       r::timer_handler_base_t &handler) noexcept {
    timers.emplace_back(&handler);
}

void supervisor_impl_t::do_invoke_timer(r::request_id_t timer_id) noexcept {
    auto predicate = [&](auto &handler) { return handler->request_id == timer_id; };
    auto it = std::find_if(timers.begin(), timers.end(), predicate);
    assert(it != timers.end());
    auto &handler = *it;
    auto &actor_ptr = handler->owner;
    actor_ptr->access<to::on_timer_trigger, r::request_id_t, bool>(timer_id, false);
    timers.erase(it);
}

void supervisor_impl_t::do_cancel_timer(r::request_id_t timer_id) noexcept {
    auto it = timers.begin();
    while (it != timers.end()) {
        auto &handler = *it;
        if (handler->request_id == timer_id) {
            auto &actor_ptr = handler->owner;
            actor_ptr->access<to::on_timer_trigger, r::request_id_t, bool>(timer_id, true);
            on_timer_trigger(timer_id, true);
            timers.erase(it);
            return;
        } else {
            ++it;
        }
    }
    assert(0 && "should not happen");
}

void supervisor_impl_t::start() noexcept {}
void supervisor_impl_t::shutdown() noexcept { do_shutdown(); }
void supervisor_impl_t::enqueue(r::message_ptr_t) noexcept { std::abort(); }

my_controller_t::my_controller_t(config_t &cfg) : parent_t(this, resource::interrupt, cfg) { cluster = cfg.cluster; }

void my_controller_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(names::coordinator, false);
        log = utils::get_logger(identity);
        bouncer = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        coordinator = address;
        p.register_name(names::coordinator, get_address());
    });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) {
            p.subscribe_actor(&my_controller_t::on_model_update);
            p.subscribe_actor(&my_controller_t::on_model_interrupt);
            p.subscribe_actor(&my_controller_t::on_model_subscribe);
            p.subscribe_actor(&my_controller_t::on_model_unsubscribe);
            p.subscribe_actor(&my_controller_t::on_package, bouncer);
        },
        r::plugin::config_phase_t::PREINIT);
}

void my_controller_t::process(diff::cluster_diff_t &diff, model::payload::apply_context_t &context) noexcept {
    LOG_INFO(log, "processing diff");
    parent_t::process(diff, context);
}

auto my_visitor_actor_t::operator()(const model::diff::modify::add_blocks_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    LOG_INFO(log, "visiting add_blocsk, c = {}", add_blocks_counter);
    ++add_blocks_counter;
    return model::diff::cluster_visitor_t::operator()(diff, custom);
}

static void model_visit_callback(const model::diff::cluster_diff_t &diff, model::payload::apply_context_t &ctx,
                                 void *custom) {
    reinterpret_cast<my_visitor_actor_t *>(custom)->visit(diff, ctx);
}

void my_visitor_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { log = utils::get_logger("my-visitor"); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::db, get_address());
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                send<payload::model_subscription_t>(coordinator, model_visit_callback, this);
            }
        });
    });
}

void my_visitor_actor_t::visit(const model::diff::cluster_diff_t &diff, model::payload::apply_context_t &) noexcept {
    LOG_INFO(log, "visit");
    auto r = diff.visit(*this, {});
    CHECK(r);
}

void my_visitor_actor_t::shutdown_start() noexcept {
    send<payload::model_unsubscription_t>(coordinator, model_visit_callback, this);
    parent_t::shutdown_start();
}

void my_2nd_visitor_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { log = utils::get_logger("my-2nd_visitor"); });
}

void my_2nd_visitor_actor_t::visit(const model::diff::cluster_diff_t &diff,
                                   model::payload::apply_context_t &) noexcept {
    LOG_INFO(log, "visit");
    auto r = diff.visit(*this, {});
    CHECK(r);
}

auto my_2nd_visitor_actor_t::operator()(const model::diff::modify::add_blocks_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    LOG_INFO(log, "visiting add_blocsk, c = {}", add_blocks_counter);
    ++add_blocks_counter;
    return model::diff::cluster_visitor_t::operator()(diff, custom);
}

struct fixture_t {
    fixture_t() noexcept { log = utils::get_logger("fixture"); }

    void run() noexcept {
        auto my_hash = "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD";
        auto my_id = device_id_t::from_string(my_hash).value();
        auto local_device = device_t::create(my_id, "my-device").value();

        cluster = new cluster_t(local_device, 1);

        cluster->get_devices().put(local_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<my_controller_t>().timeout(timeout).cluster(cluster).create_registry().finish();

        sup->start();
        sup->do_process();

        main();

        sup->do_process();
        sup->shutdown();
        sup->do_process();
    }

    virtual void main() noexcept {}

    cluster_ptr_t cluster;
    r::intrusive_ptr_t<my_controller_t> sup;
    utils::logger_t log;
    r::pt::time_duration timeout = r::pt::millisec{10};
};

// tests

void test_add_2_blocks_pure() {
    static constexpr auto N = std::size_t{3};
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main() noexcept override {

            auto make_diff = [&]() -> model::diff::cluster_diff_ptr_t {
                auto assembler = diff::diff_assember_t(2);

                for (size_t i = 0; i < N; ++i) {
                    auto data = fmt::format("{}", i);
                    auto view = as_bytes(data);
                    auto data_h = utils::sha256_digest(view).value();

                    auto b = proto::BlockInfo();
                    proto::set_hash(b, data_h);
                    proto::set_size(b, view.size());
                    assembler.push_back(new diff::modify::add_blocks_t({b}));
                }
                return assembler.consume();
            };

            auto v1 = sup->create_actor<my_visitor_actor_t>().timeout(timeout).finish();
            auto v2 = sup->create_actor<my_2nd_visitor_actor_t>().timeout(timeout).finish();
            sup->do_process();

            auto &addr = sup->get_address();
            sup->send<model::payload::model_update_t>(addr, make_diff(), nullptr);
            sup->send<model::payload::model_update_t>(addr, make_diff(), nullptr);
            sup->do_process();

            CHECK(cluster->get_blocks().size() == N);
            CHECK(v1->add_blocks_counter == N * 2);
            CHECK(v2->add_blocks_counter == N * 2);
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_add_2_blocks_pure, "test_add_2_blocks_pure", "[net]");
    return 1;
}

static int v = _init();

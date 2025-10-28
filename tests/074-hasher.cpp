// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "test_supervisor.h"
#include "hasher/hasher_actor.h"
#include "hasher/hasher_plugin.h"
#include "managed_hasher.h"
#include "utils/bytes.h"
#include <net/names.h>

namespace r = rotor;
namespace st = syncspirit::test;
namespace h = syncspirit::hasher;

using namespace syncspirit;
using namespace syncspirit::hasher;

struct hash_consumer_t : r::actor_base_t {
    r::address_ptr_t hasher;
    r::intrusive_ptr_t<message::digest_t> digest_res;
    r::intrusive_ptr_t<message::validation_t> validation_res;

    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.discover_name("hasher-1", hasher, true).link(); });
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&hash_consumer_t::on_digest);
            p.subscribe_actor(&hash_consumer_t::on_validation);
        });
    }

    void request_digest(const utils::bytes_t &data) {
        supervisor->route<payload::digest_t>(hasher, address, std::move(data), 0);
    }

    void request_validation(const utils::bytes_t &data, utils::bytes_view_t hash) {
        auto hash_bytes = utils::bytes_t(hash.begin(), hash.end());
        supervisor->route<payload::validation_t>(hasher, address, data, std::move(hash_bytes));
    }

    void on_digest(message::digest_t &res) noexcept { digest_res = &res; }

    void on_validation(message::validation_t &res) noexcept { validation_res = &res; }
};

TEST_CASE("hasher-actor", "[hasher]") {
    r::system_context_t ctx;
    auto timeout = r::pt::milliseconds{10};
    auto sup = ctx.create_supervisor<st::supervisor_t>().timeout(timeout).create_registry().finish();
    sup->start();
    sup->create_actor<hasher_actor_t>().index(1).timeout(timeout).finish();
    auto consumer = sup->create_actor<hash_consumer_t>().timeout(timeout).finish();
    sup->do_process();

    auto data = test::as_owned_bytes("abcdef");
    consumer->request_digest(data);
    sup->do_process();
    REQUIRE(consumer->digest_res);
    auto &digest = consumer->digest_res->payload.result.value();
    CHECK(digest[2] == 126);

    consumer->request_validation(data, digest);
    sup->do_process();
    REQUIRE(consumer->validation_res);
    CHECK(!consumer->validation_res->payload.result.has_error());

    sup->shutdown();
    sup->do_process();
}

TEST_CASE("hasher-plugin", "[hasher]") {
    struct consumer_t : r::actor_base_t {
        using parent_t = r::actor_base_t;
        using parent_t::parent_t;

        // clang-format off
        using plugins_list_t = std::tuple<
            r::plugin::address_maker_plugin_t,
            r::plugin::lifetime_plugin_t,
            r::plugin::init_shutdown_plugin_t,
            r::plugin::link_server_plugin_t,
            r::plugin::link_client_plugin_t,
            r::plugin::registry_plugin_t,
            r::plugin::resources_plugin_t,
            h::hasher_plugin_t,
            r::plugin::starter_plugin_t
        >;
        // clang-format on

        void configure(r::plugin::plugin_base_t &plugin) noexcept override {
            parent_t::configure(plugin);
            plugin.with_casted<h::hasher_plugin_t>([&](auto &p) { p.configure(2); });
            plugin.with_casted<r::plugin::starter_plugin_t>(
                [&](auto &p) { p.subscribe_actor(&consumer_t::on_digest); });
        }

        void on_start() noexcept override {
            parent_t::on_start();
            auto plugin = get_plugin(h::hasher_plugin_t::class_identity);
            auto hasher = static_cast<h::hasher_plugin_t *>(plugin);

            for (int i = 1; i <= 10; ++i) {
                auto data = utils::bytes_t(i, i);
                counter += i;
                hasher->calc_digest(std::move(data), 0);
            }
        }

        void on_digest(message::digest_t &res) noexcept { counter -= static_cast<int>(res.payload.data.size()); }
        int counter = 0;
    };

    r::system_context_t ctx;
    auto timeout = r::pt::milliseconds{10};
    auto sup = ctx.create_supervisor<st::supervisor_t>().timeout(timeout).create_registry().finish();
    sup->start();
    auto h1 = sup->create_actor<test::managed_hasher_t>().auto_reply().index(1).timeout(timeout).finish();
    auto h2 = sup->create_actor<test::managed_hasher_t>().auto_reply().index(2).timeout(timeout).finish();
    sup->do_process();

    auto consumer = sup->create_actor<consumer_t>().timeout(timeout).finish();
    sup->do_process();
    CHECK(consumer->counter == 0);
    CHECK(h1->digested_bytes > 0);
    CHECK(h1->digested_bytes + 5 == h2->digested_bytes);

    sup->shutdown();
    sup->do_process();
}

int _init() {
    test::init_logging();
    return 1;
}

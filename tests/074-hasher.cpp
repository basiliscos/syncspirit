// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "hasher/hasher_actor.h"
#include <ostream>
#include <fstream>
#include <net/names.h>

namespace r = rotor;
namespace st = syncspirit::test;
namespace h = syncspirit::hasher;

using namespace syncspirit::hasher;

struct hash_consumer_t : r::actor_base_t {
    r::address_ptr_t hasher;
    r::intrusive_ptr_t<message::digest_response_t> digest_res;
    r::intrusive_ptr_t<message::validation_response_t> validation_res;

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

    void request_digest(const std::string_view &data) {
        request<payload::digest_request_t>(hasher, std::string(data)).send(init_timeout);
    }

    void request_validation(const std::string_view &data, const std::string_view &hash) {
        request<payload::validation_request_t>(hasher, data, std::string(hash), nullptr).send(init_timeout);
    }

    void on_digest(message::digest_response_t &res) noexcept { digest_res = &res; }

    void on_validation(message::validation_response_t &res) noexcept { validation_res = &res; }
};

TEST_CASE("hasher-actor", "[hasher]") {
    r::system_context_t ctx;
    auto timeout = r::pt::milliseconds{10};
    auto sup = ctx.create_supervisor<st::supervisor_t>().timeout(timeout).create_registry().finish();
    sup->start();
    sup->create_actor<hasher_actor_t>().index(1).timeout(timeout).finish();
    auto consumer = sup->create_actor<hash_consumer_t>().timeout(timeout).finish();
    sup->do_process();

    std::string data = "abcdef";
    consumer->request_digest(data);
    sup->do_process();
    REQUIRE(consumer->digest_res);
    CHECK(consumer->digest_res->payload.res.weak == 136184406u);
    auto digest = consumer->digest_res->payload.res.digest;
    CHECK(digest[2] == 126);

    consumer->request_validation(data, digest);
    sup->do_process();
    REQUIRE(consumer->digest_res);
    CHECK(consumer->digest_res->payload.res.weak == 136184406u);

    sup->shutdown();
    sup->do_process();
}

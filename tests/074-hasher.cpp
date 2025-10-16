// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "test_supervisor.h"
#include "hasher/hasher_actor.h"
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
        auto msg = r::make_routed_message<payload::digest_t>(hasher, address, std::move(data));
        supervisor->put(std::move(msg));
    }

    void request_validation(const utils::bytes_t &data, utils::bytes_view_t hash) {
        auto hash_bytes = utils::bytes_t(hash.begin(), hash.end());
        auto msg = r::make_routed_message<payload::validation_t>(hasher, address, data, std::move(hash_bytes));
        supervisor->put(std::move(msg));
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

int _init() {
    test::init_logging();
    return 1;
}

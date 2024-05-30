// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include <rotor.hpp>
#include <rotor/asio.hpp>
#include "test-utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "net/resolver_actor.h"
#include "access.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::net;

namespace asio = boost::asio;
namespace sys = boost::system;
namespace r = rotor;
namespace ra = r::asio;

using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;
using response_ptr_t = r::intrusive_ptr_t<message::resolve_response_t>;

auto timeout = r::pt::time_duration{r::pt::millisec{1000}};

struct my_supervisor_t : ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    using parent_t::parent_t;
    using responses_t = std::vector<response_ptr_t>;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        ra::supervisor_asio_t::configure(plugin);
        plugin.with_casted<r::plugin::starter_plugin_t>(
            [&](auto &p) { p.subscribe_actor(&my_supervisor_t::on_resolve); });

        if (configure_callback) {
            configure_callback(plugin);
        }
    }

    void on_resolve(message::resolve_response_t &res) noexcept { responses.emplace_back(&res); }

    responses_t responses;
    configure_callback_t configure_callback;
};

using supervisor_ptr_t = r::intrusive_ptr_t<my_supervisor_t>;
using actor_ptr_t = r::intrusive_ptr_t<resolver_actor_t>;

struct fixture_t {
    fixture_t() : ctx(io_ctx), root_path{bfs::unique_path()}, path_quard{root_path} {
        utils::set_default("trace");
        log = utils::get_logger("fixture");
    }

    virtual void main() noexcept = 0;

    void run() {
        auto strand = std::make_shared<asio::io_context::strand>(io_ctx);
        sup = ctx.create_supervisor<my_supervisor_t>().strand(strand).timeout(timeout).create_registry().finish();
        sup->start();
        sup->do_process();

        resolv_conf_path = root_path / "resolv.conf";
        hosts_path = root_path / "hosts";

        main();

        sup->do_shutdown();
        io_ctx.run();
    }

    asio::io_context io_ctx{1};
    ra::system_context_asio_t ctx;
    bfs::path root_path;
    bfs::path resolv_conf_path;
    bfs::path hosts_path;
    path_guard_t path_quard;
    utils::logger_t log;
    supervisor_ptr_t sup;
    actor_ptr_t resolver;
};

void test_local_resolver() {
    struct F : fixture_t {
        void main() noexcept override {

            write_file(resolv_conf_path, "nameserver 127.0.0.1\n");
            write_file(hosts_path, "127.0.0.2 lclhst.localdomain lclhst\n");

            resolver = sup->create_actor<resolver_actor_t>()
                           .resolve_timeout(timeout / 2)
                           .hosts_path(hosts_path.c_str())
                           .resolvconf_path(resolv_conf_path.c_str())
                           .timeout(timeout)
                           .finish();

            sup->do_process();

            sup->request<payload::address_request_t>(resolver->get_address(), "lclhst", 123).send(timeout);
            sup->do_process();
            REQUIRE(sup->responses.size() == 1);

            auto &results = sup->responses.at(0)->payload.res->results;
            REQUIRE(results.size() == 1);
            REQUIRE(results.at(0) == tcp::endpoint(asio::ip::make_address("127.0.0.2"), 123));
        }
    };

    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_local_resolver, "test_local_resolver", "[resolver]");
    return 1;
}

static int v = _init();

#include "catch.hpp"
#include "test-utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "net/db_actor.h"
#include "net/messages.h"
#include "net/names.h"
#include <ostream>
#include <fstream>
#include <net/names.h>

namespace r = rotor;
namespace st = syncspirit::test;
namespace bfs = boost::filesystem;
using namespace syncspirit;
using namespace syncspirit::net;

struct db_consumer_t : r::actor_base_t {
    r::address_ptr_t target;
    r::intrusive_ptr_t<message::store_new_folder_response_t> new_folder_res;
    r::intrusive_ptr_t<message::store_file_response_t> file_res;

    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.discover_name(names::db, target, true).link(true); });
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&db_consumer_t::on_new_folder);
            p.subscribe_actor(&db_consumer_t::on_file);
        });
    }

    void on_new_folder(message::store_new_folder_response_t &res) noexcept { new_folder_res = &res; }

    void on_file(message::store_file_response_t &res) noexcept { file_res = &res; }
};

TEST_CASE("db-actor", "[db]") {
    r::system_context_t ctx;
    auto timeout = r::pt::milliseconds{10};
    auto sup = ctx.create_supervisor<st::supervisor_t>().timeout(timeout).create_registry().finish();
    sup->start();

    bfs::path root_path;
    root_path = bfs::unique_path();
    bfs::create_directory(root_path);
    auto root_path_guard = st::path_guard_t(root_path);

    db::Device db_device;
    db_device.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
    db_device.set_name("d1");
    db_device.set_cert_name("d1_cert_name");
    auto device = model::device_ptr_t(new model::device_t(db_device, 2));
    auto cluster = model::cluster_ptr_t(new model::cluster_t(device));

    auto db = sup->create_actor<net::db_actor_t>().db_dir(root_path.string()).device(device).timeout(timeout).finish();
    auto act = sup->create_actor<db_consumer_t>().timeout(timeout).finish();

    sup->do_process();
    CHECK(!db->get_shutdown_reason());
    CHECK(!act->get_shutdown_reason());

    auto folder_path = root_path / "smthg";
    db::Folder db_folder;
    db_folder.set_id("123");
    db_folder.set_path(folder_path.string());
    db_folder.set_label("lll");
    act->request<payload::store_new_folder_request_t>(db->get_address(), db_folder, nullptr, 0ul, cluster)
        .send(timeout);
    sup->do_process();
    REQUIRE(act->new_folder_res);
    REQUIRE(!act->new_folder_res->payload.ee);
    auto &folder = act->new_folder_res->payload.res.folder;
    REQUIRE(folder);

    CHECK(folder->label() == "lll");
    CHECK(folder->get_path() == folder_path);
    CHECK(folder->id() == "123");

    sup->shutdown();
    sup->do_process();
}

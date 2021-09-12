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
    r::intrusive_ptr_t<message::store_folder_info_response_t> file_info_res;
    r::intrusive_ptr_t<message::load_cluster_response_t> cluster_res;

    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.discover_name(names::db, target, true).link(true); });
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&db_consumer_t::on_new_folder);
            p.subscribe_actor(&db_consumer_t::on_file_info);
            p.subscribe_actor(&db_consumer_t::on_cluster);
        });
    }

    void on_new_folder(message::store_new_folder_response_t &res) noexcept { new_folder_res = &res; }

    void on_file_info(message::store_folder_info_response_t &res) noexcept { file_info_res = &res; }

    void on_cluster(message::load_cluster_response_t &res) noexcept { cluster_res = &res; }
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
    auto &db_addr = db->get_address();
    auto act = sup->create_actor<db_consumer_t>().timeout(timeout).finish();

    sup->do_process();
    CHECK(!db->get_shutdown_reason());
    CHECK(!act->get_shutdown_reason());

    auto folder_path = root_path / "smthg";
    db::Folder db_folder;
    db_folder.set_id("123");
    db_folder.set_path(folder_path.string());
    db_folder.set_label("lll");
    act->request<payload::store_new_folder_request_t>(db_addr, db_folder, nullptr, 0ul, cluster).send(timeout);
    sup->do_process();
    REQUIRE(act->new_folder_res);
    REQUIRE(!act->new_folder_res->payload.ee);
    auto &folder = act->new_folder_res->payload.res.folder;
    REQUIRE(folder);

    CHECK(folder->label() == "lll");
    CHECK(folder->get_path() == folder_path);
    CHECK(folder->id() == "123");

    auto fi = folder->get_folder_info(device);

    proto::FileInfo pr_fileinfo;
    pr_fileinfo.set_sequence(7);
    pr_fileinfo.set_size(5);
    pr_fileinfo.set_block_size(5);

    auto pr_block = pr_fileinfo.add_blocks();
    pr_block->set_hash("12345");
    pr_block->set_size(5);
    pr_block->set_weak_hash(8);

    auto file = model::file_info_ptr_t(new model::file_info_t(pr_fileinfo, fi.get()));
    REQUIRE(file);
    CHECK(file->get_blocks().size() == 1);
    CHECK(file->is_dirty());
    fi->add(file);
    CHECK(fi->is_dirty());
    act->request<payload::store_folder_info_request_t>(db_addr, fi).send(timeout);

    sup->do_process();
    REQUIRE(act->file_info_res);
    REQUIRE(!act->file_info_res->payload.ee);
    auto &block = *file->get_blocks().begin();
    CHECK(block->get_db_key());

    // check cluster loading
    act->request<payload::load_cluster_request_t>(db_addr).send(timeout);
    sup->do_process();
    REQUIRE(act->cluster_res);
    REQUIRE(!act->cluster_res->payload.ee);
    auto &devices = act->cluster_res->payload.res.devices;
    CHECK(devices.size() == 1);
    CHECK(devices.by_key(device->get_db_key()));
    CHECK(devices.by_id(device->get_id()));
    auto &cluster_2 = act->cluster_res->payload.res.cluster;
    REQUIRE(cluster_2->get_folders().size() == 1);

    auto &blocks = cluster_2->get_blocks();
    CHECK(blocks.size() == 1);

    auto folder_2 = cluster_2->get_folders().by_key(folder->get_db_key());
    REQUIRE(folder_2);
    CHECK(folder_2->get_db_key() == folder->get_db_key());
    CHECK(folder_2->get_path() == folder->get_path());
    CHECK(folder_2->id() == folder->id());
    CHECK(folder_2->label() == folder->label());
    REQUIRE(folder_2->get_folder_infos().size() == 1);

    auto fi_2 = folder_2->get_folder_info(device);
    REQUIRE(fi_2);
    CHECK(fi_2->get_db_key() == fi->get_db_key());
    CHECK(fi_2->get_index() == fi->get_index());
    REQUIRE(fi_2->get_file_infos().size() == 1);

    // remove block, save and load cluster
    file->remove_blocks();
    fi->mark_dirty();
    act->file_info_res.reset();
    act->request<payload::store_folder_info_request_t>(db_addr, fi).send(timeout);
    sup->do_process();
    REQUIRE(act->cluster_res);
    REQUIRE(!act->cluster_res->payload.ee);

    act->cluster_res.reset();
    act->request<payload::load_cluster_request_t>(db_addr).send(timeout);
    sup->do_process();
    REQUIRE(act->cluster_res);
    REQUIRE(!act->cluster_res->payload.ee);
    auto& cluster_3 = act->cluster_res->payload.res.cluster;
    auto& blocks_3 = cluster_3->get_blocks();
    CHECK(blocks_3.size() == 0);

    sup->shutdown();
    sup->do_process();
}

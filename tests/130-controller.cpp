#include "catch.hpp"
#include "test-utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "db/utils.h"
#include "fs/utils.h"
#include "fs/fs_actor.h"
#include "utils/log.h"
#include "net/controller_actor.h"
#include "net/db_actor.h"
#include <net/names.h>

namespace st = syncspirit::test;
namespace fs = syncspirit::fs;

using namespace syncspirit;
using namespace syncspirit::net;
using namespace syncspirit::test;
using namespace syncspirit::model;

struct sample_peer_t : r::actor_base_t {
    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);

        plugin.with_casted<r::plugin::starter_plugin_t>(
            [&](auto &p) { p.subscribe_actor(&sample_peer_t::on_start_reading); });
    }

    void on_start_reading(message::start_reading_t &) noexcept { ++start_reading; }

    size_t start_reading = 0;
};

struct Fixture;

using callback_t  = std::function<void(Fixture&)>;

struct Fixture {

    model::device_ptr_t device_my;
    model::device_ptr_t device_peer;
    cluster_ptr_t cluster;
    model::ignored_folders_map_t ignored_folders;
    r::intrusive_ptr_t<sample_peer_t> peer;
    payload::cluster_config_ptr_t peer_cluster_config;

    callback_t setup_cb;
    callback_t run_cb;

    void run() {
        std::string prompt = "> ";
        std::mutex std_out_mutex;
        utils::set_default("trace", prompt, std_out_mutex, false);

        auto root_path = bfs::unique_path();
        bfs::create_directory(root_path);
        auto root_path_guard = path_guard_t(root_path);

        std::uint64_t key = 0;
        db::Device db_my;
        db_my.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
        db_my.set_name("d1");
        db_my.set_cert_name("d1_cert_name");
        auto d_my = model::device_ptr_t(new model::device_t(db_my, ++key));

        db::Device db_peer;
        db_peer.set_id(test::device_id2sha256("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7"));
        db_peer.set_name("d2");
        db_peer.set_cert_name("d2_cert_name");
        auto d_peer = model::device_ptr_t(new model::device_t(db_peer, ++key));

        cluster = new cluster_t(d_my);
        if (setup_cb) {
            setup_cb(*this);
        }

        r::system_context_t ctx;
        auto timeout = r::pt::milliseconds{10};
        auto sup = ctx.create_supervisor<st::supervisor_t>().timeout(timeout).create_registry().finish();
        sup->start();
        sup->create_actor<fs::fs_actor_t>().fs_config({1024, 5, 0}).timeout(timeout).finish();
        sup->create_actor<db_actor_t>().db_dir((root_path / "db").string()).device(d_my).timeout(timeout).finish();
        peer = sup->create_actor<sample_peer_t>().timeout(timeout).finish();
        sup->do_process();

        auto controller = sup->create_actor<controller_actor_t>()
                              .cluster(cluster)
                              .peer_addr(peer->get_address())
                              .peer(d_peer)
                              .request_timeout(timeout)
                              .peer_cluster_config(std::move(peer_cluster_config))
                              .ignored_folders(&ignored_folders)
                              .timeout(timeout)
                              .finish();
        sup->do_process();
        auto reason = controller->get_shutdown_reason();
        if (reason) {
            spdlog::warn("shutdown reason = {}", reason->message());
        };
        REQUIRE(!reason);
        run_cb(*this);

        sup->shutdown();
        sup->do_process();
    }

};

void test_start_reading() {

    Fixture f;
    f.setup_cb = [](Fixture& f) {
        f.peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig());
    };
    f.run_cb = [](Fixture& f) {
        CHECK(f.peer->start_reading == 1);
    };
    f.run();
}

REGISTER_TEST_CASE( test_start_reading, "test_start_reading", "[controller]" );

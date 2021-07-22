#include "catch.hpp"
#include "test-utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "db/utils.h"
#include "fs/utils.h"
#include "fs/fs_actor.h"
#include "ui/messages.hpp"
#include "utils/log.h"
#include "utils/tls.h"
#include "net/controller_actor.h"
#include "net/db_actor.h"
#include <net/names.h>

namespace st = syncspirit::test;
namespace fs = syncspirit::fs;

using namespace syncspirit;
using namespace syncspirit::net;
using namespace syncspirit::test;
using namespace syncspirit::model;

struct Fixture;
using callback_t = std::function<void(Fixture &)>;
using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;

struct sample_peer_t : r::actor_base_t {
    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);

        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&sample_peer_t::on_start_reading);
            p.subscribe_actor(&sample_peer_t::on_block_request);
        });
        if (configure_callback) {
            configure_callback(plugin);
        }
    }

    void on_start_reading(message::start_reading_t &) noexcept { ++start_reading; }

    void on_block_request(message::block_request_t &req) noexcept {
        assert(responses.size());
        reply_to(req, responses.front());
        responses.pop_front();
    }

    using Responses = std::list<std::string>;

    Responses responses;
    size_t start_reading = 0;
    configure_callback_t configure_callback;
};

struct Fixture {

    model::device_ptr_t device_my;
    model::device_ptr_t device_peer;
    cluster_ptr_t cluster;
    model::ignored_folders_map_t ignored_folders;
    r::intrusive_ptr_t<sample_peer_t> peer;
    r::intrusive_ptr_t<st::supervisor_t> sup;
    r::intrusive_ptr_t<net::controller_actor_t> controller;
    payload::cluster_config_ptr_t peer_cluster_config;
    bfs::path root_path;

    callback_t setup_cb;
    callback_t prerun_cb;
    callback_t run_cb;

    void run() {
        std::string prompt = "> ";
        std::mutex std_out_mutex;
        utils::set_default("trace", prompt, std_out_mutex, false);

        root_path = bfs::unique_path();
        bfs::create_directory(root_path);
        auto root_path_guard = path_guard_t(root_path);

        std::uint64_t key = 0;
        db::Device db_my;
        db_my.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
        db_my.set_name("d1");
        db_my.set_cert_name("d1_cert_name");
        device_my = model::device_ptr_t(new model::device_t(db_my, ++key));

        db::Device db_peer;
        db_peer.set_id(test::device_id2sha256("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7"));
        db_peer.set_name("d2");
        db_peer.set_cert_name("d2_cert_name");
        device_peer = model::device_ptr_t(new model::device_t(db_peer, ++key));

        cluster = new cluster_t(device_my);
        if (setup_cb) {
            setup_cb(*this);
        }

        r::system_context_t ctx;
        auto timeout = r::pt::milliseconds{10};
        sup = ctx.create_supervisor<st::supervisor_t>().timeout(timeout).create_registry().finish();
        sup->start();
        sup->create_actor<fs::fs_actor_t>().fs_config({1024, 5, 0}).timeout(timeout).finish();
        sup->create_actor<db_actor_t>().db_dir((root_path / "db").string()).device(device_my).timeout(timeout).finish();
        peer = sup->create_actor<sample_peer_t>().timeout(timeout).finish();
        if (prerun_cb) {
            prerun_cb(*this);
        }
        sup->do_process();

        controller = sup->create_actor<controller_actor_t>()
                         .cluster(cluster)
                         .device(device_my)
                         .peer_addr(peer->get_address())
                         .peer(device_peer)
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
    f.setup_cb = [](Fixture &f) { f.peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig()); };
    f.run_cb = [](Fixture &f) { CHECK(f.peer->start_reading == 1); };
    f.run();
}

void test_new_folder() {
    using notify_t = ui::message::new_folder_notify_t;
    using notify_ptr_t = r::intrusive_ptr_t<notify_t>;
    using cluster_msg_t = r::intrusive_ptr_t<message::cluster_config_t>;

    notify_ptr_t notify;
    cluster_msg_t cluster_msg;
    Fixture f;
    auto p_folder = proto::Folder();

    f.setup_cb = [&](Fixture &f) {
        auto config = proto::ClusterConfig();
        auto folder = proto::Folder();
        folder.set_label("my-folder");
        folder.set_id("123");

        auto d_my = proto::Device();
        d_my.set_id(f.device_my->device_id.get_sha256());
        d_my.set_index_id(21);
        d_my.set_max_sequence(0);

        auto d_peer = proto::Device();
        d_my.set_id(f.device_peer->device_id.get_sha256());
        d_my.set_index_id(22);
        d_my.set_max_sequence(1);

        *folder.add_devices() = d_my;
        *folder.add_devices() = d_peer;
        *config.add_folders() = folder;
        f.peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig(config));
        p_folder = folder;
    };
    f.prerun_cb = [&](Fixture &f) {
        f.peer->configure_callback = [&](auto &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<notify_t>([&](notify_t &msg) { notify = &msg; }), f.sup->get_address());
                p.subscribe_actor(
                    r::lambda<message::cluster_config_t>([&](message::cluster_config_t &msg) { cluster_msg = &msg; }));
            });
        };
    };
    f.run_cb = [&](Fixture &f) {
        CHECK(f.peer->start_reading == 1);
        REQUIRE(notify);
        CHECK(notify->payload.folder.label() == "my-folder");
        CHECK(notify->payload.folder.id() == "123");
        CHECK(notify->payload.source == f.device_peer);
        CHECK(notify->payload.source_index == 22);

        auto path = f.root_path / "my-folder";
        auto db_folder = db::Folder();
        db_folder.set_id(p_folder.id());
        db_folder.set_label(p_folder.label());
        db_folder.set_path(path.string());

        auto folder = model::folder_ptr_t(new model::folder_t(db_folder));
        f.cluster->get_folders().put(folder);
        folder->assign_device(f.device_my);
        folder->assign_cluster(f.cluster.get());

        auto db_fi_local = db::FolderInfo();
        db_fi_local.set_index_id(234);
        auto fi_local =
            model::folder_info_ptr_t(new model::folder_info_t(db_fi_local, f.device_my.get(), folder.get(), 2));

        auto db_fi_source = db::FolderInfo();
        db_fi_source.set_index_id(345);
        db_fi_source.set_max_sequence(5);
        auto fi_source =
            model::folder_info_ptr_t(new model::folder_info_t(db_fi_source, f.device_peer.get(), folder.get(), 3));

        folder->add(fi_local);
        folder->add(fi_source);

        f.peer->send<payload::store_new_folder_notify_t>(f.controller->get_address(), folder);
        f.sup->do_process();

        REQUIRE(cluster_msg);

        // TODO: test for wrong block hash
        auto block_info = proto::BlockInfo();
        block_info.set_size(5);
        block_info.set_hash(utils::sha256_digest("12345").value());

        auto fi = proto::FileInfo();
        fi.set_name("a.txt");
        fi.set_type(proto::FileInfoType::FILE);
        fi.set_sequence(5);
        fi.set_block_size(5);
        fi.set_size(5);
        *fi.add_blocks() = block_info;

        proto::Index index;
        index.set_folder(folder->id());
        *index.add_files() = fi;

        auto index_ptr = proto::message::Index(std::make_unique<proto::Index>(std::move(index)));
        f.peer->send<payload::forwarded_message_t>(f.controller->get_address(), std::move(index_ptr));
        f.peer->responses.push_back("12345");
        f.sup->do_process();
        CHECK(st::read_file(path / "a.txt") == "12345");
    };
    f.run();
}

REGISTER_TEST_CASE(test_start_reading, "test_start_reading", "[controller]");
REGISTER_TEST_CASE(test_new_folder, "test_new_folder", "[controller]");

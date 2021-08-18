#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "fs/fs_actor.h"
#include "net/db_actor.h"
#include "net/cluster_supervisor.h"
#include "net/names.h"
#include "ui/messages.hpp"
#include "utils/error_code.h"
#include "utils/log.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

using supervisor_t = rotor::asio::supervisor_asio_t;

namespace {

using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;

struct sample_coordinator_t : r::actor_base_t {
    using message_t = message::cluster_ready_notify_t;
    using message_ptr_t = r::intrusive_ptr_t<message_t>;
    using r::actor_base_t::actor_base_t;

    message_ptr_t cluster_ready;
    configure_callback_t configure_callback;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.register_name(names::coordinator, get_address()); });
        plugin.with_casted<r::plugin::starter_plugin_t>(
            [&](auto &p) { p.subscribe_actor(&sample_coordinator_t::on_cluster_ready); });
        if (configure_callback) {
            configure_callback(plugin);
        }
    }

    void on_cluster_ready(message::cluster_ready_notify_t &msg) noexcept { cluster_ready = &msg; }
};

struct Fixture {
    using coordinator_ptr_t = r::intrusive_ptr_t<sample_coordinator_t>;

    model::device_ptr_t device_my;
    r::intrusive_ptr_t<supervisor_t> sup;
    coordinator_ptr_t coord;
    r::address_ptr_t cluster_addr;
    bfs::path root_path;
    model::cluster_ptr_t cluster;
    r::pt::time_duration timeout = r::pt::millisec{10};

    Fixture() { utils::set_default("trace"); }
    void run() {
        root_path = bfs::unique_path();
        bfs::create_directory(root_path);
        auto root_path_guard = path_guard_t(root_path);

        std::uint64_t key = 0;
        db::Device db_my;
        db_my.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
        db_my.set_name("d1");
        db_my.set_cert_name("d1_cert_name");
        device_my = model::device_ptr_t(new model::device_t(db_my, ++key));
        cluster = new cluster_t(device_my);

        asio::io_context io_context{1};
        ra::system_context_asio_t ctx(io_context);
        auto strand = std::make_shared<asio::io_context::strand>(io_context);
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).strand(strand).create_registry().finish();
        sup->start();
        coord = sup->create_actor<sample_coordinator_t>().timeout(timeout).finish();
        sup->create_actor<fs::fs_actor_t>().fs_config({1024, 5, 0}).timeout(timeout).finish();
        sup->create_actor<db_actor_t>().db_dir((root_path / "db").string()).device(device_my).timeout(timeout).finish();

        pre_run();
        sup->do_process();

        auto reason = sup->get_shutdown_reason();
        if (reason) {
            spdlog::warn("shutdown reason = {}", reason->message());
        };
        REQUIRE(!reason);
        model::devices_map_t devices;
        devices.put(device_my);
        model::ignored_folders_map_t ignored_folders;
        auto cluster_sup = sup->create_actor<cluster_supervisor_t>()
                               .timeout(timeout)
                               .strand(strand)
                               .device(device_my)
                               .devices(&devices)
                               .ignored_folders(&ignored_folders)
                               .cluster(cluster)
                               .bep_config(config::bep_config_t{})
                               .finish();
        cluster_addr = cluster_sup->get_address();

        sup->do_process();
        reason = cluster_sup->get_shutdown_reason();
        if (reason) {
            spdlog::warn("shutdown reason = {}", reason->message());
        };
        REQUIRE(!reason);
        auto &state = static_cast<r::actor_base_t *>(sup.get())->access<to::state>();
        REQUIRE(state == r::state_t::OPERATIONAL);
        main();

        sup->shutdown();
        sup->do_process();
        io_context.run(); // avoid mem leaks
    }
    virtual void pre_run(){};
    virtual void main(){};
};
} // namespace

void test_start_empty_cluster() {
    struct F : Fixture {
        void main() override { REQUIRE(coord->cluster_ready); }
    };
    F().run();
}
void test_add_new_folder() {
    using request_t = ui::message::create_folder_request_t;
    using response_t = ui::message::create_folder_response_t;
    using response_ptr_t = r::intrusive_ptr_t<response_t>;

    struct F : Fixture {
        response_ptr_t res;

        void pre_run() override {
            coord->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
                plugin.template with_casted<r::plugin::starter_plugin_t>(
                    [&](auto &p) { p.subscribe_actor(r::lambda<response_t>([&](response_t &msg) { res = &msg; })); });
            };
        }
        void main() override {
            REQUIRE(coord->cluster_ready);
            REQUIRE(cluster->get_folders().size() == 0);
            auto new_dir = root_path / "new_dir";
            auto new_file = new_dir / "some-file.txt";
            bfs::create_directory(new_dir);
            write_file(new_file, "zzz");

            db::Folder folder;
            folder.set_path(new_dir.string());
            folder.set_id("1235");
            folder.set_label("my-label");

            coord->request<ui::payload::create_folder_request_t>(cluster_addr, folder).send(timeout);
            sup->do_process();

            REQUIRE(res);
            REQUIRE(!res->payload.ee);
            REQUIRE(cluster->get_folders().size() == 1);
        }
    };
    F().run();
}

REGISTER_TEST_CASE(test_start_empty_cluster, "test_start_empty_cluster", "[cluster]");
REGISTER_TEST_CASE(test_add_new_folder, "test_add_new_folder", "[cluster]");

#include "sample_db.h"
#include <net/names.h>

using namespace syncspirit::test;

void sample_db_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);

    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(net::names::db, get_address()); });
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        log = utils::get_logger("sample.db");
        p.set_identity("sample.db", false);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&sample_db_t::on_store_folder_info);
        p.subscribe_actor(&sample_db_t::on_store_file);
    });
}

void sample_db_t::on_store_folder_info(message::store_folder_info_request_t &message) noexcept {
    auto &fi = message.payload.request_payload.folder_info;
    LOG_TRACE(log, "{}, on_store_folder_info folder_info = {}", identity, fi->get_db_key());
    assert(fi->is_dirty());
    save(fi);
    fi->unmark_dirty();
    reply_to(message);
}

void sample_db_t::save(model::folder_info_ptr_t &folder_info) noexcept {
    auto &folder = *folder_info->get_folder();
    auto &cluster = *folder.get_cluster();
    auto &blocks_map = cluster.get_blocks();
    auto file_infos = model::file_infos_map_t();
    for (auto &it : folder_info->get_file_infos()) {
        auto &fi = it.second;
        if (!fi->is_dirty()) {
            continue;
        }

        for (auto &block : fi->get_blocks()) {
            if (!block->is_dirty()) {
                continue;
            }
            block->unmark_dirty();
            blocks_map.put(block);
        }

        fi->unmark_dirty();
        file_infos.put(fi);
    }

    auto &native_file_infos = folder_info->get_file_infos();
    for (auto it : file_infos) {
        native_file_infos.put(it.second);
    }

    cluster.get_deleted_blocks().clear();
    folder.get_folder_infos().put(folder_info);
}

void sample_db_t::on_store_file(message::store_file_request_t &message) noexcept {
    auto &file = message.payload.request_payload.file;
    LOG_DEBUG(log, "{}, on_store_file, file = {}", identity, file->get_full_name());
    assert(file->is_dirty() && "file should be marked dirty");
    file->unmark_dirty();

    auto folder_info = file->get_folder_info();
    if (folder_info->is_dirty()) {
        folder_info->unmark_dirty();
    }

    auto &folder = *folder_info->get_folder();
    auto &cluster = *folder.get_cluster();
    auto &deleted_blocks_map = cluster.get_deleted_blocks();
    deleted_blocks_map.clear();
    auto &map = folder_info->get_file_infos();
    map.put(file);

    reply_to(message);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "folder_infos.h"
#include "model/diff/apply_controller.h"
#include "model/misc/error_code.h"
#include "model/cluster.h"
#include <tuple>
#include <list>
#include <memory_resource>

using namespace syncspirit::model::diff::load;

auto folder_infos_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    using loader_t = model::entity_t::bulk_loader_t;
    using item_t = std::tuple<model::folder_t*, model::device_t*, loader_t*, std::size_t>;
    using loaders_t = std::pmr::list<loader_t>;
    using loaders_map_t = std::pmr::unordered_map<model::folder_t*, loader_t*>;
    auto &cluster = controller.get_cluster();
    auto &folders = cluster.get_folders();
    auto &devices = cluster.get_devices();

    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto folders_queue = std::pmr::vector<item_t>(allocator);
    auto loaders_map = loaders_map_t(allocator);
    auto loaders = loaders_t(allocator);
    folders_queue.reserve(container.size());

    for (std::size_t i = 0; i < container.size(); ++i) {
        auto& item = container[i];
        auto decomposed = folder_info_t::decompose_key(item.key);
        auto device = devices.by_key(decomposed.device_key());
        if (!device) {
            return make_error_code(error_code_t::no_such_device);
        }

        auto folder = folders.by_key(decomposed.folder_key());
        if (!folder) {
            return make_error_code(error_code_t::no_such_folder);
        }
        auto& loader = loaders_map[folder.get()];
        if (!loader) {
            loader = &loaders.emplace_back(folder->get_loader());
        }
        folders_queue.emplace_back(folder.get(), device.get(), loader, i);
    }

    for (std::size_t i = 0; i < folders_queue.size(); ++i) {
        auto [folder, device, loader, index] = folders_queue[i];
        auto& item = container[index];
        auto option = folder_info_t::create(item.key, item.db_fi, device, folder);
        if (!option) {
            return option.assume_error();
        }
        auto &fi = option.assume_value();
        loader->add(fi.get());
    }
    return applicator_t::apply_sibling(controller, custom);
}

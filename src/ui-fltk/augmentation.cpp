// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "augmentation.h"
#include "tree_item.h"

#include <boost/nowide/convert.hpp>

namespace syncspirit::fltk {

augmentation_t::augmentation_t(tree_item_t *owner_) : owner{owner_} {}

void augmentation_t::on_update() noexcept {
    if (owner) {
        owner->on_update();
    }
}

void augmentation_t::on_delete() noexcept {
    if (owner) {
        owner->on_delete();
    }
}

void augmentation_t::release_onwer() noexcept { owner = nullptr; }

tree_item_t *augmentation_t::get_owner() noexcept { return owner; }

augmentation_proxy_t::augmentation_proxy_t(augmentation_ptr_t backend_) : backend{backend_} {}

void augmentation_proxy_t::on_update() noexcept { backend->on_update(); }
void augmentation_proxy_t::on_delete() noexcept {
    // no-op, owner should receive own on-delete event
}

void augmentation_proxy_t::release_onwer() noexcept {
    // no-op, owner should receive own on-delete event
}

tree_item_t *augmentation_proxy_t::get_owner() noexcept { return backend->get_owner(); }

using nc_t = augmentation_entry_base_t::name_comparator_t;
using fc_t = augmentation_entry_base_t::file_comparator_t;

bool nc_t::operator()(const ptr_t &lhs, const ptr_t &rhs) const {
    auto ld = lhs->file.is_dir();
    auto rd = rhs->file.is_dir();
    if (ld && !rd) {
        return true;
    } else if (rd && !ld) {
        return false;
    }
    return lhs->get_own_name() < rhs->get_own_name();
}

bool nc_t::operator()(const ptr_t &lhs, const std::string_view rhs) const {
    auto ld = lhs->file.is_dir();
    if (!ld) {
        return false;
    }
    return lhs->get_own_name() < rhs;
}

bool nc_t::operator()(const std::string_view lhs, const ptr_t &rhs) const {
    auto rd = rhs->file.is_dir();
    if (!rd) {
        return true;
    }
    return lhs < rhs->get_own_name();
}

bool fc_t::operator()(const file_t *lhs, const file_t *rhs) const {
    auto ld = lhs->is_dir();
    auto rd = rhs->is_dir();
    if (ld && !rd) {
        return true;
    } else if (rd && !ld) {
        return false;
    }
    return lhs->get_name() < rhs->get_name();
}

augmentation_entry_base_t::augmentation_entry_base_t(self_t *parent_, dynamic_item_t *owner_, std::string own_name_)
    : parent_t(owner_), parent{parent_}, own_name{std::move(own_name_)} {}

augmentation_entry_base_t::~augmentation_entry_base_t() { safe_delete(); }

auto augmentation_entry_base_t::get_children() noexcept -> children_t & { return children; }

void augmentation_entry_base_t::display() noexcept {}

auto augmentation_entry_base_t::get_parent() -> self_t * { return parent; }

void augmentation_entry_base_t::on_delete() noexcept {
    safe_delete();
    parent_t::on_delete();
}

void augmentation_entry_base_t::on_update() noexcept {
    record_diff();
    auto node = this;
    while (node && !node->owner) {
        node = node->parent;
    }
    auto &sup = node->owner->supervisor;
    node = this;
    while (node) {
        sup.postpone_update(*node);
        node = node->parent;
    }
}

void augmentation_entry_base_t::apply_update() {
    if (stats_diff.entries || stats_diff.entries_size || stats_diff.scanned_entries) {
        push_diff_up();
    }
    parent_t::on_update();
}

void augmentation_entry_base_t::reset_stats() {
    stats.scanned_entries = 0;
    stats.local_mark = false;

    stats_diff.scanned_entries = 0;
    stats_diff.local_mark = false;
}

void augmentation_entry_base_t::push_diff_up() {
    auto seq = stats_diff.sequence;
    stats.sequence = seq;
    stats.entries += stats_diff.entries;
    stats.scanned_entries += stats_diff.scanned_entries;
    stats.entries_size += stats_diff.entries_size;

    if (parent) {
        parent->stats_diff.entries += stats_diff.entries;
        parent->stats_diff.scanned_entries += stats_diff.scanned_entries;
        parent->stats_diff.entries_size += stats_diff.entries_size;
    }

    stats_diff = {};
    stats_diff.sequence = seq;
}

std::string_view augmentation_entry_base_t::get_own_name() { return own_name; }

const entry_stats_t &augmentation_entry_base_t::get_stats() const { return stats; }

void augmentation_entry_base_t::safe_delete() {
    if (parent) {
        auto self = static_cast<augmentation_entry_t *>(this);
        auto ref = augmentation_entry_ptr_t(self, false);
        parent->children.erase(ref);
        ref.detach();
        parent = nullptr;
    }
    for (auto &it : children) {
        it->parent = nullptr;
    }
    children.clear();
}

augmentation_entry_root_t::augmentation_entry_root_t(model::folder_info_t &folder_, dynamic_item_t *owner_)
    : parent_t(nullptr, owner_, {}), folder{folder_} {

    auto files = files_t();
    for (auto &it : folder.get_file_infos()) {
        auto file = it.item.get();
        files.emplace(file);
    }
    for (auto file : files) {
        auto path = bfs::path(boost::nowide::widen(file->get_name()));
        auto parent = (self_t *)(this);
        auto p_it = path.begin();
        auto count = std::distance(p_it, path.end());
        for (decltype(count) i = 0; i < count - 1; ++i, ++p_it) {
            auto name = boost::nowide::narrow(p_it->wstring());
            auto name_view = std::string_view(name);
            auto c_it = parent->children.find(name_view);
            parent = c_it->get();
        }
        auto own_name = p_it->string();
        auto child = augmentation_entry_ptr_t(new augmentation_entry_t(parent, *file, own_name));
        parent->children.emplace(std::move(child));
    }
    on_update();
}

void augmentation_entry_root_t::track(model::file_info_t &file) {
    assert(!file.get_augmentation());
    pending_augmentation.emplace(&file);
}

void augmentation_entry_root_t::augment_pending() {
    using nodes_t = std::set<augmentation_entry_base_t *>;
    auto updated_parents = nodes_t{};
    for (auto it = pending_augmentation.begin(); it != pending_augmentation.end();) {
        auto &file = **it;
        auto path = bfs::path(boost::nowide::widen(file.get_name()));
        auto host = (self_t *)(this);
        auto p_it = path.begin();
        auto count = std::distance(p_it, path.end());
        for (decltype(count) i = 0; i + 1 < count; ++i, ++p_it) {
            auto name = boost::nowide::narrow(p_it->wstring());
            auto name_view = std::string_view(name);
            auto c_it = host->children.find(name_view);
            if (c_it == host->children.end()) {
                host = {};
                break;
            }
            host = c_it->get();
        }
        if (host) {
            auto own_name = p_it->string();
            auto child = augmentation_entry_ptr_t(new augmentation_entry_t(host, file, own_name));
            host->children.emplace(child);
            updated_parents.emplace(host);
            it = pending_augmentation.erase(it);
        } else {
            ++it;
        }
    }
    for (auto item : updated_parents) {
        if (auto owner = item->get_owner(); owner) {
            static_cast<dynamic_item_t *>(owner)->refresh_children();
        }
    }
}

model::folder_info_t *augmentation_entry_root_t::get_folder() const { return &folder; }

auto augmentation_entry_root_t::get_file() const -> model::file_info_t * { return nullptr; }

int augmentation_entry_root_t::get_position(bool) { return 0; }

void augmentation_entry_root_t::record_diff() {}

augmentation_entry_t::augmentation_entry_t(self_t *parent, model::file_info_t &file_, std::string own_name_)
    : parent_t(parent, nullptr, std::move(own_name_)), file{file_} {
    file.set_augmentation(this);
    on_update();
}

void augmentation_entry_t::display() noexcept {
    if (!owner) {
        if (!parent->owner) {
            parent->display();
        }
        owner = static_cast<dynamic_item_t *>(parent->owner)->create(*this);
    }
    return;
}

int augmentation_entry_t::get_position(bool include_deleted) {
    auto &container = parent->children;
    int position = 0;
    for (auto &it : container) {
        if (it.get() == this) {
            break;
        }
        if (include_deleted || !it->file.is_deleted()) {
            ++position;
        }
    }
    return position;
}

auto augmentation_entry_t::get_file() const -> model::file_info_t * { return &file; }

auto augmentation_entry_t::get_folder() const -> model::folder_info_t * { return file.get_folder_info(); }

void augmentation_entry_t::record_diff() {
    if (!stats.local_mark && file.is_local()) {
        ++stats_diff.scanned_entries;
        stats.local_mark = true;
    }
    if (stats.sequence != file.get_sequence()) {
        stats_diff.entries_size += file.get_size() - stats.entries_size;
        stats_diff.entries += (!stats.sequence ? 1 : 0);
        stats_diff.sequence = stats.sequence = file.get_sequence();
    }
}

} // namespace syncspirit::fltk

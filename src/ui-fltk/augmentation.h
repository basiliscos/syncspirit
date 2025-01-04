// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "model/misc/augmentation.hpp"
#include "model/file_info.h"

#include <map>

namespace syncspirit::fltk {

struct tree_item_t;
struct dynamic_item_t;

struct augmentation_base_t : model::augmentation_t {
    virtual tree_item_t *get_owner() noexcept = 0;
    virtual void release_onwer() noexcept = 0;
};

using augmentation_ptr_t = model::intrusive_ptr_t<augmentation_base_t>;

struct augmentation_t : augmentation_base_t {
    augmentation_t(tree_item_t *owner);

    void on_update() noexcept override;
    void on_delete() noexcept override;
    void release_onwer() noexcept override;
    tree_item_t *get_owner() noexcept override;

  protected:
    tree_item_t *owner;
};

struct augmentation_proxy_t final : augmentation_base_t {
    augmentation_proxy_t(augmentation_ptr_t backend);

    void on_update() noexcept override;
    void on_delete() noexcept override;
    void release_onwer() noexcept override;
    tree_item_t *get_owner() noexcept override;

  private:
    augmentation_ptr_t backend;
};

struct augmentation_entry_base_t;
struct augmentation_entry_t;
struct augmentation_entry_root_t;
using augmentation_entry_ptr_t = model::intrusive_ptr_t<augmentation_entry_t>;

struct augmentation_entry_base_t : augmentation_t {
    using parent_t = augmentation_t;
    using self_t = augmentation_entry_base_t;
    using ptr_t = augmentation_entry_ptr_t;
    struct name_comparator_t {
        using is_transparent = std::true_type;
        bool operator()(const ptr_t &lhs, const ptr_t &rhs) const;
        bool operator()(const ptr_t &lhs, const std::string_view rhs) const;
        bool operator()(const std::string_view lhs, const ptr_t &rhs) const;
    };
    struct file_comparator_t {
        using file_t = model::file_info_t;
        bool operator()(const file_t *lhs, const file_t *rhs) const;
    };

    using children_t = std::set<augmentation_entry_ptr_t, name_comparator_t>;

    ~augmentation_entry_base_t();

    virtual void display() noexcept;
    children_t &get_children() noexcept;
    std::string_view get_own_name();
    self_t *get_parent();

    virtual model::file_info_t *get_file() = 0;
    virtual model::folder_info_t *get_folder() = 0;
    virtual int get_position(bool include_deleted) = 0;

  protected:
    augmentation_entry_base_t(self_t *parent, dynamic_item_t *owner, std::string own_name);

    std::string own_name;
    self_t *parent;
    children_t children;

    friend struct augmentation_entry_root_t;
    friend struct augmentation_entry_t;
};

struct augmentation_entry_root_t final : augmentation_entry_base_t {
    using parent_t = augmentation_entry_base_t;
    using files_t = std::set<model::file_info_t *, file_comparator_t>;
    augmentation_entry_root_t(model::folder_info_t &folder, dynamic_item_t *owner);

    void track(model::file_info_t &file);
    void augment_pending();
    model::folder_info_t *get_folder() override;
    model::file_info_t *get_file() override;
    int get_position(bool include_deleted) override;

    model::folder_info_t &folder;
    files_t pending_augmentation;
};

struct augmentation_entry_t final : augmentation_entry_base_t {
    using parent_t = augmentation_entry_base_t;
    augmentation_entry_t(self_t *parent, model::file_info_t &file, std::string own_name);

    void display() noexcept override;
    model::folder_info_t *get_folder() override;
    model::file_info_t *get_file() override;
    int get_position(bool include_deleted) override;

  private:
    model::file_info_t &file;

    friend struct name_comparator_t;
};

} // namespace syncspirit::fltk

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "folder.h"
#include "ignored_folder.h"
#include "db/prefix.h"
#include "proto/proto-helpers.h"
#include "misc/error_code.h"

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::ignored_folder);

outcome::result<ignored_folder_ptr_t> ignored_folder_t::create(std::string_view id, std::string_view label) noexcept {
    auto ptr = ignored_folder_ptr_t();
    ptr = new ignored_folder_t(id, label);
    return outcome::success(std::move(ptr));
}

outcome::result<ignored_folder_ptr_t> ignored_folder_t::create(utils::bytes_view_t key, utils::bytes_view_t data) noexcept {
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_ignored_folder_prefix);
    }

    auto ptr = ignored_folder_ptr_t();
    ptr = new ignored_folder_t(key);

    auto r = ptr->assign_fields(data);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

ignored_folder_t::ignored_folder_t(std::string_view folder_id, std::string_view label_) noexcept {
    key.resize(folder_id.size() + 1);
    key[0] = prefix;
    auto ptr = (unsigned char*)folder_id.data();
    std::copy(ptr, ptr + folder_id.size(), key.data() + 1);
    label = label_;
}

ignored_folder_t::ignored_folder_t(utils::bytes_view_t key_) noexcept {
    key = utils::bytes_t(key_.data(), key_.data() + key_.size());
}

outcome::result<void> ignored_folder_t::assign_fields(utils::bytes_view_t data) noexcept {
    db::IgnoredFolder folder;
    if (auto left = db::decode(data, folder); left) {
        return make_error_code(error_code_t::ignored_folder_deserialization_failure);
    }
    label = std::string(db::get_label(folder));
    return outcome::success();
}

utils::bytes_view_t ignored_folder_t::get_key() const noexcept { return key; }

utils::bytes_view_t ignored_folder_t::get_id() const noexcept { return {key.data() + 1, key.size() - 1}; }

std::string_view ignored_folder_t::get_label() const noexcept { return label; }

utils::bytes_t ignored_folder_t::serialize() noexcept {
    db::IgnoredFolder r;
    db::set_label(r, label);
    return db::encode(r);
}

template <> SYNCSPIRIT_API utils::bytes_view_t get_index<0>(const ignored_folder_ptr_t &item) noexcept {
    return item->get_id();
}

ignored_folder_ptr_t ignored_folders_map_t::by_key(utils::bytes_view_t key) const noexcept {
    return get<0>(key);
}


} // namespace syncspirit::model

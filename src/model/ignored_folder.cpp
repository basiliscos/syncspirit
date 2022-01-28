#include "folder.h"
#include "ignored_folder.h"
#include "../db/prefix.h"
#include "structs.pb.h"
#include "misc/error_code.h"

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::ignored_folder);

outcome::result<ignored_folder_ptr_t> ignored_folder_t::create(std::string folder_id, std::string_view label) noexcept {
    auto ptr = ignored_folder_ptr_t();
    ptr = new ignored_folder_t(std::move(folder_id), label);
    return outcome::success(std::move(ptr));
}

outcome::result<ignored_folder_ptr_t> ignored_folder_t::create(std::string_view key, std::string_view data) noexcept {
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
    std::copy(folder_id.begin(), folder_id.end(), key.data() + 1);
    label = label_;
}

ignored_folder_t::ignored_folder_t(std::string_view key_) noexcept { key = key_; }

outcome::result<void> ignored_folder_t::assign_fields(std::string_view data) noexcept {
    db::IgnoredFolder folder;
    auto ok = folder.ParseFromArray(data.data(), data.size());
    if (!ok) {
        return make_error_code(error_code_t::ignored_device_deserialization_failure);
    }
    label = folder.label();
    return outcome::success();
}

std::string_view ignored_folder_t::get_key() const noexcept { return key; }

std::string_view ignored_folder_t::get_id() const noexcept { return std::string_view(key.data() + 1, key.size() - 1); }

std::string_view ignored_folder_t::get_label() const noexcept { return label; }

std::string ignored_folder_t::serialize() noexcept {
    char c = prefix;
    db::IgnoredFolder r;
    r.set_label(label);
    return r.SerializeAsString();
}

template <> std::string_view get_index<0, ignored_folder_ptr_t>(const ignored_folder_ptr_t &item) noexcept {
    return item->get_id();
}

} // namespace syncspirit::model

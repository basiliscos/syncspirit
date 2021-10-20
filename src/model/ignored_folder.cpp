#include "ignored_folder.h"
#include "../db/prefix.h"
#include "structs.pb.h"

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::ignored_folder);

ignored_folder_t::ignored_folder_t(std::string &&folder_id, std::string_view label_) noexcept {
    key.resize(folder_id.size() +1);
    key[0] = prefix;
    std::copy(folder_id.begin(), folder_id.end(), key.data() + 1);
    label = label_;
}

ignored_folder_t::ignored_folder_t(std::string_view key_, std::string_view data) noexcept {
    assert(key_[0] == prefix);
    key.resize(key_.size());
    std::copy(key_.begin(), key_.end(), key.data());

    db::IgnoredFolder folder;
    auto ok = folder.ParseFromArray(data.data(), data.size());
    assert(ok);
    (void)ok;
    label = folder.label();
}

std::string_view ignored_folder_t::get_key() const noexcept {
    return key;
}

std::string_view ignored_folder_t::get_id() const noexcept {
    return std::string_view(key.data() + 1, key.size() - 1);
}

std::string_view ignored_folder_t::get_label() const noexcept {
    return label;
}


std::string ignored_folder_t::serialize() noexcept {
    char c = prefix;
    db::IgnoredFolder r;
    r.set_label(label);
    return r.SerializeAsString();
}

template<>
std::string_view get_index<0, ignored_folder_ptr_t>(const ignored_folder_ptr_t& item) noexcept {
    return item->get_id();
}


}

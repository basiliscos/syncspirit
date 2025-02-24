// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "folder.h"
#include "db/utils.h"
#include "db/prefix.h"
#include "proto/proto-structs.h"
#include "misc/error_code.h"
#include "utils/format.hpp"
#include <spdlog/spdlog.h>

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::folder);

outcome::result<folder_ptr_t> folder_t::create(utils::bytes_view_t key, const db::Folder &folder) noexcept {
    if (key.size() != data_length) {
        return make_error_code(error_code_t::invalid_folder_key_length);
    }
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_folder_prefix);
    }

    auto ptr = folder_ptr_t();
    ptr = new folder_t(key);
    ptr->assign_fields(folder);
    return outcome::success(std::move(ptr));
}

outcome::result<folder_ptr_t> folder_t::create(const bu::uuid &uuid, const db::Folder &folder) noexcept {
    auto ptr = folder_ptr_t();
    ptr = new folder_t(uuid);
    ptr->assign_fields(folder);
    return outcome::success(std::move(ptr));
}

folder_t::folder_t(utils::bytes_view_t key_) noexcept : synchronizing{false}, suspended{false} {
    std::copy(key_.begin(), key_.end(), key);
}

folder_t::folder_t(const bu::uuid &uuid) noexcept : synchronizing{false}, suspended{false} {
    key[0] = prefix;
    std::copy(uuid.begin(), uuid.end(), key + 1);
}

utils::bytes_view_t folder_t::get_uuid() const noexcept { return utils::bytes_view_t(key + 1, uuid_length); }

void folder_t::add(const folder_info_ptr_t &folder_info) noexcept { folder_infos.put(folder_info); }

void folder_t::assign_cluster(const cluster_ptr_t &cluster_) noexcept { cluster = cluster_.get(); }

utils::bytes_t folder_t::serialize() noexcept {
    auto r = db::Folder();
    folder_data_t::serialize(r);
    return r.encode();
}

auto folder_t::is_shared_with(const model::device_t &device) const noexcept -> folder_info_ptr_t {
    return folder_infos.by_device_id(device.device_id().get_sha256());
}

std::optional<proto::Folder> folder_t::generate(const model::device_t &device) const noexcept {
    if (!is_shared_with(device)) {
        return {};
    }

    proto::Folder r;
    r.id(id);
    r.label(label);
    r.read_only(read_only);
    r.ignore_permissions(ignore_permissions);
    r.ignore_delete(ignore_delete);
    r.disable_temp_indexes(disable_temp_indixes);
    r.paused(paused);
    for (auto &it : folder_infos) {
        auto &fi = *it.item;
        auto &d = *fi.get_device();
        auto pd = proto::Device();
        pd.id(d.device_id().get_sha256());
        pd.name(d.get_name());
        pd.compression(d.get_compression());
        if (auto cn = d.get_cert_name(); cn) {
            pd.cert_name(cn.value());
        }
        std::int64_t max_seq = fi.get_max_sequence();
        pd.max_sequence(max_seq);
        pd.index_id(fi.get_index());
        pd.introducer(d.is_introducer());
        pd.skip_introduction_removals(d.get_skip_introduction_removals());
        spdlog::trace("folder_t::generate (==>), folder = {} (index = 0x{:x}), device = {}, max_seq = {}", label,
                      fi.get_index(), d.device_id(), max_seq);
    }
    return r;
}

const pt::ptime &folder_t::get_scan_start() const noexcept { return scan_start; }
void folder_t::set_scan_start(const pt::ptime &value) noexcept { scan_start = value; }
const pt::ptime &folder_t::get_scan_finish() noexcept { return scan_finish; }
void folder_t::set_scan_finish(const pt::ptime &value) noexcept {
    assert(!scan_start.is_not_a_date_time());
    assert(scan_start <= value);
    scan_finish = value;
}

bool folder_t::is_scanning() const noexcept {
    if (scan_start.is_not_a_date_time()) {
        return false;
    }
    if (scan_finish.is_not_a_date_time()) {
        return true;
    }
    return scan_start > scan_finish;
}

bool folder_t::is_synchronizing() const noexcept { return synchronizing > 0; }

void folder_t::adjust_synchronization(std::int_fast32_t delta) noexcept {
    synchronizing += delta;
    assert(synchronizing >= 0);
}

void folder_t::mark_suspended(bool value) noexcept { suspended = value; }
bool folder_t::is_suspended() const noexcept { return suspended; }

template <> SYNCSPIRIT_API std::string_view get_index<0>(const folder_ptr_t &item) noexcept {
    auto key = item->get_key();
    auto ptr = (const char*)key.data();
    return {ptr, key.size()};
}
template <> SYNCSPIRIT_API std::string_view get_index<1>(const folder_ptr_t &item) noexcept { return item->get_id(); }

folder_ptr_t folders_map_t::by_id(std::string_view id) const noexcept { return get<1>(id); }

folder_ptr_t folders_map_t::by_key(utils::bytes_view_t key) const noexcept {
    auto ptr = (const char* )key.data();
    return get<0>({ptr, key.size()});
}

} // namespace syncspirit::model

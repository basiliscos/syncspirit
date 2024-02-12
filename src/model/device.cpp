// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "device.h"
#include "structs.pb.h"
#include "../db/prefix.h"
#include "misc/error_code.h"

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::device);

template <> void device_t::assign(const db::Device &d) noexcept {
    name = d.name();
    compression = d.compression();
    cert_name = d.cert_name();
    introducer = d.introducer();
    auto_accept = d.auto_accept();
    paused = d.paused();
    skip_introduction_removals = d.skip_introduction_removals();

    for (int i = 0; i < d.addresses_size(); ++i) {
        auto uri = utils::parse(d.addresses(i));
        assert(uri);
        uris.emplace_back(std::move(uri.value()));
        static_uris.emplace_back(uris.back());
    }
}

outcome::result<device_ptr_t> device_t::create(std::string_view key, const db::Device &data) noexcept {
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_device_prefix);
    }

    auto id = device_id_t::from_sha256(key.substr(1));
    if (!id) {
        return make_error_code(error_code_t::invalid_device_sha256_digest);
    }

    auto ptr = device_ptr_t(new device_t(id.value(), data.name(), data.cert_name()));
    ptr->assign(data);
    return outcome::success(std::move(ptr));
}

outcome::result<device_ptr_t> device_t::create(const device_id_t &device_id, std::string_view name,
                                               std::string_view cert_name) noexcept {
    auto ptr = device_ptr_t(new device_t(device_id, name, cert_name));
    return outcome::success(std::move(ptr));
}

device_t::device_t(const device_id_t &device_id_, std::string_view name_, std::string_view cert_name_) noexcept
    : id(std::move(device_id_)), name{name_}, compression{proto::Compression::METADATA}, cert_name{cert_name_},
      introducer{false}, auto_accept{false}, paused{false}, skip_introduction_removals{false},
      state{device_state_t::offline} {}

void device_t::update(const db::Device &source) noexcept { assign(source); }

uint64_t device_t::as_uint() noexcept {
    auto device_id = id.get_sha256();
    uint64_t id;
    auto ptr = device_id.data();
    std::copy(ptr, ptr + sizeof(id), reinterpret_cast<char *>(&id));
    return id;
}

std::string device_t::serialize() noexcept {
    db::Device r;
    r.set_name(name);
    r.set_compression(compression);
    if (cert_name) {
        r.set_cert_name(cert_name.value());
    }
    r.set_introducer(introducer);
    r.set_skip_introduction_removals(skip_introduction_removals);
    r.set_auto_accept(auto_accept);
    r.set_paused(paused);

    for (auto &address : static_uris) {
        *r.add_addresses() = address.full;
    }

    return r.SerializeAsString();
}

void device_t::update_state(device_state_t new_state) { state = new_state; }

std::string_view device_t::get_key() const noexcept { return id.get_key(); }

void device_t::assing_uris(const uris_t &uris_) noexcept { uris = uris_; }

local_device_t::local_device_t(const device_id_t &device_id, std::string_view name, std::string_view cert_name) noexcept
    : device_t(device_id, name, cert_name) {
    state = device_state_t::online;
}

std::string_view local_device_t::get_key() const noexcept { return local_device_id.get_key(); }

template <> SYNCSPIRIT_API std::string_view get_index<0, device_ptr_t>(const device_ptr_t &item) noexcept {
    return item->get_key();
}

template <> SYNCSPIRIT_API std::string_view get_index<1, device_ptr_t>(const device_ptr_t &item) noexcept {
    return item->device_id().get_sha256();
}

device_ptr_t devices_map_t::by_sha256(std::string_view device_id) const noexcept { return get<1>(device_id); }

} // namespace syncspirit::model

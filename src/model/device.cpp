// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "device.h"
#include "proto/proto-helpers-db.h"
#include "db/prefix.h"
#include "misc/error_code.h"
#include "misc/file_iterator.h"
#include "utils/time.h"
#include "utils/error_code.h"
#include <cassert>

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::device);

template <> auto device_t::assign(const db::Device &d) noexcept -> outcome::result<void> {
    name = db::get_name(d);
    compression = db::get_compression(d);
    cert_name = db::get_cert_name(d);
    introducer = db::get_introducer(d);
    auto_accept = db::get_auto_accept(d);
    paused = db::get_paused(d);
    skip_introduction_removals = db::get_skip_introduction_removals(d);
    static_uris.clear();
    uris.clear();
    last_seen = pt::from_time_t(db::get_last_seen(d));

    auto uris = uris_t{};
    auto addresses_count = db::get_addresses_size(d);
    for (size_t i = 0; i < addresses_count; ++i) {
        auto uri = utils::parse(db::get_addresses(d, i));
        if (!uri) {
            return make_error_code(utils::error_code_t::malformed_url);
        }
        assert(uri);
        uris.emplace_back(uri);
    }
    set_static_uris(std::move(uris));
    return outcome::success();
}

outcome::result<device_ptr_t> device_t::create(utils::bytes_view_t key, const db::Device &data) noexcept {
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_device_prefix);
    }

    auto id = device_id_t::from_sha256(key.subspan(1));
    if (!id) {
        return make_error_code(error_code_t::invalid_device_sha256_digest);
    }

    auto name = db::get_name(data);
    auto cert_name = db::get_cert_name(data);
    auto ptr = device_ptr_t(new device_t(id.value(), name, cert_name));
    auto ec = ptr->assign(data);
    if (!ec) {
        return ec.error();
    }
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
      state{device_state_t::offline}, last_seen{pt::from_time_t(0)}, rx_bytes{0}, tx_bytes{0} {}

device_t::~device_t() {}

auto device_t::update(const db::Device &source) noexcept -> outcome::result<void> { return assign(source); }

auto device_t::serialize(db::Device &r) const noexcept -> utils::bytes_t {
    db::set_name(r, name);
    db::set_compression(r, compression);
    if (cert_name.has_value()) {
        db::set_cert_name(r, cert_name.value());
    }
    db::set_introducer(r, introducer);
    db::set_skip_introduction_removals(r, skip_introduction_removals);
    db::set_auto_accept(r, auto_accept);
    db::set_paused(r, paused);
    db::set_last_seen(r, utils::as_seconds(last_seen));

    for (size_t i = 0; i < static_uris.size(); ++i) {
        auto buff = static_uris[i]->buffer();
        db::set_addresses(r, i, buff);
    }
    return db::encode(r);
}

auto device_t::serialize() const noexcept -> utils::bytes_t {
    db::Device r;
    return serialize(r);
}

void device_t::update_state(device_state_t new_state, std::string_view connection_id_) noexcept {
    if (state == device_state_t::online || new_state == device_state_t::online) {
        last_seen = pt::microsec_clock::local_time();
        if (new_state == device_state_t::online) {
            assert(!connection_id_.empty());
            bool new_wins = (connection_id.empty()) || (connection_id_.size() < connection_id.size()) ||
                            (connection_id_ < connection_id);
            if (new_wins) {
                connection_id = connection_id_;
            }
        }
    } else if (state < new_state) {
        connection_id = connection_id_;
    } else {
        connection_id = {};
    }
    state = new_state;
}

void device_t::update_contact(const tcp::endpoint &endpoint_, std::string_view client_name_,
                              std::string_view client_version_) noexcept {
    endpoint = endpoint_;
    client_name = client_name_;
    client_version = client_version_;
}

auto device_t::get_key() const noexcept -> utils::bytes_view_t { return id.get_key(); }

void device_t::set_static_uris(uris_t uris) noexcept { static_uris = std::move(uris); }

void device_t::assign_uris(const uris_t &uris_) noexcept { uris = uris_; }

auto device_t::create_iterator(cluster_t &cluster) noexcept -> file_iterator_ptr_t {
    assert(!iterator);
    iterator = new file_iterator_t(cluster, this);
    return iterator;
}

void device_t::release_iterator(file_iterator_ptr_t &it) noexcept {
    assert(it == iterator);
    (void)it;
    iterator.reset();
}

file_iterator_t *device_t::get_iterator() noexcept { return iterator.get(); }

std::string_view device_t::get_connection_id() noexcept { return connection_id; }

local_device_t::local_device_t(const device_id_t &device_id, std::string_view name, std::string_view cert_name) noexcept
    : device_t(device_id, name, cert_name) {
    state = device_state_t::online;
}

utils::bytes_view_t local_device_t::get_key() const noexcept { return local_device_id.get_key(); }

template <> SYNCSPIRIT_API utils::bytes_view_t get_index<0>(const device_ptr_t &item) noexcept {
    return item->get_key();
}

template <> SYNCSPIRIT_API utils::bytes_view_t get_index<1>(const device_ptr_t &item) noexcept {
    return item->device_id().get_sha256();
}

device_ptr_t devices_map_t::by_sha256(utils::bytes_view_t device_id) const noexcept { return get<1>(device_id); }

device_ptr_t devices_map_t::by_key(utils::bytes_view_t key) const noexcept { return get<0>(key); }

} // namespace syncspirit::model

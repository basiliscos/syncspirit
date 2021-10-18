#include "device.h"
#include "structs.pb.h"
#include "../db/prefix.h"

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::device);

device_t::device_t(std::string_view key, std::string_view data) noexcept
    : id{device_id_t::from_sha256(key.substr(1)).value()}{
    assert(key[0] == prefix);

    db::Device d;
    auto ok = d.ParsePartialFromArray(data.data(), data.size());
    assert(ok);
    (void)ok;

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
        static_addresses.emplace_back(std::move(uri.value()));
    }
}

device_t::device_t(const device_id_t &device_id, std::string_view name_, std::string_view cert_name_) noexcept {
    id = std::move(device_id);
    name = name_;
    cert_name = cert_name_;
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

    int i = 0;
    for (auto &address : static_addresses) {
        *r.mutable_addresses(i) = address.full;
    }

    return r.SerializeAsString();
}

void device_t::mark_online(bool value) noexcept { online = value; }

std::string_view device_t::get_key() const noexcept { return id.get_key(); }

std::string_view local_device_t::get_key() const noexcept { return local_device_id.get_key(); }

template<>
std::string_view get_index<0, device_ptr_t>(const device_ptr_t& item) noexcept {
    return item->get_key();
}

template<>
std::string_view get_index<1, device_ptr_t>(const device_ptr_t& item) noexcept {
    return item->device_id().get_sha256();
}

device_ptr_t devices_map_t::bySha256(std::string_view device_id) noexcept {
    return get<1>(device_id);
}


}

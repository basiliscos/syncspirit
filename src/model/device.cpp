#include "device.h"
#include "structs.pb.h"

using namespace syncspirit;
using namespace syncspirit::model;

device_t::device_t(const db::Device &d, uint64_t db_key_) noexcept
    : device_id{device_id_t::from_sha256(d.id()).value()}, name{d.name()}, compression{d.compression()},
      cert_name{d.cert_name()}, introducer{d.introducer()}, auto_accept{d.auto_accept()}, paused{d.paused()},
      skip_introduction_removals{d.skip_introduction_removals()}, db_key{db_key_} {

    for (int i = 0; i < d.addresses_size(); ++i) {
        static_addresses.emplace_back(d.addresses(i));
    }
}

db::Device device_t::serialize() noexcept {
    db::Device r;
    r.set_id(get_id());
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
    return r;
}

void device_t::mark_online(bool value) noexcept { online = value; }

const std::string &device_t::get_id() const noexcept { return device_id.get_sha256(); }

const std::string &local_device_t::get_id() const noexcept { return local_device_id.get_sha256(); }

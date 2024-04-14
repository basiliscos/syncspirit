// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "update_contact.h"
#include "../contact_visitor.h"
#include "../../cluster.h"
#include <fmt/core.h>
#include <algorithm>

using namespace syncspirit::model::diff::modify;

update_contact_t::update_contact_t(const model::cluster_t &cluster, const model::device_id_t &device_,
                                   const utils::uri_container_t &uris_) noexcept
    : device{device_}, self{false} {
    auto &devices = cluster.get_devices();
    known = (bool)devices.by_sha256(device.get_sha256());
    if (known) {
        self = cluster.get_device()->device_id().get_sha256() == device.get_sha256();
    }

    auto copy = uris_;
    std::sort(begin(copy), end(copy));
    auto end = std::unique(std::begin(copy), std::end(copy));
    std::copy(begin(copy), end, std::back_insert_iterator(this->uris));
}

update_contact_t::update_contact_t(const model::cluster_t &cluster, const ip_addresses_t &addresses) noexcept
    : device{cluster.get_device()->device_id()}, known{true}, self{true} {
    auto port = 0;
    auto my_device = cluster.get_device();
    for (auto &uri : my_device->get_uris()) {
        if (uri.port != 0) {
            port = uri.port;
            break;
        }
    }
    auto uris = my_device->get_uris();

    for (auto &addr : addresses) {
        auto uri_str = fmt::format("tcp://{0}:{1}/", addr, port);
        auto uri = utils::parse(uri_str).value();
        uris.push_back(uri);
    }
    std::sort(begin(uris), end(uris));
    auto end = std::unique(std::begin(uris), std::end(uris));
    std::copy(begin(uris), end, std::back_insert_iterator(this->uris));
}

auto update_contact_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    if (known) {
        auto &devices = cluster.get_devices();
        auto device = devices.by_sha256(this->device.get_sha256());
        device->assign_uris(uris);
    }
    return outcome::success();
}

auto update_contact_t::visit(contact_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}

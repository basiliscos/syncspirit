// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "proto/proto-helpers.h"

using namespace pp;

template<typename Field>
void assign_default(Field& f) {
    using value_t = std::remove_reference_t<std::remove_cv_t<decltype(f.value())>>;
    f = value_t();
}

namespace syncspirit::proto::details {

utils::bytes_view_t id(Announce& data) noexcept {
    auto& f = data["id"_f];
    if (!f.has_value()) {
        assign_default(f);
    }
    return f.value();
}

void id(Announce& data, utils::bytes_view_t value) noexcept {
    data["id"_f] = std::vector<unsigned char>(value.begin(), value.end());
}

std::size_t addresses_size(Announce& data) noexcept {
    return data["addresses"_f].size();
}

std::string_view addresses(Announce& data, std::size_t i) noexcept {
    return data["addresses"_f][i];
}

void addresses(Announce& data, std::size_t i, std::string_view value) noexcept {
    data["addresses"_f][i] = value;
}

std::uint64_t instance_id(Announce& data) noexcept {
    auto& f = data["instance_id"_f];
    if (!f.has_value()) {
        assign_default(f);
    }
    return f.value();
}

void instance_id(Announce& data, std::uint64_t value) noexcept {
    data["instance_id"_f] = value;
}

}

// void                SYNCSPIRIT_API instance_id(Announce&, std::uint64_t value) noexcept;

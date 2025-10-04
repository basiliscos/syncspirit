// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "path.h"
#include <filesystem>
#include <boost/nowide/convert.hpp>
#include <cstring>
#include <cassert>

namespace bfs = std::filesystem;

using namespace boost::nowide;

namespace syncspirit::model {

path_t::path_t(std::string_view full_name) noexcept {
    name = string_t(full_name.data(), full_name.data() + full_name.size());
    auto file_path = bfs::path(widen(full_name));
    auto tmp = std::vector<char>(full_name.size() * 4);
    auto it = file_path.begin();
    auto root_name = it->wstring();
    auto ptr = narrow(tmp.data(), tmp.size(), root_name.data());
    auto prev = std::strlen(tmp.data());
    ++it;
    auto sz = std::distance(it, file_path.end());
    pieces.resize(sz);
    auto i = std::uint32_t{0};
    for (; it != file_path.end(); ++it) {
        auto sub_name = it->wstring();
        auto ptr = narrow(tmp.data(), tmp.size(), sub_name.data());
        pieces[i] = static_cast<uint32_t>(prev + 1);
        prev += std::strlen(tmp.data()) + 1;
        ++i;
    }
}

path_t::path_t(path_t &source) noexcept {
    name = source.name;
    pieces = source.pieces;
}

std::size_t path_t::get_pieces_size() const noexcept { return pieces.size() + (!name.empty() ? 1 : 0); }

std::string_view path_t::get_full_name() const noexcept { return std::string_view(name.data(), name.size()); }

std::string_view path_t::get_parent_name() const noexcept {
    if (pieces.size()) {
        auto last_offset = pieces.back();
        auto view = std::string_view(name.data(), name.size());
        return view.substr(0, last_offset - 1);
    }
    return {};
}

std::string_view path_t::get_own_name() const noexcept {
    if (pieces.size()) {
        auto last_offset = pieces.back();
        auto view = std::string_view(name.data() + last_offset, name.size() - last_offset);
        return view;
    }
    return std::string_view(name.data(), name.size());
}

auto path_t::begin() const noexcept -> iterator_t { return iterator_t(this); };

auto path_t::end() const noexcept -> iterator_t { return iterator_t(); }

bool path_t::contains(const path_t &other) const noexcept {
    auto ptr = other.name.data();
    auto end = ptr + other.name.sz;
    auto view = std::string_view(ptr, end);
    auto self = std::string_view(name.data(), name.size());
    return view.find(self) == 0;
}

using I = path_t::iterator_t;

I::iterator_t() noexcept : position{-1}, path{nullptr} {}

I::iterator_t(const path_t *path_) noexcept : position{0}, path{path_} {}

auto I::operator*() const noexcept -> reference {
    assert(path);
    assert(position >= 0);
    assert(position <= path->pieces.size());

    auto b = position ? path->pieces[position - 1] : 0;
    auto e = position < path->pieces.size() ? path->pieces[position] - 1 : std::string::npos;
    auto s = e - b;
    auto &n = path->name;
    auto view = std::string_view(n.data(), n.data() + n.size());
    return view.substr(b, s);
}

I &I::operator++() noexcept {
    assert(path);
    auto sz = path->pieces.size();

    assert(position <= sz);
    ++position;
    if (position > sz) {
        path = nullptr;
        position = -1;
    }
    return *this;
}

bool I::operator==(iterator_t other) noexcept { return (path == other.path) && position == other.position; }

} // namespace syncspirit::model

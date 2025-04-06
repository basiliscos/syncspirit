// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "path.h"
#include <filesystem>
#include <boost/nowide/convert.hpp>
#include <cstring>
#include <cassert>

namespace bfs = std::filesystem;

using namespace boost::nowide;

namespace syncspirit::presentation {

path_t::path_t(std::string_view full_name) noexcept : name(full_name) {
    auto file_path = bfs::path(widen(full_name));
    auto prev = std::uint32_t{0};
    auto tmp = std::vector<char>(full_name.size() * 4);
    auto it = file_path.begin();
    for (++it; it != file_path.end(); ++it) {
        auto sub_name = it->wstring();
        auto ptr = narrow(tmp.data(), tmp.size(), sub_name.data());
        pieces.emplace_back(prev + 1);
        prev += std::strlen(tmp.data()) + 1;
    }
}

std::size_t path_t::get_pieces_size() const noexcept { return pieces.size() + (!name.empty() ? 1 : 0); }

std::string_view path_t::get_full_name() const noexcept { return name; }

std::string_view path_t::get_parent_name() const noexcept {
    if (pieces.size()) {
        auto last_offset = pieces.back();
        auto view = std::string_view(name);
        return view.substr(0, last_offset);
    }
    return {};
}

std::string_view path_t::get_own_name() const noexcept {
    if (pieces.size()) {
        auto last_offset = pieces.back();
        auto view = std::string_view(name);
        return view.substr(last_offset + 1);
    }
    return name;
}

auto path_t::begin() const noexcept -> iterator_t { return iterator_t(this); };

auto path_t::end() const noexcept -> iterator_t { return iterator_t(); }

using I = path_t::iterator_t;

I::iterator_t() noexcept : position{-1}, path{nullptr} {}

I::iterator_t(const path_t *path_) noexcept : position{0}, path{path_} {}

auto I::operator*() const noexcept -> reference {
    assert(path);
    assert(position >= 0);
    assert(position <= path->pieces.size());

    auto b = position ? path->pieces[position - 1] + 1 : 0;
    auto e = position < path->pieces.size() ? path->pieces[position] : std::string::npos;
    auto s = e - b;
    auto view = std::string_view(path->name);
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

} // namespace syncspirit::presentation

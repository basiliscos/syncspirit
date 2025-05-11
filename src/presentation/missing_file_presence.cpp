// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "missing_file_presence.h"
#include "file_entity.h"

#include <memory_resource>

using namespace syncspirit;
using namespace syncspirit::presentation;

using augmentations_on_stack_t = std::pmr::set<model::augmentation_t *>;

missing_file_presence_t::missing_file_presence_t(file_entity_t &entity_) noexcept : file_presence_t({}, {}) {
    features = features_t::missing | features_t::file;
    entity = &entity_;
}

missing_file_presence_t::~missing_file_presence_t() {
    on_delete();
    entity = nullptr;
    statistics = {};
}

void missing_file_presence_t::add(model::augmentation_t *augmentation) noexcept { augmentations.insert(augmentation); }

void missing_file_presence_t::remove(model::augmentation_t *augmentation) noexcept {
    augmentations.erase(augmentation);
}

void missing_file_presence_t::on_update() noexcept {
    file_presence_t::on_update();
    if (augmentations.size()) {
        auto buffer = std::array<std::byte, 128>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = std::pmr::polymorphic_allocator<std::string>(&pool);
        auto b = augmentations.begin();
        auto e = augmentations.end();
        auto augs = augmentations_on_stack_t(b, e, allocator);
        for (auto aug : augs) {
            aug->on_update();
        }
    }
}

void missing_file_presence_t::on_delete() noexcept {
    if (augmentations.size()) {
        auto buffer = std::array<std::byte, 128>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = std::pmr::polymorphic_allocator<std::string>(&pool);
        auto b = augmentations.begin();
        auto e = augmentations.end();
        auto augs = augmentations_on_stack_t(b, e, allocator);
        for (auto aug : augs) {
            aug->on_delete();
        }
        augmentations.clear();
    }
}

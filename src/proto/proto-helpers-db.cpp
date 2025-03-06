// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "proto-helpers-db.h"
#include "proto-helpers-impl.hpp"
#include <protopuf/message.h>
#include <protopuf/coder.h>

namespace syncspirit::db {

using namespace syncspirit::details;

int decode(utils::bytes_view_t data, BlockInfo &object) { return generic_decode(data, object); }

utils::bytes_t encode(const BlockInfo &object) { return generic_encode(object); }

int decode(utils::bytes_view_t data, Device &object) { return generic_decode(data, object); }

utils::bytes_t encode(const Device &object) { return generic_encode(object); }

int decode(utils::bytes_view_t data, FileInfo &object) { return generic_decode(data, object); }

utils::bytes_t encode(const FileInfo &object) { return generic_encode(object); }

int decode(utils::bytes_view_t data, Folder &object) { return generic_decode(data, object); }

utils::bytes_t encode(const Folder &object) { return generic_encode(object); }

int decode(utils::bytes_view_t data, FolderInfo &object) { return generic_decode(data, object); }

utils::bytes_t encode(const FolderInfo &object) { return generic_encode(object); }

int decode(utils::bytes_view_t data, IgnoredFolder &object) { return generic_decode(data, object); }

utils::bytes_t encode(const IgnoredFolder &object) { return generic_encode(object); }

int decode(utils::bytes_view_t data, PendingFolder &object) { return generic_decode(data, object); }

utils::bytes_t encode(const PendingFolder &object) { return generic_encode(object); }

int decode(utils::bytes_view_t data, SomeDevice &object) { return generic_decode(data, object); }

utils::bytes_t encode(const SomeDevice &object) { return generic_encode(object); }

} // namespace syncspirit::db

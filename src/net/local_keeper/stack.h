// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "unscanned_dir.h"
#include "hash_new_file.h"
#include "hash_existing_file.h"
#include "hash_incomplete_file.h"
#include "unexamined.h"
#include "incomplete.h"
#include "child_ready.h"
#include "rehashed_incomplete.h"

namespace syncspirit::net::local_keeper {

using hash_new_file_ptr_t = boost::intrusive_ptr<hash_new_file_t>;
using hash_existing_file_ptr_t = boost::intrusive_ptr<hash_existing_file_t>;
using hash_incomplete_file_ptr_t = boost::intrusive_ptr<hash_incomplete_file_t>;

struct complete_scan_t {};
struct suspend_scan_t {
    sys::error_code ec;
};
struct unsuspend_scan_t {};
struct removed_dir_t {
    presentation::presence_ptr_t presence;
};
struct confirmed_deleted_t {
    presentation::presence_ptr_t presence;
};

struct fatal_error_t {
    sys::error_code ec;
};

using stack_item_t =
    std::variant<unscanned_dir_t, unexamined_t, incomplete_t, complete_scan_t, child_ready_t, hash_new_file_ptr_t,
                 hash_existing_file_ptr_t, hash_incomplete_file_ptr_t, rehashed_incomplete_t, removed_dir_t,
                 confirmed_deleted_t, suspend_scan_t, unsuspend_scan_t, fatal_error_t>;
using stack_t = std::list<stack_item_t>;

struct dirs_stack_t : stack_t {
    dirs_stack_t(stack_t &outer_);
    ~dirs_stack_t();
    stack_t &outer;
};

} // namespace syncspirit::net::local_keeper

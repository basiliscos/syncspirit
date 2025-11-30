// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "local_keeper.h"
#include "model/diff/advance/advance.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/local/blocks_availability.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/local/file_availability.h"
#include "model/diff/modify/suspend_folder.h"
#include "model/misc/resolver.h"
#include "presentation/folder_entity.h"
#include "fs/fs_slave.h"
#include "fs/utils.h"
#include "names.h"
#include "presentation/folder_presence.h"
#include "presentation/local_file_presence.h"
#include "presentation/presence.h"
#include "presentation/cluster_file_presence.h"
#include "proto/proto-helpers-bep.h"
#include "utils/platform.h"

#include <algorithm>
#include <memory_resource>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::net;

namespace bfs = std::filesystem;
namespace sys = boost::system;

using boost::nowide::narrow;

namespace {

struct folder_slave_t;
using allocator_t = std::pmr::polymorphic_allocator<char>;
using F = presentation::presence_t::features_t;

struct stack_context_t {
    model::diff::cluster_diff_ptr_t diff;
    model::diff::cluster_diff_t *next = nullptr;

    void push(model::diff::cluster_diff_t *d) noexcept {
        if (next) {
            next = next->assign_sibling(d);
        } else {
            next = d;
            diff = d;
        }
    }
};

struct folder_context_t : boost::intrusive_ref_counter<folder_context_t, boost::thread_safe_counter> {
    folder_context_t(model::folder_info_ptr_t local_folder_) noexcept : local_folder{local_folder_} {}
    model::folder_info_ptr_t local_folder;
};

using folder_context_ptr_t = r::intrusive_ptr_t<folder_context_t>;

struct child_info_t {
    using blocks_t = std::vector<proto::BlockInfo>;

    child_info_t(fs::task::scan_dir_t::child_info_t backend, presentation::presence_ptr_t self_,
                 presentation::presence_ptr_t parent_)
        : self(std::move(self_)), parent(std::move(parent_)) {
        path = std::move(backend.path);
        link_target = std::move(backend.target);
        last_write_time = fs::to_unix(backend.last_write_time);
        size = backend.size;
        type = [&]() -> proto::FileInfoType {
            using FT = proto::FileInfoType;
            auto t = backend.status.type();
            if (t == bfs::file_type::directory)
                return FT::DIRECTORY;
            else if (t == bfs::file_type::symlink)
                return FT::SYMLINK;
            else
                return FT::FILE;
        }();
        auto &status = backend.status;
        perms = static_cast<std::uint32_t>(status.permissions());
        ec = backend.ec;
    }

    proto::FileInfo serialize(const model::folder_info_t &local_folder, blocks_t blocks, bool ignore_permissions) {
        auto data = proto::FileInfo();
        auto name = fs::relativize(path, local_folder.get_folder()->get_path());
        proto::set_name(data, narrow(name.generic_wstring()));
        proto::set_type(data, type);
        proto::set_modified_s(data, last_write_time);
        if (size) {
            auto block_size = proto::get_size(blocks.front());
            proto::set_block_size(data, block_size);
            proto::set_size(data, size);
            proto::set_blocks(data, std::move(blocks));
        }
        if (ignore_permissions == false) {
            proto::set_permissions(data, perms);
        } else {
            proto::set_permissions(data, 0666);
            proto::set_no_permissions(data, true);
        }
        if (type == proto::FileInfoType::SYMLINK) {
            proto::set_symlink_target(data, narrow(link_target.generic_wstring()));
        }
        return data;
    }

    bfs::path path;
    bfs::path link_target;
    std::int64_t last_write_time;
    std::uintmax_t size;
    proto::FileInfoType type;
    std::uint32_t perms;
    sys::error_code ec;
    presentation::presence_ptr_t self;
    presentation::presence_ptr_t parent;
};

struct unscanned_dir_t {
    unscanned_dir_t(bfs::path path_, presentation::presence_ptr_t presence_)
        : path(std::move(path_)), presence(std::move(presence_)) {}
    bfs::path path;
    presentation::presence_ptr_t presence;
};

struct unexamined_t : child_info_t {
    unexamined_t(child_info_t info) : child_info_t(std::move(info)) {}
};
struct incomplete_t : child_info_t {
    using child_info_t::child_info_t;
};
struct child_ready_t : child_info_t {
    child_ready_t(child_info_t info, blocks_t blocks_ = {})
        : child_info_t{std::move(info)}, blocks{std::move(blocks_)} {}
    blocks_t blocks;
};
struct hash_base_t : model::arc_base_t<hash_base_t>, child_info_t {
    using blocks_t = std::vector<proto::BlockInfo>;

    hash_base_t(child_info_t &&info_, std::int32_t block_size_ = 0) : child_info_t{std::move(info_)} {
        if (block_size_ == 0) {
            auto div = fs::get_block_size(size, 0);
            block_size = div.size;
            unprocessed_blocks = unhashed_blocks = total_blocks = div.count;
        } else {
            block_size = block_size_;
            auto count = size / static_cast<decltype(size)>(block_size_);
            if (size % count) {
                ++count;
            }
            unprocessed_blocks = unhashed_blocks = total_blocks = count;
        }
        blocks.resize(total_blocks);
    }

    bool commit_error(sys::error_code ec_, std::int32_t delta) {
        ec = ec_;
        errored_blocks += delta;
        return errored_blocks + unprocessed_blocks == unhashed_blocks;
    }

    bool commit_hash() const { return errored_blocks + unprocessed_blocks + unhashed_blocks == total_blocks; }

    std::int32_t block_size;
    std::int32_t total_blocks;
    std::int32_t unprocessed_blocks;
    std::int32_t unhashed_blocks;
    std::int32_t errored_blocks = 0;
    sys::error_code ec;
    blocks_t blocks;
    model::advance_action_t action = model::advance_action_t::ignore;
    bool incomplete = false;
};

struct hash_new_file_t : hash_base_t {
    using hash_base_t::hash_base_t;
};

struct hash_existing_file_t : hash_base_t {
    using hash_base_t::hash_base_t;
};

struct hash_incomplete_file_t : hash_base_t {
    hash_incomplete_file_t(child_info_t info_, const presentation::presence_t *presence_,
                           model::advance_action_t action_)
        : hash_base_t(std::move(info_), false) {
        incomplete = true;
        auto cp = static_cast<const presentation::cluster_file_presence_t *>(presence_);
        auto &file = cp->get_file_info();
        block_size = file.get_block_size();
        unprocessed_blocks = unhashed_blocks = total_blocks = file.iterate_blocks().get_total();
        blocks.resize(total_blocks);
        self = const_cast<presentation::presence_t *>(presence_);
        action = action_;
    }
};

struct rehashed_incomplete_t : child_ready_t {
    rehashed_incomplete_t(child_info_t info, blocks_t blocks_, model::advance_action_t action_)
        : child_ready_t(std::move(info), std::move(blocks_)), action{action_} {}
    model::advance_action_t action;
    ;
};

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
struct fatal_error_t {
    sys::error_code ec;
};

using stack_item_t =
    std::variant<unscanned_dir_t, unexamined_t, incomplete_t, complete_scan_t, child_ready_t, hash_new_file_ptr_t,
                 hash_existing_file_ptr_t, hash_incomplete_file_ptr_t, rehashed_incomplete_t, removed_dir_t,
                 suspend_scan_t, unsuspend_scan_t, fatal_error_t>;
using stack_t = std::list<stack_item_t>;

using folder_slave_ptr_t = r::intrusive_ptr_t<folder_slave_t>;
using hash_base_ptr_t = model::intrusive_ptr_t<hash_base_t>;

struct hash_context_t final : hasher::payload::extendended_context_t {
    hash_context_t(folder_slave_ptr_t slave_, hash_base_ptr_t hash_file_)
        : slave{std::move(slave_)}, hash_file{std::move(hash_file_)} {}

    folder_slave_ptr_t slave;
    hash_base_ptr_t hash_file;
};
using hash_context_ptr_t = r::intrusive_ptr_t<hash_context_t>;

struct rename_context_t final : hasher::payload::extendended_context_t {
    rename_context_t(rehashed_incomplete_t item_) : item(std::move(item_)) {}
    rehashed_incomplete_t item;
};

struct folder_slave_t final : fs::fs_slave_t {
    using local_keeper_ptr_t = r::intrusive_ptr_t<net::local_keeper_t>;

    folder_slave_t(folder_context_ptr_t context_, local_keeper_ptr_t actor_) noexcept
        : context{context_}, actor{std::move(actor_)} {
        log = actor->log;
        auto folder = context->local_folder->get_folder();
        auto augmentation = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
        auto local_device = folder->get_cluster()->get_device();
        auto folder_presence = folder_entity->get_presence(local_device.get());
        auto path = folder->get_path();
        ignore_permissions = folder->are_permissions_ignored() || !utils::platform_t::permissions_supported(path);
        stack.push_front(complete_scan_t{});
        stack.push_front(unscanned_dir_t(std::move(path), folder_presence));
    }

    void process_stack(stack_context_t &ctx) noexcept {
        auto try_next = true;
        while (!stack.empty() && try_next) {
            auto it = stack.begin();
            auto &item = *it;
            auto r = std::visit([&](auto &item) { return process(item, ctx); }, item);
            if (r >= 0) {
                stack.erase(it);
            }
            try_next = r > 0;
        }
        if (ctx.diff) {
            actor->send<model::payload::model_update_t>(actor->coordinator, std::move(ctx.diff));
        }
    }

    void process_stack() noexcept {
        auto ctx = stack_context_t();
        process_stack(ctx);
    }

    int process(complete_scan_t &, stack_context_t &ctx) noexcept {
        auto nothing_left = pending_io.empty() && (actor->concurrent_hashes_left == actor->concurrent_hashes_limit) &&
                            (actor->fs_tasks == 0);
        if (nothing_left || force_completion) {
            auto folder = context->local_folder->get_folder();
            auto folder_id = folder->get_id();
            auto now = r::pt::microsec_clock::local_time();
            LOG_DEBUG(log, "pushing scan_finish");
            ctx.push(new model::diff::local::scan_finish_t(folder_id, now));
        }
        return -1;
    }

    int process(unscanned_dir_t &dir, stack_context_t &ctx) noexcept {
        auto sub_task = fs::task::scan_dir_t(std::move(dir.path), std::move(dir.presence));
        pending_io.emplace_back(std::move(sub_task));
        return 0;
    }

    int process(unexamined_t &child_info, stack_context_t &ctx) noexcept {
        auto &type = child_info.type;
        if (type == proto::FileInfoType::DIRECTORY) {
            auto self = child_info.self;
            auto path_copy = child_info.path;
            stack.push_front(child_ready_t(std::move(child_info)));
            stack.push_front(unscanned_dir_t(std::move(path_copy), self));
        } else if (type == proto::FileInfoType::SYMLINK) {
            stack.push_front(child_ready_t(std::move(child_info)));
        } else {
            assert(type == proto::FileInfoType::FILE);
            if (!child_info.size || child_info.self) {
                stack.emplace_front(child_ready_t(std::move(child_info)));
            } else {
                auto block_size = [&]() -> std::int32_t {
                    // for possible correct importing later at local-update.
                    if (!child_info.self) {
                        using namespace presentation;
                        auto folder = context->local_folder->get_folder();
                        auto &folder_path = folder->get_path();
                        auto rel_path = fs::relativize(child_info.path, folder_path);
                        auto name = narrow(rel_path.generic_wstring());
                        auto folder_infos = folder->get_folder_infos();
                        for (auto &it : folder_infos) {
                            if (auto file = it.item->get_file_infos().by_name(name)) {
                                auto augmentation = file->get_augmentation().get();
                                auto file_presence = static_cast<cluster_file_presence_t *>(augmentation);
                                auto best = file_presence->get_entity()->get_best();
                                if (best && best->get_features() & F::cluster) {
                                    auto mutable_best = const_cast<presentation::presence_t *>(best);
                                    auto cp = static_cast<cluster_file_presence_t *>(mutable_best);
                                    auto &best_file = cp->get_file_info();
                                    auto match = best_file.is_file() && best_file.get_size() == child_info.size;
                                    if (match) {
                                        return best_file.get_block_size();
                                    }
                                }
                            }
                        }
                    }
                    return 0;
                }();
                auto ptr = hash_new_file_ptr_t(new hash_new_file_t(std::move(child_info), block_size));
                stack.emplace_front(std::move(ptr));
            }
        }
        return 1;
    }

    int process(suspend_scan_t &item, stack_context_t &ctx) noexcept {
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &ec = item.ec;
        LOG_WARN(log, "suspending '{}', due to: {}", folder_id, ec.message());
        ctx.push(new model::diff::modify::suspend_folder_t(*folder, true, ec));
        auto it = stack.begin();
        std::advance(it, 1);
        while (it != stack.end() && (&*it != &stack.back())) {
            it = stack.erase(it);
        }
        return 1;
    }

    int process(fatal_error_t &item, stack_context_t &ctx) noexcept {
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &ec = item.ec;
        auto ee = actor->make_error(ec);
        actor->do_shutdown(ee);
        LOG_WARN(actor->log, "severe error during processing {}: {}", folder_id, ec.message());
        auto it = stack.begin();
        std::advance(it, 1);
        while (it != stack.end() && (&*it != &stack.back())) {
            it = stack.erase(it);
        }
        force_completion = true;
        return 1;
    }

    int process(unsuspend_scan_t &, stack_context_t &ctx) noexcept {
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        LOG_TRACE(log, "un-suspending {}", folder_id);
        ctx.push(new model::diff::modify::suspend_folder_t(*folder, false));
        return 1;
    }

    int process(child_ready_t &info, stack_context_t &ctx) noexcept {
        using FT = proto::FileInfoType;
        bool emit_update = false;
        if (!info.self) {
            emit_update = true;
        } else {
            auto presence = static_cast<presentation::cluster_file_presence_t *>(info.self.get());
            auto &file = const_cast<model::file_info_t &>(presence->get_file_info());
            bool match = false;
            auto &type = info.type;
            auto modification_match = (type == FT::SYMLINK) || (info.last_write_time == file.get_modified_s());
            if (modification_match && info.perms == file.get_permissions()) {
                if (type == model::file_info_t::as_type(file.get_type())) {
                    if (type == FT::SYMLINK) {
                        auto target = narrow(info.link_target.generic_wstring());
                        match = file.get_link_target() == target;
                    } else {
                        match = file.get_size() == info.size;
                    }
                }
            }
            if (match) {
                using namespace model::diff;
                ctx.push(new local::file_availability_t(&file, *context->local_folder));
            } else {
                if (info.size && info.blocks.empty()) {
                    auto ptr = hash_existing_file_ptr_t(new hash_existing_file_t(std::move(info)));
                    stack.emplace_front(std::move(ptr));
                } else {
                    emit_update = true;
                }
            }
        }
        if (emit_update) {
            auto folder = context->local_folder->get_folder();
            auto folder_id = folder->get_id();
            auto data = info.serialize(*context->local_folder, std::move(info.blocks), ignore_permissions);
            ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer, std::move(data),
                                                              folder_id));
        }
        return 1;
    }

    int schedule_hash(hash_base_t *item, stack_context_t &ctx) noexcept {
        if (item->errored_blocks) {
            if (item->commit_hash()) {
                LOG_WARN(actor->log, "I/O error during processing '{}': {}", narrow(item->path.generic_wstring()),
                         item->ec.message());
            }

            return 1;
        }
        if (!actor->concurrent_hashes_left) {
            return -1;
        }
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &ec = item->ec;

        auto total_blocks = item->total_blocks;
        auto block_size = item->block_size;
        auto &blocks_limit = actor->concurrent_hashes_left;
        auto first_block = item->total_blocks - item->unprocessed_blocks;
        auto max_blocks = std::min(blocks_limit, item->unprocessed_blocks);
        auto last_block_sz = [&]() -> std::int32_t {
            if (item->unprocessed_blocks == max_blocks) {
                if (total_blocks > 1) {
                    auto sz = std::int64_t(block_size) * (total_blocks - 1);
                    return item->size - sz;
                }
            }
            return block_size;
        }();
        assert(last_block_sz > 0);
        auto offset = std::int64_t{first_block} * block_size;
        auto hash_context = hash_context_ptr_t(new hash_context_t(this, item));
        auto sub_task = fs::task::segment_iterator_t(actor->get_address(), hash_context, item->path, offset,
                                                     first_block, max_blocks, block_size, last_block_sz);
        pending_io.emplace_back(std::move(sub_task));
        blocks_limit -= max_blocks;
        auto blocks_left = item->unprocessed_blocks -= max_blocks;

        LOG_TRACE(log, "going to rehash {} block(s) ({}..{}) of '{}'", max_blocks, first_block,
                  first_block + max_blocks, narrow(item->path.wstring()));
        return blocks_left ? -1 : 1;
    }

    int process(hash_existing_file_ptr_t &item, stack_context_t &ctx) noexcept {
        return schedule_hash(item.get(), ctx);
    }

    int process(hash_new_file_ptr_t &item, stack_context_t &ctx) noexcept { return schedule_hash(item.get(), ctx); }

    int process(hash_incomplete_file_ptr_t &item, stack_context_t &ctx) noexcept {
        return schedule_hash(item.get(), ctx);
    }

    int process(removed_dir_t &item, stack_context_t &ctx) noexcept {
        auto folder_id = context->local_folder->get_folder()->get_id();
        auto dir = static_cast<presentation::local_file_presence_t *>(item.presence.get());
        auto dir_data = dir->get_file_info().as_proto(false);
        ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer, std::move(dir_data),
                                                          folder_id));
        for (auto child : item.presence->get_children()) {
            if (child->get_features() & F::directory) {
                stack.push_front(removed_dir_t(child));
            } else {
                auto file = static_cast<presentation::local_file_presence_t *>(child);
                auto file_data = file->get_file_info().as_proto(false);
                proto::set_deleted(file_data, true);
                ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer,
                                                                  std::move(file_data), folder_id));
            }
        }
        return 1;
    }

    int process(incomplete_t &item, stack_context_t &ctx) noexcept {
        auto name = narrow(item.path.stem().generic_wstring());
        auto name_view = std::string_view(name);
        auto self_device = actor->cluster->get_device().get();
        auto &entities = item.parent->get_entity()->get_children();
        auto comparator = presentation::entity_t::name_comparator_t{};
        auto it = std::lower_bound(entities.begin(), entities.end(), name_view, comparator);
        auto presence = (presentation::presence_t *)(nullptr);
        if (it != entities.end()) {
            presence = const_cast<presentation::presence_t *>((*it)->get_best());
            if (presence && presence->get_device() == self_device) {
                presence = nullptr;
            }
        }
        auto action = model::advance_action_t::ignore;
        if (presence) {
            auto cp = static_cast<const presentation::cluster_file_presence_t *>(presence);
            auto &peer_file = cp->get_file_info();
            if (peer_file.get_size() != item.size) {
                presence = nullptr;
            } else {
                auto local_file = (const model::file_info_t *)(nullptr);
                auto local_fi = context->local_folder.get();
                auto local_presence = presence->get_entity()->get_presence(self_device);
                if (local_presence->get_features() & F::cluster) {
                    auto cp = static_cast<const presentation::cluster_file_presence_t *>(local_presence);
                    local_file = &cp->get_file_info();
                }
                action = model::resolve(peer_file, local_file, *local_fi);
                if (action == model::advance_action_t::ignore) {
                    presence = nullptr;
                }
            }
        }

        if (!presence || action == model::advance_action_t::ignore) {
            LOG_DEBUG(log, "scheduling removal of '{}", narrow(item.path.generic_wstring()));
            auto sub_task = fs::task::remove_file_t(std::move(item.path));
            pending_io.emplace_back(std::move(sub_task));
        } else {
            LOG_TRACE(log, "scheduling rehashing of '{}", narrow(item.path.generic_wstring()));
            auto &child_info = static_cast<child_info_t &>(item);
            auto ptr = hash_incomplete_file_ptr_t(new hash_incomplete_file_t(std::move(child_info), presence, action));
            stack.emplace_front(std::move(ptr));
        }
        return 1;
    }

    int process(rehashed_incomplete_t &item, stack_context_t &ctx) noexcept {
        auto cp = static_cast<const presentation::cluster_file_presence_t *>(item.self.get());
        auto &peer_file = cp->get_file_info();
        auto &blocks = item.blocks;
        auto it = peer_file.iterate_blocks();
        auto schedule_removal = it.get_total() != static_cast<std::uint32_t>(blocks.size());
        if (!schedule_removal) {
            auto valid_blocks = model::diff::local::blocks_availability_t::valid_blocks_map_t();
            valid_blocks.resize(blocks.size());
            auto matched = blocks.size();
            for (size_t i = 0; i < blocks.size(); ++i, it.next()) {
                auto peer_block = it.current().first;
                auto &local_block = blocks[i];
                if (peer_block->get_hash() != proto::get_hash(local_block)) {
                    --matched;
                } else {
                    valid_blocks[i] = true;
                }
            }
            if (matched == blocks.size()) {
                LOG_DEBUG(log, "scheduling finalization of '{}", narrow(item.path.generic_wstring()));
                auto modified_s = peer_file.get_modified_s();
                auto name = [&]() -> bfs::path {
                    if (item.action == model::advance_action_t::remote_copy) {
                        return bfs::path(peer_file.get_name()->get_own_name());
                    } else {
                        assert(item.action == model::advance_action_t::resolve_remote_win);
                        auto self_device = actor->cluster->get_device().get();
                        auto local_presence = item.self->get_entity()->get_presence(self_device);
                        assert(local_presence->get_features() & F::cluster);
                        auto lp = static_cast<const presentation::cluster_file_presence_t *>(local_presence);
                        auto &local_file = cp->get_file_info();
                        return bfs::path(local_file.make_conflicting_name()).filename();
                    }
                }();
                auto rename_ctx = hasher::payload::extendended_context_prt_t();
                auto path_copy = item.path;
                rename_ctx = new rename_context_t(std::move(item));
                auto sub_task =
                    fs::task::rename_file_t(std::move(path_copy), std::move(name), modified_s, std::move(rename_ctx));
                pending_io.emplace_back(std::move(sub_task));
            } else {
                if (matched) {
                    using namespace model::diff::local;
                    auto &peer_folder = cp->get_folder()->get_folder_info();
                    ctx.push(new blocks_availability_t(peer_file, peer_folder, std::move(valid_blocks)));
                } else {
                    schedule_removal = true;
                }
            }
        }
        if (schedule_removal) {
            LOG_DEBUG(log, "scheduling removal of '{}", narrow(item.path.generic_wstring()));
            auto sub_task = fs::task::remove_file_t(std::move(item.path));
            pending_io.emplace_back(std::move(sub_task));
        }
        return 1;
    }

    bool post_process() noexcept {
        auto folder_id = context->local_folder->get_folder()->get_id();
        LOG_TRACE(log, "postpocess of '{}'", folder_id);
        auto ctx = stack_context_t();

        for (auto &t : tasks_out) {
            std::visit([&](auto &t) { post_process(t, ctx); }, t);
        }
        tasks_out.clear();

        process_stack(ctx);

        return pending_io.size();
    }

    void prepare_task() {
        assert(!pending_io.empty());
        auto &t = pending_io.front();
        tasks_in.emplace_back(std::move(t));
        pending_io.pop_front();
    }

    bool post_process(hash_base_t &hash_file, hasher::message::digest_t &msg) noexcept {
        auto &p = msg.payload;
        auto &result = msg.payload.result;
        if (result.has_error()) {
            auto &ec = result.assume_error();
            stack.push_front(fatal_error_t(ec));
        } else {
            auto index = p.block_index;
            auto offset = index * hash_file.block_size;
            auto bi = proto::BlockInfo();
            proto::set_offset(bi, offset);
            proto::set_size(bi, static_cast<std::int32_t>(p.data.size()));
            proto::set_hash(bi, std::move(result).assume_value());
            hash_file.blocks[index] = std::move(bi);
            --hash_file.unhashed_blocks;
            if (!hash_file.unhashed_blocks) {
                auto blocks = std::move(hash_file.blocks);
                auto copy = static_cast<child_info_t &>(hash_file);
                if (hash_file.incomplete) {
                    stack.push_front(rehashed_incomplete_t(std::move(copy), std::move(blocks), hash_file.action));
                } else {
                    stack.push_front(child_ready_t(std::move(copy), std::move(blocks)));
                }
            }
            ++actor->concurrent_hashes_left;
        }
        return post_process();
    }

    void post_process(fs::task::scan_dir_t &task, stack_context_t &ctx) noexcept {
        using checked_chidren_t = std::pmr::set<std::string_view>;
        auto &ec = task.ec;
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &root_path = folder->get_path();
        bool is_root = task.path == folder->get_path();
        if (is_root) {
            if (ec) {
                stack.push_front(suspend_scan_t(ec));
                return;
            } else {
                if (folder->is_suspended()) {
                    stack.push_front(unsuspend_scan_t());
                    return;
                }
            }
        }

        if (task.ec) {
            actor->log->warn("cannot scan '{}': {}", narrow(task.path.wstring()), ec.message());
        }

        auto parent_presence = task.presence.get();
        auto buffer = std::array<std::byte, 1024 * 128>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = allocator_t(&pool);
        auto checked_children = checked_chidren_t(allocator);

        auto it_disk = task.child_infos.begin();
        while (it_disk != task.child_infos.end()) {
            auto &info = *it_disk;
            auto is_dir = info.status.type() == bfs::file_type::directory;
            auto presence = get_presence(task.presence.get(), info.path, is_dir);
            if (presence) {
                auto filename = presence->get_entity()->get_path()->get_own_name();
                checked_children.emplace(filename);
            }
            if (info.ec) {
                actor->log->warn("scannig of  {} failed: {}", narrow(info.path.wstring()), info.ec.message());
            } else {
                if (fs::is_temporal(info.path)) {
                    auto child = incomplete_t(std::move(info), presence, task.presence);
                    stack.push_front(std::move(child));
                } else {
                    auto child = child_info_t(std::move(info), presence, task.presence);
                    stack.push_front(unexamined_t(std::move(child)));
                }
            }
            ++it_disk;
        }

        if (parent_presence) {
            for (auto child : parent_presence->get_children()) {
                auto features = child->get_features();
                if (features & F::local) {
                    auto filename = child->get_entity()->get_path()->get_own_name();
                    if (!checked_children.count(filename)) {
                        checked_children.emplace(filename);
                        if (features & F::directory) {
                            stack.push_front(removed_dir_t(child));
                        } else {
                            auto file = static_cast<presentation::local_file_presence_t *>(child);
                            auto file_data = file->get_file_info().as_proto(false);
                            proto::set_deleted(file_data, true);
                            ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer,
                                                                              std::move(file_data), folder_id));
                        }
                    }
                }
            }
            using queue_t = std::pmr::list<presentation::entity_t *>;
            auto queue = queue_t(allocator);
            for (auto child_entity : parent_presence->get_entity()->get_children()) {
                auto filename = child_entity->get_path()->get_own_name();
                if (!checked_children.count(filename)) {
                    auto best = child_entity->get_best();
                    if (best->get_features() & F::deleted) {
                        queue.emplace_back(child_entity);
                    }
                }
            }
            while (!queue.empty()) {
                auto child_entity = queue.front();
                queue.pop_back();
                auto best = child_entity->get_best();
                auto presence = static_cast<const presentation::cluster_file_presence_t *>(best);
                auto &peer_file = presence->get_file_info();
                auto pr_file = peer_file.as_proto(true);
                ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer,
                                                                  std::move(pr_file), folder_id));
                if (best->get_features() & F::directory) {
                    for (auto c : child_entity->get_children()) {
                        auto best = child_entity->get_best();
                        if (best->get_features() & F::deleted) {
                            queue.emplace_back(c);
                        }
                    }
                }
            }
        }
    }

    void post_process(fs::task::segment_iterator_t &task, stack_context_t &ctx) noexcept {
        auto &ec = task.ec;
        if (ec) {
            auto hash_ctx = static_cast<hash_context_t *>(task.context.get());
            auto delta = task.block_count - task.current_block;
            this->actor->concurrent_hashes_left += delta;
            if (hash_ctx->hash_file->commit_error(ec, delta)) {
                LOG_WARN(actor->log, "I/O error during processing '{}': {}", narrow(task.path.generic_wstring()),
                         ec.message());
            }
        }
    }

    void post_process(fs::task::remove_file_t &task, stack_context_t &ctx) noexcept {
        auto &ec = task.ec;
        if (ec) {
            LOG_WARN(log, "(ignored) cannot remove '{}': {}", narrow(task.path.generic_wstring()), ec.message());
        }
    }

    void post_process(fs::task::rename_file_t &task, stack_context_t &ctx) noexcept {
        auto raname_ctx = static_cast<rename_context_t *>(task.context.get());
        auto &ec = task.ec;
        if (ec) {
            auto &path = task.path;
            LOG_WARN(log, "cannot rename '{}' -> {}: {}, going to remove", narrow(path.generic_wstring()),
                     narrow(task.new_name.generic_wstring()), ec.message());
            auto sub_task = fs::task::remove_file_t(std::move(path));
            pending_io.emplace_back(std::move(sub_task));
        } else {
            auto &item = raname_ctx->item;
            auto cp = static_cast<const presentation::cluster_file_presence_t *>(item.self.get());
            auto &peer_file = cp->get_file_info();
            auto peer = cp->get_device();
            auto &peer_folder = cp->get_folder()->get_folder_info();
            auto diff = model::diff::advance::advance_t::create(item.action, peer_file, peer_folder, *actor->sequencer);
            ctx.push(diff.get());
        }
    }

    presentation::presence_t *get_presence(presentation::presence_t *parent, const bfs::path &path, bool is_dir) {
        if (!parent) {
            return nullptr;
        }
        LOG_TRACE(log, "get_presence for {}", path.string());
        auto &children = parent->get_children();
        auto comparator = presentation::presence_t::child_comparator_t{};
        auto own_name = narrow(path.filename().generic_wstring());
        auto presence_like = presentation::presence_t::presence_like_t{own_name, is_dir};
        auto it = std::lower_bound(children.begin(), children.end(), presence_like, comparator);
        if (it != children.end()) {
            auto &p = *it;
            if (!(p->get_features() & F::missing)) {
                return p;
            }
        }
        return nullptr;
    }

    tasks_t pending_io;
    stack_t stack;
    folder_context_ptr_t context;
    local_keeper_ptr_t actor;
    utils::logger_t log;
    bool force_completion = false;
    bool ignore_permissions;
};

} // namespace

local_keeper_t::local_keeper_t(config_t &config)
    : r::actor_base_t(config), sequencer{std::move(config.sequencer)},
      concurrent_hashes_left{static_cast<std::int32_t>(config.concurrent_hashes)},
      concurrent_hashes_limit{concurrent_hashes_left} {
    assert(sequencer);
    assert(concurrent_hashes_left);
}

void local_keeper_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.local_keeper", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::fs_actor, fs_addr, false).link(false);
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&local_keeper_t::on_model_update, coordinator);
                plugin->subscribe_actor(&local_keeper_t::on_thread_ready, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&local_keeper_t::on_post_process);
        p.subscribe_actor(&local_keeper_t::on_digest);
    });
}

void local_keeper_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void local_keeper_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    r::actor_base_t::shutdown_start();
}

void local_keeper_t::on_model_update(model::message::model_update_t &msg) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *msg.payload.diff;
    auto r = diff.visit(*this, const_cast<void *>(msg.payload.custom));
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void local_keeper_t::on_thread_ready(model::message::thread_ready_t &message) noexcept {
    auto &p = message.payload;
    if (p.thread_id == std::this_thread::get_id()) {
        LOG_TRACE(log, "on_thread_ready");
        cluster = message.payload.cluster;
    }
}

auto local_keeper_t::operator()(const model::diff::local::scan_start_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    bool do_scan = true;
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    if ((folder->is_suspended() && !folder->get_suspend_reason()) || state != r::state_t::OPERATIONAL) {
        do_scan = false;
    }
    if (do_scan) {
        LOG_DEBUG(log, "initiating scan of {}", diff.folder_id);
        auto local_folder = folder->get_folder_infos().by_device(*cluster->get_device());
        auto ctx = folder_context_ptr_t(new folder_context_t(local_folder));
        auto backend = new folder_slave_t(std::move(ctx), this);
        backend->process_stack();
        backend->prepare_task();
        auto slave = fs::payload::foreign_executor_prt_t(backend);
        route<fs::payload::foreign_executor_prt_t>(fs_addr, address, std::move(slave));
        ++fs_tasks;
    } else {
        LOG_DEBUG(log, "skipping scan of {}", diff.folder_id);
    }
    return diff.visit_next(*this, custom);
}

void local_keeper_t::on_post_process(fs::message::foreign_executor_t &msg) noexcept {
    --fs_tasks;
    LOG_TRACE(log, "on_post_process, active tasks: {}", fs_tasks);
    assert(fs_tasks >= 0);
    if (state == r::state_t::OPERATIONAL) {
        auto &slave = static_cast<folder_slave_t &>(*msg.payload.get());
        auto folder_id = slave.context->local_folder->get_folder()->get_id();
        auto has_pending = slave.post_process();
        if (has_pending && fs_tasks == 0) {
            if (slave.ec) {
                LOG_ERROR(log, "cannot process folder any longer: {}", slave.ec.message());
            } else {
                slave.prepare_task();
                slave.ec = utils::make_error_code(utils::error_code_t::no_action);
                redirect(&msg, fs_addr, address);
                LOG_TRACE(log, "redirected {}", (void *)&slave);
                ++fs_tasks;
            }
        }
    } else {
        LOG_DEBUG(log, "skipping post-processing of foreign executor (non-operational)");
    }
}

void local_keeper_t::on_digest(hasher::message::digest_t &msg) noexcept {
    auto &p = msg.payload;
    LOG_TRACE(log, "on_digest, block size: {}, index: {}", p.data.size(), p.block_index);
    if (state == r::state_t::OPERATIONAL) {
        auto hash_ctx = *static_cast<hash_context_t *>(p.context.get());
        auto &slave = *hash_ctx.slave.get();
        auto has_pending = slave.post_process(*hash_ctx.hash_file.get(), msg);
        if (has_pending && fs_tasks == 0) {
            slave.prepare_task();
            LOG_TRACE(log, "routed {}", (void *)&slave);
            route<fs::payload::foreign_executor_prt_t>(fs_addr, address, std::move(hash_ctx.slave));
            ++fs_tasks;
        }
    } else {
        LOG_DEBUG(log, "skipping post-processing of hashed digest (non-operational)");
    }
}

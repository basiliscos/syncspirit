// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "folder_slave.h"
#include "fs/utils.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/local/blocks_availability.h"
#include "model/diff/local/file_availability.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/modify/mark_reachable.h"
#include "model/diff/modify/suspend_folder.h"
#include "net/local_keeper.h"
#include "presentation/folder_entity.h"
#include "presentation/folder_presence.h"
#include "presentation/local_file_presence.h"
#include "proto/proto-helpers-bep.h"
#include "utils/platform.h"

#include <boost/nowide/convert.hpp>

using namespace syncspirit::net;
using namespace syncspirit::net::local_keeper;
using boost::nowide::narrow;

folder_slave_t::folder_slave_t(folder_context_ptr_t context_, local_keeper_ptr_t actor_) noexcept
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

folder_slave_t::~folder_slave_t() {}

void folder_slave_t::process_stack(stack_context_t &ctx) noexcept {
    auto try_next = true;
    while (!stack.empty() && try_next) {
        auto it = stack.begin();
        auto &item = *it;
        auto r = std::visit([&](auto &item) { return process(item, ctx); }, item);
        if (r >= 0) {
            stack.erase(it);
        }
        try_next = r > 0;
        if (try_next) {
            if (ctx.diffs_left <= 0 && !stack.empty() && pending_io.empty()) {
                pending_io.emplace_back(fs::task::noop_t());
                break;
            }
        }
    }
    if (ctx.diff) {
        actor->send<model::payload::model_update_t>(actor->coordinator, std::move(ctx.diff));
    }
}

int folder_slave_t::schedule_hash(hash_base_t *item, stack_context_t &ctx) noexcept {
    if (item->errored_blocks) {
        if (item->commit_hash()) {
            LOG_WARN(log, "I/O error during processing '{}': {}", narrow(item->path.generic_wstring()),
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
    auto sub_task = fs::task::segment_iterator_t(actor->get_address(), hash_context, item->path, offset, first_block,
                                                 max_blocks, block_size, last_block_sz);
    pending_io.emplace_back(std::move(sub_task));
    blocks_limit -= max_blocks;
    auto blocks_left = item->unprocessed_blocks -= max_blocks;

    LOG_TRACE(log, "going to rehash {} block(s) ({}..{}) of '{}'", max_blocks, first_block, first_block + max_blocks,
              narrow(item->path.wstring()));
    return blocks_left ? -1 : 1;
}
void folder_slave_t::prepare_task() {
    assert(!pending_io.empty());
    auto &t = pending_io.front();
    tasks_in.emplace_back(std::move(t));
    pending_io.pop_front();
}

auto folder_slave_t::get_presence(presentation::presence_t *parent, const bfs::path &path, bool is_dir)
    -> presentation::presence_t * {
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

int folder_slave_t::process(complete_scan_t &, stack_context_t &ctx) noexcept {
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

int folder_slave_t::process(unscanned_dir_t &dir, stack_context_t &ctx) noexcept {
    auto sub_task = fs::task::scan_dir_t(std::move(dir.path), std::move(dir.presence), std::move(dir.dir_info));
    pending_io.emplace_back(std::move(sub_task));
    return 0;
}

int folder_slave_t::process(unexamined_t &child_info, stack_context_t &ctx) noexcept {
    auto &type = child_info.type;
    if (type == proto::FileInfoType::DIRECTORY) {
        auto self = child_info.self;
        stack.push_front(unscanned_dir_t(std::move(child_info)));
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

int folder_slave_t::process(suspend_scan_t &item, stack_context_t &ctx) noexcept {
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

int folder_slave_t::process(fatal_error_t &item, stack_context_t &ctx) noexcept {
    auto folder = context->local_folder->get_folder();
    auto folder_id = folder->get_id();
    auto &ec = item.ec;
    auto ee = actor->make_error(ec);
    actor->do_shutdown(ee);
    LOG_WARN(log, "severe error during processing {}: {}", folder_id, ec.message());
    auto it = stack.begin();
    std::advance(it, 1);
    while (it != stack.end() && (&*it != &stack.back())) {
        it = stack.erase(it);
    }
    force_completion = true;
    return 1;
}

int folder_slave_t::process(unsuspend_scan_t &, stack_context_t &ctx) noexcept {
    auto folder = context->local_folder->get_folder();
    auto folder_id = folder->get_id();
    LOG_TRACE(log, "un-suspending {}", folder_id);
    ctx.push(new model::diff::modify::suspend_folder_t(*folder, false));
    return 1;
}

int folder_slave_t::process(child_ready_t &info, stack_context_t &ctx) noexcept {
    using FT = proto::FileInfoType;
    using namespace model::diff;
    bool emit_update = false;
    if (!info.self) {
        emit_update = true;
    } else {
        auto presence = static_cast<presentation::cluster_file_presence_t *>(info.self.get());
        auto &file = const_cast<model::file_info_t &>(presence->get_file_info());
        bool match = false;
        auto &type = info.type;
        auto modification_match =
            (type == FT::SYMLINK) || (type == FT::DIRECTORY) || (info.last_write_time == file.get_modified_s());
        if (modification_match) {
            if (type == model::file_info_t::as_type(file.get_type())) {
                auto ignore_perms = ignore_permissions || file.has_no_permissions();
                auto perms_match = ignore_perms || info.perms == file.get_permissions();
                if (perms_match) {
                    if (type == FT::SYMLINK) {
                        auto target = narrow(info.link_target.generic_wstring());
                        match = file.get_link_target() == target;
                    } else {
                        match = file.get_size() == info.size;
                    }
                }
            }
        }
        if (match) {
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
        ctx.push(new advance::local_update_t(*actor->cluster, *actor->sequencer, std::move(data), folder_id));
    }
    return 1;
}

int folder_slave_t::process(hash_existing_file_ptr_t &item, stack_context_t &ctx) noexcept {
    return schedule_hash(item.get(), ctx);
}

int folder_slave_t::process(hash_new_file_ptr_t &item, stack_context_t &ctx) noexcept {
    return schedule_hash(item.get(), ctx);
}

int folder_slave_t::process(hash_incomplete_file_ptr_t &item, stack_context_t &ctx) noexcept {
    return schedule_hash(item.get(), ctx);
}

int folder_slave_t::process(removed_dir_t &item, stack_context_t &ctx) noexcept {
    auto folder_id = context->local_folder->get_folder()->get_id();
    auto dir = static_cast<presentation::local_file_presence_t *>(item.presence.get());
    auto dir_data = dir->get_file_info().as_proto(false);
    proto::set_deleted(dir_data, true);
    ctx.push(
        new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer, std::move(dir_data), folder_id));
    auto dirs_stack = dirs_stack_t(stack);
    auto children = item.presence->get_children();
    for (auto child : item.presence->get_children()) {
        if (child->get_features() & F::directory) {
            dirs_stack.push_front(removed_dir_t(child));
        } else {
            auto file = static_cast<presentation::local_file_presence_t *>(child);
            auto file_data = file->get_file_info().as_proto(false);
            proto::set_deleted(file_data, true);
            ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer, std::move(file_data),
                                                              folder_id));
        }
    }
    return 1;
}

int folder_slave_t::process(confirmed_deleted_t &item, stack_context_t &ctx) {
    auto dir_presence = static_cast<presentation::local_file_presence_t *>(item.presence.get());
    auto &dir = const_cast<model::file_info_t &>(dir_presence->get_file_info());
    ctx.push(new model::diff::local::file_availability_t(&dir, *context->local_folder));

    auto dirs_stack = dirs_stack_t(stack);
    for (auto child : item.presence->get_children()) {
        auto f = child->get_features();
        if (f & F::local) {
            if (f & F::directory) {
                dirs_stack.push_front(confirmed_deleted_t(child));
            } else {
                auto file_presence = static_cast<presentation::local_file_presence_t *>(child);
                auto &file = const_cast<model::file_info_t &>(file_presence->get_file_info());
                ctx.push(new model::diff::local::file_availability_t(&file, *context->local_folder));
            }
        }
    }
    return 1;
}
int folder_slave_t::process(incomplete_t &item, stack_context_t &ctx) noexcept {
    auto name = narrow(item.path.stem().generic_wstring());
    auto name_view = std::string_view(name);
    auto self_device = actor->cluster->get_device().get();
    auto &entities = item.parent->get_entity()->get_children();
    auto comparator = presentation::entity_t::name_comparator_t{};
    auto it = std::lower_bound(entities.begin(), entities.end(), name_view, comparator);
    auto presence = (presentation::presence_t *)(nullptr);
    auto ignore = false;
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
        } else if (peer_file.is_synchronizing()) {
            LOG_DEBUG(log, "ignoring '{}' (synchronizing)", narrow(item.path.generic_wstring()));
            ignore = true;
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

    if (!ignore) {
        if (!presence || action == model::advance_action_t::ignore) {
            LOG_DEBUG(log, "scheduling removal of '{}'", narrow(item.path.generic_wstring()));
            auto sub_task = fs::task::remove_file_t(std::move(item.path));
            pending_io.emplace_back(std::move(sub_task));
        } else {
            LOG_TRACE(log, "scheduling rehashing of '{}'", narrow(item.path.generic_wstring()));
            auto &child_info = static_cast<child_info_t &>(item);
            auto ptr = hash_incomplete_file_ptr_t(new hash_incomplete_file_t(std::move(child_info), presence, action));
            stack.emplace_front(std::move(ptr));
        }
    }
    return 1;
}

int folder_slave_t::process(rehashed_incomplete_t &item, stack_context_t &ctx) noexcept {
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

bool folder_slave_t::post_process() noexcept {
    auto folder_id = context->local_folder->get_folder()->get_id();
    LOG_TRACE(log, "postpocess of '{}'", folder_id);

    auto ctx = stack_context_t(actor->files_scan_iteration_limit);

    for (auto &t : tasks_out) {
        std::visit([&](auto &t) { post_process(t, ctx); }, t);
    }
    tasks_out.clear();

    process_stack(ctx);

    return pending_io.size();
}

bool folder_slave_t::post_process(hash_base_t &hash_file, hasher::message::digest_t &msg) noexcept {
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

void folder_slave_t::post_process(fs::task::scan_dir_t &task, stack_context_t &ctx) noexcept {
    using checked_chidren_t = std::pmr::set<std::string_view>;
    auto &ec = task.ec;
    auto folder = context->local_folder->get_folder();
    auto folder_id = folder->get_id();
    auto &root_path = folder->get_path();
    bool is_root = !task.payload; // task.path == folder->get_path();
    if (is_root) {
        if (ec) {
            stack.push_front(suspend_scan_t(ec));
            return;
        } else {
            if (folder->is_suspended()) {
                stack.push_front(unsuspend_scan_t());
            }
        }
    }

    if (task.ec) {
        return handle_scan_error(task, ctx);
    } else if (!is_root) {
        auto dir_info = static_cast<child_info_t *>(task.payload.get());
        stack.push_front(child_ready_t(std::move(*dir_info)));
    }

    auto dir_presence = task.presence.get();
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
            log->warn("scannig of  {} failed: {}", narrow(info.path.wstring()), info.ec.message());
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

    if (dir_presence) {
        auto dirs_stack = dirs_stack_t(stack);
        for (auto child : dir_presence->get_children()) {
            auto features = child->get_features();
            if (features & F::local) {
                auto filename = child->get_entity()->get_path()->get_own_name();
                if (!checked_children.count(filename)) {
                    checked_children.emplace(filename);
                    auto is_dir = features & F::directory;
                    if (!(features & F::deleted)) {
                        if (is_dir) {
                            dirs_stack.push_front(removed_dir_t(child));
                        } else {
                            auto file = static_cast<presentation::local_file_presence_t *>(child);
                            auto file_data = file->get_file_info().as_proto(false);
                            proto::set_deleted(file_data, true);
                            ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer,
                                                                              std::move(file_data), folder_id));
                        }
                    } else {
                        auto &target_stack = is_dir ? dirs_stack : stack;
                        dirs_stack.push_front(confirmed_deleted_t(child));
                    }
                }
            }
        }

        using queue_t = std::pmr::list<presentation::entity_t *>;
        auto queue = queue_t(allocator);
        for (auto child_entity : dir_presence->get_entity()->get_children()) {
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
            ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer, std::move(pr_file),
                                                              folder_id));
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

void folder_slave_t::post_process(fs::task::segment_iterator_t &task, stack_context_t &ctx) {
    auto &ec = task.ec;
    if (ec) {
        auto hash_ctx = static_cast<hash_context_t *>(task.context.get());
        auto delta = task.block_count - task.current_block;
        actor->concurrent_hashes_left += delta;
        auto &hash_file = *hash_ctx->hash_file;
        if (hash_file.commit_error(ec, delta)) {
            auto path_str = narrow(task.path.generic_wstring());
            LOG_WARN(log, "I/O error during processing '{}': {}", path_str, ec.message());
            auto presence = hash_file.self.get();
            if (presence && presence->get_features() & F::local) {
                auto file_presence = static_cast<presentation::local_file_presence_t *>(presence);
                auto &file = const_cast<model::file_info_t &>(file_presence->get_file_info());
                auto local_fi = context->local_folder.get();
                ctx.push(new model::diff::modify::mark_reachable_t(file, *local_fi, false));
            } else if (hash_file.incomplete) {
                LOG_DEBUG(log, "scheduling removal of '{}", path_str);
                auto sub_task = fs::task::remove_file_t(task.path);
                pending_io.emplace_back(std::move(sub_task));
            }
        }
    }
}

void folder_slave_t::post_process(fs::task::remove_file_t &task, stack_context_t &ctx) noexcept {
    auto &ec = task.ec;
    if (ec) {
        LOG_WARN(log, "(ignored) cannot remove '{}': {}", narrow(task.path.generic_wstring()), ec.message());
    }
}

void folder_slave_t::post_process(fs::task::rename_file_t &task, stack_context_t &ctx) noexcept {
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

void folder_slave_t::post_process(fs::task::noop_t &, stack_context_t &) noexcept {}

void folder_slave_t::handle_scan_error(fs::task::scan_dir_t &task, stack_context_t &ctx) noexcept {
    auto &ec = task.ec;
    log->warn("cannot scan '{}': {}", narrow(task.path.wstring()), ec.message());
    auto dir_presence = task.presence.get();
    if (dir_presence && dir_presence->get_features() & F::local) {
        auto buffer = std::array<std::byte, 1024 * 128>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = allocator_t(&pool);
        using queue_t = std::pmr::list<presentation::presence_t *>;
        auto queue = queue_t(allocator);
        queue.emplace_back(dir_presence);

        auto local_fi = context->local_folder.get();
        while (!queue.empty()) {
            auto presence = queue.front();
            queue.pop_front();
            auto features = presence->get_features();
            auto file_presence = static_cast<presentation::local_file_presence_t *>(presence);
            auto &file = file_presence->get_file_info();
            auto diff = model::diff::cluster_diff_ptr_t();
            diff.reset(new model::diff::modify::mark_reachable_t(file, *local_fi, false));
            ctx.push(diff.get());

            for (auto &child : presence->get_children()) {
                if (child->get_features() & F::local) {
                    queue.emplace_back(child);
                }
            }
        }
    }
}

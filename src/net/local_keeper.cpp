// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "local_keeper.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/local/file_availability.h"
#include "model/diff/modify/suspend_folder.h"
#include "presentation/folder_entity.h"
#include "fs/fs_slave.h"
#include "fs/utils.h"
#include "names.h"
#include "presentation/local_file_presence.h"
#include "presentation/presence.h"
#include "presentation/cluster_file_presence.h"
#include "proto/proto-helpers-bep.h"

#include <algorithm>
#include <memory_resource>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::net;

namespace bfs = std::filesystem;
namespace sys = boost::system;

using boost::nowide::narrow;

namespace {

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

    proto::FileInfo serialize(const model::folder_info_t &local_folder, blocks_t blocks) {
        auto data = proto::FileInfo();
        auto name = fs::relativize(path, local_folder.get_folder()->get_path());
        proto::set_name(data, narrow(name.generic_wstring()));
        proto::set_type(data, type);
        proto::set_modified_s(data, last_write_time);
        proto::set_permissions(data, perms);
        if (size) {
            auto block_size = proto::get_size(blocks.front());
            proto::set_block_size(data, block_size);
            proto::set_size(data, size);
            proto::set_blocks(data, std::move(blocks));
        }
#if 0
        if (ignore_permissions == false) {
            auto permissions = static_cast<uint32_t>(status.permissions());
            proto::set_permissions(metadata, permissions);
        } else {
            proto::set_permissions(metadata, 0666);
            proto::set_no_permissions(metadata, true);
        }
#endif
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
struct child_ready_t : child_info_t {
    child_ready_t(child_info_t info, blocks_t blocks_ = {})
        : child_info_t{std::move(info)}, blocks{std::move(blocks_)} {}
    blocks_t blocks;
};
struct hash_base_t : fs::payload::extendended_context_t, child_info_t {
    using blocks_t = std::vector<proto::BlockInfo>;
    hash_base_t(child_info_t &&info_, fs::payload::extendended_context_prt_t context_)
        : child_info_t{std::move(info_)}, context{std::move(context_)} {
        auto div = fs::get_block_size(size, 0);
        block_size = div.size;
        unprocessed_blocks = unhashed_blocks = total_blocks = div.count;
        blocks.resize(total_blocks);
    }

    bool commit_error(std::int32_t delta) {
        errored_blocks += delta;
        return errored_blocks + unprocessed_blocks == unhashed_blocks;
    }

    std::int32_t block_size;
    std::int32_t total_blocks;
    std::int32_t unprocessed_blocks;
    std::int32_t unhashed_blocks;
    std::int32_t errored_blocks = 0;
    blocks_t blocks;
    fs::payload::extendended_context_prt_t context;
};

struct hash_new_file_t : hash_base_t {
    using hash_base_t::hash_base_t;
};

struct hash_existing_file_t : hash_base_t {
    using hash_base_t::hash_base_t;
};

struct check_child_t : child_info_t {
    check_child_t(child_info_t &&info, presentation::presence_t *presence_)
        : child_info_t{std::move(info)}, presence{presence_} {}
    presentation::presence_ptr_t presence;
};

using hash_new_file_ptr_t = boost::intrusive_ptr<hash_new_file_t>;
using hash_existing_file_ptr_t = boost::intrusive_ptr<hash_existing_file_t>;

struct complete_scan_t {};
struct suspend_scan_t {
    sys::error_code ec;
};
struct unsuspend_scan_t {};
struct removed_dir_t {
    presentation::presence_ptr_t presence;
};

using stack_item_t =
    std::variant<unscanned_dir_t, unexamined_t, complete_scan_t, child_ready_t, hash_new_file_ptr_t,
                 hash_existing_file_ptr_t, check_child_t, removed_dir_t, suspend_scan_t, unsuspend_scan_t>;

struct folder_slave_t final : fs::fs_slave_t {
    using local_keeper_ptr_t = r::intrusive_ptr_t<net::local_keeper_t>;
    using stack_t = std::list<stack_item_t>;

    folder_slave_t(folder_context_ptr_t context_, local_keeper_ptr_t actor_) noexcept
        : context{context_}, actor{std::move(actor_)} {
        log = actor->log;
        auto folder = context->local_folder->get_folder();
        auto augmentation = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
        auto local_device = folder->get_cluster()->get_device();
        auto folder_presence = folder_entity->get_presence(local_device.get());
        auto path = folder->get_path();
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
        using clock_t = r::pt::microsec_clock;
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto now = clock_t::local_time();
        ctx.push(new model::diff::local::scan_finish_t(folder_id, now));
        return 0;
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
                stack.push_front(child_ready_t(std::move(child_info)));
            } else {
                auto ptr = hash_new_file_ptr_t(new hash_new_file_t(std::move(child_info), this));
                stack.emplace_back(std::move(ptr));
            }
        }
        return 1;
    }

    int process(suspend_scan_t &item, stack_context_t &ctx) noexcept {
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &ec = item.ec;
        LOG_TRACE(log, "suspending '{}', due to: {}", folder_id, ec.message());
        ctx.push(new model::diff::modify::suspend_folder_t(*folder, true, ec));
        while (stack.size() > 1) {
            stack.pop_front();
        }
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
                    auto ptr = hash_existing_file_ptr_t(new hash_existing_file_t(std::move(info), this));
                    stack.emplace_back(std::move(ptr));
                } else {
                    emit_update = true;
                }
            }
        }
        if (emit_update) {
            auto folder = context->local_folder->get_folder();
            auto folder_id = folder->get_id();
            auto data = info.serialize(*context->local_folder, std::move(info.blocks));
            ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer, std::move(data),
                                                              folder_id));
        }
        return 1;
    }

    int schedule_hash(hash_base_t *item, stack_context_t &ctx) noexcept {
        if (!actor->requested_hashes_limit) {
            return -1;
        }
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &ec = item->ec;

        auto total_blocks = item->total_blocks;
        auto block_size = item->block_size;
        auto &blocks_limit = actor->requested_hashes_limit;
        auto processed_blocks = item->total_blocks - item->unprocessed_blocks;
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
        auto offset = std::int64_t{processed_blocks} * block_size;
        auto task_ctx = hasher::payload::extendended_context_prt_t(item);
        auto sub_task = fs::task::segment_iterator_t(actor->get_address(), item, item->path, offset, processed_blocks,
                                                     max_blocks, block_size, last_block_sz);
        pending_io.emplace_back(std::move(sub_task));
        blocks_limit -= max_blocks;
        item->unprocessed_blocks -= max_blocks;

        LOG_TRACE(log, "going to rehash {} blocks of '{}'", max_blocks, narrow(item->path.wstring()));
        return item->unprocessed_blocks ? -1 : 1;
    }

    int process(hash_existing_file_ptr_t &item, stack_context_t &ctx) noexcept {
        return schedule_hash(item.get(), ctx);
    }

    int process(hash_new_file_ptr_t &item, stack_context_t &ctx) noexcept { return schedule_hash(item.get(), ctx); }

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

    int process(check_child_t &item, stack_context_t &ctx) noexcept { std::abort(); }

    bool post_process() noexcept {
        auto folder_id = context->local_folder->get_folder()->get_id();
        LOG_TRACE(log, "postpocess of '{}'", folder_id);
        auto ctx = stack_context_t();

        for (auto &t : tasks_out) {
            std::visit([&](auto &t) { post_process(t, ctx); }, t);
        }
        tasks_out.clear();

        process_stack(ctx);

        if (!pending_io.empty()) {
            auto &t = pending_io.front();
            tasks_in.emplace_back(std::move(t));
            pending_io.pop_front();
        }

        return tasks_in.empty();
    }

    bool post_process(hash_base_t &hash_file, hasher::message::digest_t &msg) noexcept {
        auto &p = msg.payload;
        auto &result = msg.payload.result;
        if (result.has_error()) {
            std::abort();
        }
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
            stack.push_front(child_ready_t(std::move(copy), std::move(blocks)));
        }
        ++actor->requested_hashes_limit;
        return post_process();
    }

    void post_process(fs::task::scan_dir_t &task, stack_context_t &ctx) noexcept {
        using checked_chidren_t = std::pmr::set<presentation::presence_t *>;
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
            auto presence = get_presence(task.presence.get(), info.path);
            if (presence) {
                checked_children.emplace(presence);
            }
            if (info.ec) {
                actor->log->warn("scannig of  {} failed: {}", narrow(info.path.wstring()), info.ec.message());
            } else {
                auto child = child_info_t(std::move(info), presence, task.presence);
                stack.push_front(unexamined_t(std::move(child)));
            }
            ++it_disk;
        }

        if (parent_presence) {
            for (auto child : parent_presence->get_children()) {
                if (!checked_children.count(child)) {
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
            }
        }
    }

    void post_process(fs::task::segment_iterator_t &task, stack_context_t &ctx) noexcept {
        if (task.ec) {
            auto ctx = static_cast<hash_base_t *>(task.context.get());
            auto delta = task.block_count - task.current_block;
            if (ctx->commit_error(delta)) {
                LOG_WARN(actor->log, "I/O error during processing '{}': {}", narrow(task.path.generic_wstring()),
                         task.ec.message());
            }
        }
    }

    presentation::presence_t *get_presence(presentation::presence_t *parent, const bfs::path &path) {
        struct comparator_t {
            bool operator()(const presentation::presence_t *p, const bfs::path &name) const {
                auto buffer = std::array<std::byte, 1024>();
                auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
                auto allocator = allocator_t(&pool);
                auto own_wname = std::pmr::wstring(allocator);
                auto cp = static_cast<const presentation::cluster_file_presence_t *>(p);
                auto own_name = cp->get_file_info().get_name()->get_own_name();
                own_wname.resize(own_name.size() + 1);
                auto b = own_name.data();
                auto e = b + own_name.size();
                auto str = boost::nowide::widen(own_wname.data(), own_wname.size(), b, e);
                assert(str);
                auto wname = std::wstring_view(str, str + own_name.size());
                return wname < name.wstring();
            }
        };
        if (!parent) {
            return nullptr;
        }
        LOG_TRACE(log, "get_presence for {}", path.string());
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &root_path = folder->get_path();
        auto skip = std::distance(root_path.begin(), root_path.end());
        auto it = path.begin();
        std::advance(it, skip);
        auto result = parent;
        while (it != path.end()) {
            // auto child_eq = [&]();
            auto &children = parent->get_children();
            auto it_child = std::lower_bound(children.begin(), children.end(), *it, comparator_t{});
            if (it_child != children.end()) {
                parent = result;
                result = *it_child;
            } else {
                result = {};
                break;
            }
            ++it;
        }
        return result;
    }

    tasks_t pending_io;
    stack_t stack;
    folder_context_ptr_t context;
    local_keeper_ptr_t actor;
    utils::logger_t log;
};

} // namespace

local_keeper_t::local_keeper_t(config_t &config)
    : r::actor_base_t(config), sequencer{std::move(config.sequencer)}, cluster{config.cluster},
      requested_hashes_limit{static_cast<std::int32_t>(config.requested_hashes_limit)} {
    assert(cluster);
    assert(sequencer);
    assert(requested_hashes_limit);
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

auto local_keeper_t::operator()(const model::diff::local::scan_start_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    bool do_scan = true;
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    if (folder->is_suspended() && !folder->get_suspend_reason()) {
        do_scan = false;
    }
    if (do_scan) {
        LOG_DEBUG(log, "initiating scan of {}", diff.folder_id);
        auto local_folder = folder->get_folder_infos().by_device(*cluster->get_device());
        auto ctx = folder_context_ptr_t(new folder_context_t(local_folder));
        auto backend = new folder_slave_t(std::move(ctx), this);
        backend->process_stack();
        auto slave = fs::payload::foreign_executor_prt_t(backend);
        route<fs::payload::foreign_executor_prt_t>(fs_addr, address, std::move(slave));
    } else {
        LOG_DEBUG(log, "skipping scan of {}", diff.folder_id);
    }
    return diff.visit_next(*this, custom);
}

void local_keeper_t::on_post_process(fs::message::foreign_executor_t &msg) noexcept {
    auto &slave = static_cast<folder_slave_t &>(*msg.payload.get());
    auto folder_id = slave.context->local_folder->get_folder()->get_id();
    auto pending_io = !slave.post_process();
    if (pending_io) {
        if (slave.ec) {
            LOG_ERROR(log, "cannot process folder any longer: {}", slave.ec.message());
        } else {
            slave.ec = utils::make_error_code(utils::error_code_t::no_action);
            redirect(&msg, fs_addr, address);
        }
    }
}

void local_keeper_t::on_digest(hasher::message::digest_t &msg) noexcept {
    auto &p = msg.payload;
    LOG_TRACE(log, "on_digest, block size: {}, index: {}", p.data.size(), p.block_index);
    auto &hash_file = *static_cast<hash_base_t *>(p.context.get());
    auto &slave = static_cast<folder_slave_t &>(*hash_file.context.get());
    auto pending_io = !slave.post_process(hash_file, msg);
    if (pending_io) {
        route<fs::payload::foreign_executor_prt_t>(fs_addr, address, &slave);
    }
}

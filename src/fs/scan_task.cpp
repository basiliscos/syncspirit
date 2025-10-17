// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "scan_task.h"
#include "utils.h"
#include "model/messages.h"
#include "fs/messages.h"
#include "utils/platform.h"
#include "proto/proto-helpers-bep.h"
#include <boost/nowide/convert.hpp>
#include <array>
#include <memory_resource>

using namespace syncspirit::fs;

static bool compare_paths(const bfs::path &l, const bfs::path &r) { return l.filename() > r.filename(); }

bool scan_task_t::comparator_t::operator()(const queue_item_t &lhs, const queue_item_t &rhs) const noexcept {
    auto l_index = lhs.index();
    auto r_index = rhs.index();
    if (l_index != r_index) {
        return l_index > r_index;
    }
    return std::visit(
        [&rhs](auto &l) {
            using T = std::decay_t<decltype(l)>;
            auto r = *std::get_if<T>(&rhs);
            return (compare_paths(l.path, r.path));
        },
        lhs);
}

scan_task_t::scan_task_t(model::cluster_ptr_t cluster_, std::string_view folder_id_, file_cache_ptr_t rw_cache_,
                         const config::fs_config_t &config_) noexcept
    : folder_id{folder_id_}, cluster{cluster_}, rw_cache{rw_cache_}, config{config_}, current_diff{nullptr},
      diff_siblings{0} {
    auto &fm = cluster->get_folders();
    folder = fm.by_id(folder_id);
    if (!folder) {
        return;
    }

    folder_info = folder->get_folder_infos().by_device(*cluster->get_device()).get();
    if (!folder_info) {
        return;
    }

    auto &path = folder->get_path();
    assert(path.is_absolute());

    ignore_permissions = folder->are_permissions_ignored() || !utils::platform_t::permissions_supported(path);

    stack.push(unseen_dir_t(path));
    root = path;

    auto &orig_files = folder_info->get_file_infos();
    for (auto &it : orig_files) {
        files.put(it);
    }

    files_limit = files_left = config.files_scan_iteration_limit;
    bytes_left = config.bytes_scan_iteration_limit;
    assert(bytes_left > 0);
    assert(files_left > 0);

    log = utils::get_logger("fs.scan_task");
}

const std::string &scan_task_t::get_folder_id() const noexcept { return folder_id; }

scan_result_t scan_task_t::advance() noexcept {
    if (stack.size()) {
        auto item = std::move(stack.top());
        stack.pop();
        return std::visit([this](auto item) { return do_advance(std::move(item)); }, std::move(item));
    }
    if (files.size() != 0) {
        auto file = *files.begin();
        auto path = file->get_path(*folder_info);
        seen_paths.insert({std::string(file->get_name()->get_full_name()), std::move(path)});
        files.remove(file);

        bool unchanged = file->is_deleted() || (file->is_link() && !utils::platform_t::symlinks_supported());
        if (unchanged) {
            return unchanged_meta_t{std::move(file)};
        } else {
            return removed_t{std::move(file)};
        }
    }
    return false;
}

scan_result_t scan_task_t::do_advance(unseen_dir_t queue_item) noexcept {
    using sub_queue_t = std::pmr::vector<queue_item_t>;
    using allocator_t = std::pmr::polymorphic_allocator<char>;

    auto buffer = std::array<std::byte, 16 * 1024>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = allocator_t(&pool);
    auto sub_queue = sub_queue_t(allocator);

    sys::error_code ec;
    auto &dir = queue_item.path;

    bool exists = bfs::exists(dir, ec);
    if (ec) {
        return scan_errors_t{scan_error_t{dir, ec}};
    }
    if (!exists) {
        return true;
    }

    auto record_path = [&](const bfs::path &file) {
        auto relative = relativize(file, root);
        auto str = boost::nowide::narrow(relative.generic_wstring());
        seen_paths.insert({std::move(str), file});
    };

    scan_errors_t errors;
    auto it = bfs::directory_iterator(dir, ec);
    auto removed = model::file_infos_map_t{};
    if (ec) {
        auto str = std::string{};
        if (dir != root) {
            str = bfs::relative(dir, root).string() + "/";
        }
        for (auto &it : files) {
            auto &file = *it;
            auto name = file.get_name()->get_full_name();
            bool remove = str.empty() || (name.find(str) == 0);
            if (remove) {
                seen_paths.insert({std::string(name), file.get_path(*folder_info)});
                removed.put(it);
            }
        }
        seen_paths.insert({str, dir});
        errors.push_back(scan_error_t{dir, ec});
    } else {
        for (; it != bfs::directory_iterator(); ++it) {
            auto &child = *it;
            record_path(child);
            sys::error_code ec;
            auto status = bfs::symlink_status(child, ec);
            if (ec && (status.type() != bfs::file_type::symlink)) {
                errors.push_back(scan_error_t{child, ec});
                continue;
            }
            if (rw_cache->get(child)) {
                continue;
            }
            auto rp = relativize(child, root).generic_string();
            auto file = files.by_name(rp);
            if (file) {
                removed.put(file);
                if (status.type() == bfs::file_type::directory) {
                    sub_queue.emplace_back(unseen_dir_t(child, file_t{file, child}));
                } else {
                    sub_queue.emplace_back(file_t(file, child));
                }
                continue;
            }

            auto file_type = status.type();
            proto::FileInfo metadata;
            proto::set_name(metadata, rp);
            bool is_dir = false;

            if (file_type == bfs::file_type::regular || file_type == bfs::file_type::directory) {
                auto modification_time = bfs::last_write_time(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                proto::set_modified_s(metadata, to_unix(modification_time));

                if (ignore_permissions == false) {
                    auto permissions = static_cast<uint32_t>(status.permissions());
                    proto::set_permissions(metadata, permissions);
                } else {
                    proto::set_permissions(metadata, 0666);
                    proto::set_no_permissions(metadata, true);
                }
            }
            if (file_type == bfs::file_type::regular) {
                proto::set_type(metadata, proto::FileInfoType::FILE);
                auto sz = bfs::file_size(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                proto::set_size(metadata, sz);
            } else if (file_type == bfs::file_type::directory) {
                proto::set_type(metadata, proto::FileInfoType::DIRECTORY);
                is_dir = true;
                sub_queue.emplace_back(unseen_dir_t(child, unknown_file_t{child, std::move(metadata)}));
            } else if (file_type == bfs::file_type::symlink) {
                auto target = bfs::read_symlink(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                proto::set_symlink_target(metadata, target.string());
                proto::set_type(metadata, proto::FileInfoType::SYMLINK);
                proto::set_no_permissions(metadata, true);
            } else {
                LOG_WARN(log, "unknown/unimplemented file type {} : {}", (int)status.type(), bfs::path(child).string());
                continue;
            }

            if (!is_dir) {
                sub_queue.emplace_back(unknown_file_t{child, std::move(metadata)});
            }
        }
    }
    for (auto &it : removed) {
        files.remove(it);
    }

    std::visit(
        [this](auto it) {
            using T = std::decay_t<decltype(it)>;
            if constexpr (!std::is_same_v<T, std::monostate>) {
                stack.push(std::move(it));
            }
        },
        std::move(queue_item.next));

    std::sort(sub_queue.begin(), sub_queue.end(), comparator_t{});
    for (auto &item : sub_queue) {
        stack.push(std::move(item));
    }

    if (!errors.empty()) {
        return errors;
    }

    return true;
}

scan_result_t scan_task_t::do_advance(file_t queue_item) noexcept {
    auto &file = queue_item.file;
    if (file->is_file()) {
        return advance_regular_file(file);
    } else if (file->is_dir()) {
        return unchanged_meta_t{file};
    } else {
        assert(file->is_link());
        return advance_symlink_file(file);
    }
}

scan_result_t scan_task_t::advance_regular_file(file_info_t &file) noexcept {
    sys::error_code ec;

    auto path = file->get_path(*folder_info);
    auto meta = proto::FileInfo();
    bool changed = false;

    auto sz = bfs::file_size(path, ec);
    if (ec) {
        return file_error_t{path, ec};
    }

    proto::set_size(meta, sz);
    if (sz != (size_t)file->get_size()) {
        changed = true;
    }

    auto modified = bfs::last_write_time(path, ec);
    if (ec) {
        return file_error_t{path, ec};
    }
    auto modified_s = to_unix(modified);
    proto::set_modified_s(meta, modified_s);
    if (modified_s != file->get_modified_s()) {
        changed = true;
    }

    if (!ignore_permissions && !file->has_no_permissions()) {
        auto status = bfs::status(path, ec);
        if (ec) {
            return file_error_t{path, ec};
        }

        auto permissions = static_cast<uint32_t>(status.permissions());
        proto::set_permissions(meta, permissions);
        if (permissions != file->get_permissions()) {
            changed = true;
        }
    }

    if (changed) {
        using FT = proto::FileInfoType;
        proto::set_name(meta, file->get_name()->get_full_name());
        proto::set_type(meta, FT::FILE);
        return changed_meta_t{file, std::move(meta)};
    }

    return unchanged_meta_t{file};
}

scan_result_t scan_task_t::advance_symlink_file(file_info_t &file) noexcept {
    auto path = file->get_path(*folder_info);

    if (!bfs::is_symlink(path)) {
        LOG_CRITICAL(log, "not implemented change tracking: symlink -> non-symblink");
        return unchanged_meta_t{file};
    }

    sys::error_code ec;
    auto target = bfs::read_symlink(path, ec);
    if (ec) {
        return file_error_t{path, ec};
    }

    auto target_str = target.string();
    if (target_str == file->get_link_target()) {
        return unchanged_meta_t{file};
    } else {
        using FT = proto::FileInfoType;
        auto meta = proto::FileInfo();
        proto::set_name(meta, file->get_name()->get_full_name());
        proto::set_type(meta, FT::SYMLINK);
        proto::set_symlink_target(meta, std::move(target_str));
        return changed_meta_t{file, std::move(meta)};
    }
}

scan_result_t scan_task_t::do_advance(unknown_file_t file) noexcept {
    auto &path = file.path;
    if (!is_temporal(file.path.filename())) {
        return unknown_file_t{std::move(path), std::move(file.metadata)};
    }
    auto name = path.filename();
    auto name_str = name.generic_wstring();
    auto new_name = name_str.substr(0, name_str.size() - tmp_suffix.size());
    auto clean_path = path.parent_path() / new_name;
    if (rw_cache->get(clean_path)) {
        return true;
    }
    auto relative_path = [&]() -> std::string {
        auto rp = relativize(path, root);
        auto new_path = rp.parent_path() / new_name;
        return new_path.generic_string();
    }();
    auto local_file = model::file_info_ptr_t{};
    auto peer_file = model::file_info_ptr_t{};
    auto peer_counter = proto::Counter();
    for (auto &it : folder->get_folder_infos()) {
        auto &folder_info = it.item;
        auto &files = folder_info->get_file_infos();
        auto f = files.by_name(relative_path);
        if (f) {
            seen_paths.insert({std::string(f->get_name()->get_full_name()), path});
            if (folder_info->get_device() == cluster->get_device()) {
                local_file = std::move(f);
            } else {
                if (!peer_file) {
                    peer_file = std::move(f);
                    peer_counter = peer_file->get_version().get_best();
                } else {
                    auto c = f->get_version().get_best();
                    if (proto::get_value(peer_counter) < proto::get_value(c)) {
                        peer_counter = c;
                        peer_file = std::move(f);
                        break;
                    }
                }
            }
        }
    }

    if (local_file) {
        files.remove(local_file);
    }

    sys::error_code ec;
    if (!peer_file) {
        LOG_INFO(log, "source cannot be found for temporal file {}, removing orphan", relative_path);
        bfs::remove(path, ec);
        return orphaned_removed_t{path};
    }

    auto modified_time = bfs::last_write_time(path, ec);
    if (ec) {
        LOG_DEBUG(log, "removing outdated temporally {}, cannot get last modification: {}", path.string(),
                  ec.message());
        auto ec_rm = sys::error_code();
        bfs::remove(path, ec_rm);
        return file_error_t{path, ec_rm ? ec_rm : ec};
    }
    auto modified_at = to_unix(modified_time);
    auto now = std::time(nullptr);
    if (modified_at + config.temporally_timeout <= now) {
        LOG_DEBUG(log, "removing outdated temporally {}", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{path, ec};
        }
        return incomplete_removed_t{peer_file};
    }

    auto actual_size = proto::get_size(file.metadata);
    bool size_matches = static_cast<std::int64_t>(peer_file->get_size()) == actual_size;

    if (!size_matches) {
        LOG_DEBUG(log, "removing temporally '{}' because of size-mismatch or outdated source", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{path, ec};
        }
        return incomplete_removed_t{peer_file};
    }

    auto opt = fs::file_t::open_read(path);
    if (!opt) {
        LOG_DEBUG(log, "try to remove temporally {}, which cannot open ", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{path, ec};
        }
        return incomplete_removed_t{peer_file};
    }

    auto &opened_file = opt.assume_value();
    return incomplete_t{peer_file, file_ptr_t(new fs::file_t(std::move(opened_file)))};
}

scan_result_t scan_task_t::do_advance(std::monostate item) noexcept { return true; }

void scan_task_t::push(model::diff::cluster_diff_t *update, std::int64_t bytes_consumed, bool consumes_file) noexcept {
    if (diff_siblings >= files_limit) {
        assert(current_diff);
        diffs.emplace_back(std::move(update_diff));
        current_diff = {};
        diff_siblings = 0;
    }
    ++diff_siblings;
    if (current_diff) {
        current_diff = current_diff->assign_sibling(update);
    } else {
        update_diff.reset(update);
        current_diff = update;
    }
    bytes_left -= bytes_consumed;
    if (consumes_file) {
        --files_left;
    }
}

auto scan_task_t::get_seen_paths() const noexcept -> const seen_paths_t & { return seen_paths; }

auto scan_task_t::guard(r::actor_base_t &actor, r::address_ptr_t coordinator) noexcept -> send_guard_t {
    return send_guard_t(*this, actor, coordinator);
}

scan_task_t::send_guard_t::send_guard_t(scan_task_t &task_, r::actor_base_t &actor_,
                                        r::address_ptr_t coordinator_) noexcept
    : task{task_}, actor{actor_}, coordinator{coordinator_}, force_send{false}, manage_progress{false} {}

void scan_task_t::send_guard_t::send_by_force() noexcept { force_send = true; }

void scan_task_t::send_guard_t::send_progress() noexcept { manage_progress = true; }

scan_task_t::send_guard_t::~send_guard_t() {
    auto consume = force_send || task.bytes_left <= 0 || task.files_left <= 0;
    if (consume) {
        auto diff = std::move(task.update_diff);
        if (diff) {
            for (auto &inner_diff : task.diffs) {
                actor.send<model::payload::model_update_t>(coordinator, std::move(inner_diff), nullptr);
            }
            task.current_diff = nullptr;
            task.bytes_left = task.config.bytes_scan_iteration_limit;
            task.files_left = task.config.files_scan_iteration_limit;
            task.diffs.clear();
            task.diff_siblings = 0;

            actor.send<model::payload::model_update_t>(coordinator, std::move(diff), nullptr);
            task.log->debug("sending model update");
            if (manage_progress) {
                auto &sup = actor.get_supervisor();
                auto address = actor.get_address();
                sup.route<payload::scan_progress_t>(coordinator, address, &task);
                task.log->debug("routing scan progress");
            }
        }
    } else if (manage_progress) {
        task.log->debug("sending scan progress, files left = {}", task.files_left);
        actor.send<payload::scan_progress_t>(actor.get_address(), &task);
    }
}

#include "folder_info.h"
#include "file_info.h"
#include "cluster.h"
#include "misc/error_code.h"
#include "misc/version_utils.h"
#include "../db/prefix.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::file_info);

outcome::result<file_info_ptr_t> file_info_t::create(std::string_view key, const db::FileInfo &data, const folder_info_ptr_t& folder_info_) noexcept {
    if (key.size() != data_length) {
        return make_error_code(error_code_t::invalid_file_info_key_length);
    }
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_file_info_prefix);
    }

    auto ptr = file_info_ptr_t();
    ptr = new file_info_t(key, folder_info_);

    auto r = ptr->fields_update(data);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

outcome::result<file_info_ptr_t> file_info_t::create(const uuid_t& uuid, const proto::FileInfo &info_, const folder_info_ptr_t& folder_info_) noexcept {
    auto ptr = file_info_ptr_t();
    ptr = new file_info_t(uuid, folder_info_);

    auto r = ptr->fields_update(info_, info_.blocks_size());
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

file_info_t::file_info_t(std::string_view key_, const folder_info_ptr_t& folder_info_) noexcept : folder_info{folder_info_.get()} {
    assert(key_.substr(1, uuid_length) == folder_info->get_uuid());
    std::copy(key_.begin(), key_.end(), key);
}

static void fill(char* key, const uuid_t& uuid, const folder_info_ptr_t& folder_info_) noexcept {
    key[0] = prefix;
    auto fi_key = folder_info_->get_uuid();
    std::copy(fi_key.begin(), fi_key.end(), key + 1);
    std::copy(uuid.begin(), uuid.end(), key + 1 + fi_key.size());
}

std::string file_info_t::create_key(const uuid_t& uuid, const folder_info_ptr_t& folder_info_) noexcept {
    std::string key;
    key.resize(data_length);
    fill(key.data(), uuid, folder_info_);
    return key;
}

file_info_t::file_info_t(const uuid_t& uuid, const folder_info_ptr_t& folder_info_) noexcept
    : folder_info{folder_info_.get()} {
    fill(key, uuid, folder_info_);
}

file_info_t::~file_info_t() {
    for (auto &b : blocks) {
        if (!b) {
            continue;
        }
        auto indices = b->unlink(this);
        for (auto i : indices) {
            blocks[i].reset();
        }
    }
}

std::string_view file_info_t::get_name() const noexcept {
    return name;
}

std::uint64_t file_info_t::get_block_offset(size_t block_index) const noexcept {
    assert(!blocks.empty());
    return block_size * block_index;
}

template <typename Source> outcome::result<void> file_info_t::fields_update(const Source &s, size_t block_count) noexcept {
    name = s.name();
    sequence = s.sequence();
    type = s.type();
    set_size(s.size());
    permissions = s.permissions();
    modified_s = s.modified_s();
    modified_ns = s.modified_ns();
    modified_by = s.modified_by();
    if (s.deleted()) {
        flags |= flags_t::f_deleted;
    }
    if (s.invalid()) {
        flags |= flags_t::f_invalid;
    }
    if (s.no_permissions()) {
        flags |= flags_t::f_no_permissions;
    }
    version = s.version();
    block_size = s.block_size();
    symlink_target = s.symlink_target();
    version = s.version();
    full_name = fmt::format("{}/{}", folder_info->get_folder()->get_label(), get_name());
    return reserve_blocks(block_count);
}

auto file_info_t::fields_update(const db::FileInfo& source) noexcept -> outcome::result<void> {
    return fields_update<db::FileInfo>(source, source.blocks_size());
}


std::string_view file_info_t::get_uuid() const noexcept {
     return std::string_view(key + 1 + uuid_length, uuid_length);
}

void file_info_t::set_sequence(std::int64_t value) noexcept {
    sequence = value;
}

template<typename T> T file_info_t::as() const noexcept {
    T r;
    auto name = get_name();
    r.set_name(name.data(), name.size());
    r.set_sequence(sequence);
    r.set_type(type);
    r.set_size(size);
    r.set_permissions(permissions);
    r.set_modified_s(modified_s);
    r.set_modified_ns(modified_ns);
    r.set_modified_by(modified_by);
    r.set_deleted(flags & f_deleted);
    r.set_invalid(flags & f_invalid);
    r.set_no_permissions(flags & f_no_permissions);
    *r.mutable_version() = version;
    r.set_block_size(block_size);
    r.set_symlink_target(symlink_target);
    return r;
}

db::FileInfo file_info_t::as_db(bool include_blocks) const noexcept {
    auto r = as<db::FileInfo>();

    if (include_blocks) {
        for (auto &block : blocks) {
            if (!block) {
                continue;
            }
            auto db_key = block->get_hash();
            r.mutable_blocks()->Add(std::string(db_key));
        }
    }
    return r;
}

proto::FileInfo file_info_t::as_proto(bool include_blocks) const noexcept {
    assert(!include_blocks && "TODO");
    return as<proto::FileInfo>();
}


outcome::result<void> file_info_t::reserve_blocks(size_t block_count) noexcept {
    size_t count = 0;
    if (!block_count && !(flags & f_deleted) && !(flags & f_invalid)) {
        if (size < block_size) {
            return make_error_code(error_code_t::invalid_block_size);
        }
        if (size) {
            if (!block_size) {
                return make_error_code(error_code_t::invalid_block_size);
            }
            count = size / block_size;
            if (block_size * count != size) {
                ++count;
            }
        }
    } else {
        count = block_count;
    }
    remove_blocks();
    blocks.resize(count);
    marks.resize(count);
    missing_blocks = !(flags & f_deleted) && !(flags & f_invalid) ? count : 0;
    return outcome::success();
}

std::string file_info_t::serialize(bool include_blocks) const noexcept {
    return as_db(include_blocks).SerializeAsString();
}

void file_info_t::mark_local_available(size_t block_index) noexcept {
    assert(!marks[block_index]);
    assert(block_index < block_size);
    blocks[block_index]->mark_local_available(this);
    marks[block_index] = true;
    --missing_blocks;
}

bool file_info_t::is_locally_available() noexcept {
    return missing_blocks == 0;
}


#if 0
void file_info_t::update(const proto::FileInfo &remote_info) noexcept {
    if (remote_info.sequence() > sequence) {
        fields_update(remote_info);
        update_blocks(remote_info);
        mark_dirty();
    }
}

bool file_info_t::update(const local_file_t &local_file) noexcept {
    auto local_blocks = local_file.blocks;
    bool matched = local_blocks.size() == blocks.size();
    auto limit = std::min(local_blocks.size(), blocks.size());
    size_t i = 0;
    for (; i < limit; ++i) {
        auto &lb = local_blocks[i];
        auto &sb = blocks[i];
        if (!(*lb == *sb)) {
            matched = false;
            break;
        }
    }

    if (matched) {
        for (auto &b : blocks) {
            b->mark_local_available(this);
            ++i;
        }
    } else {
        size_t j = 0;
        if (!local_file.temp) {
            for (auto &b : blocks) {
                b->unlink(this, true);
            }
            blocks = local_blocks;
            for (auto &b : blocks) {
                b->link(this, j++);
                b->mark_local_available(this);
            }
        } else {
            for (j = 0; j < i; ++j) {
                auto &b = blocks[j];
                b->link(this, j++);
                b->mark_local_available(this);
            }
            for (j = i; j < blocks.size(); ++j) {
                blocks[j]->unlink(this, true);
                blocks[j] = nullptr;
            }
            mark_incomplete();
            matched = true;
        }
        mark_dirty();
    }
    return !matched;
}

void file_info_t::update_blocks(const proto::FileInfo &remote_info) noexcept {
    auto &cluster = *folder_info->get_folder()->get_cluster();
    auto &blocks_map = cluster.get_blocks();

    auto ex_blocks = block_infos_map_t();
    for (auto &block : blocks) {
        ex_blocks.put(block);
    }

    blocks.resize(0);
    for (int i = 0; i < remote_info.blocks_size(); ++i) {
        auto &b = remote_info.blocks(i);
        auto &hash = b.hash();
        auto block = blocks_map.by_id(hash);
        if (!block) {
            block = new block_info_t(b);
            blocks_map.put(block);
            block->link(this, i);
        } else {
            auto ex_block = ex_blocks.by_id(hash);
            if (ex_block) {
                ex_blocks.remove(ex_block);
            } else {
                block->link(this, i);
            }
        }
        blocks.emplace_back(std::move(block));
    }
    auto &deleted_blocks_map = cluster.get_deleted_blocks();
    for (auto &it : ex_blocks) {
        remove_block(it.second, blocks_map, deleted_blocks_map, false);
    }
}

void file_info_t::remove_block(block_info_ptr_t &block, block_infos_map_t &cluster_blocks,
                               block_infos_map_t &deleted_blocks, bool zero_indices) noexcept {
    if (!block) {
        return;
    }
    auto indices = block->unlink(this, true);
    if (block->is_deleted()) {
        deleted_blocks.put(block);
        cluster_blocks.remove(block);
    }
    if (zero_indices) {
        for (auto i : indices) {
            blocks[i] = nullptr;
        }
    }
}

void file_info_t::remove_blocks() noexcept {
    auto &cluster = *folder_info->get_folder()->get_cluster();
    auto &blocks_map = cluster.get_blocks();
    auto &deleted_blocks_map = cluster.get_deleted_blocks();
    for (auto &it : blocks) {
        remove_block(it, blocks_map, deleted_blocks_map);
    }
    if (blocks.size()) {
        mark_dirty();
    }
    blocks.clear();
}

void file_info_t::assign_block(const model::block_info_ptr_t &block, size_t index) noexcept {
    assert(index < blocks.size() && "blocks should be reserve enough space");
    blocks[index] = block;
    block->link(this, index);
    mark_dirty();
}


void file_info_t::record_update(const device_t &source) noexcept {
    uint64_t value = version.counters_size() + 1;
    auto &device_id = source.device_id.get_sha256();
    uint64_t id;
    std::copy(device_id.data(), device_id.data() + sizeof(id), reinterpret_cast<char *>(&id));

    auto counter = version.add_counters();
    counter->set_id(id);
    counter->set_value(value);
}

bool file_info_t::is_older(const file_info_t &other) noexcept { return sequence < other.sequence; }

file_info_ptr_t file_info_t::link(const device_ptr_t &target) noexcept {
    auto fi = folder_info->get_folder()->get_folder_info(target);
    assert(fi);

    auto &full_name = get_full_name();
    auto local_folder_info = fi->get_folder()->get_folder_info(target);
    auto &local_file_infos = local_folder_info->get_file_infos();
    auto local_file = local_file_infos.by_id(full_name);
    if (local_file) {
        assert(local_file->get_sequence() <= sequence);
        /* is being synced */
        bool match =
            (local_file->get_sequence() == sequence) && (local_file->size == size) && local_file->is_incomplete();
        if (match) {
            local_file->locked = true;
            auto &cluster = *folder_info->get_folder()->get_cluster();
            auto &blocks_map = cluster.get_blocks();
            auto &deleted_blocks_map = cluster.get_deleted_blocks();
            auto &local_blocks = local_file->get_blocks();
            size_t j = 0;
            for (; j < blocks.size() && j < local_blocks.size(); ++j) {
                auto &lb = local_blocks[j];
                if (!lb || (*lb != *blocks[j])) {
                    break;
                }
            }
            for (; j < local_blocks.size(); ++j) {
                local_file->remove_block(local_blocks[j], blocks_map, deleted_blocks_map);
            }
            local_blocks.resize(blocks.size());
            return local_file;
        }
        local_file->remove_blocks();
        local_file_infos.remove(local_file);
    }
    auto db = serialize(false);
    local_file = new file_info_t(db, local_folder_info.get());
    local_file_infos.put(local_file);
    local_file->mark_dirty();
    local_file->locked = true;
    local_file->blocks.resize(blocks.size());
    if (blocks.size()) {
        local_file->mark_incomplete();
    }
    return local_file;
}

void file_info_t::after_sync() noexcept {
    assert(locked);
    locked = false;
    if (sequence > folder_info->get_max_sequence()) {
        folder_info->set_max_sequence(sequence);
    }
}

proto::FileInfo file_info_t::get() const noexcept {
    proto::FileInfo r;
    r.set_name(std::string(get_name()));
    r.set_type(type);
    r.set_size(size);
    r.set_permissions(permissions);
    r.set_modified_s(modified_s);
    r.set_modified_ns(modified_ns);
    r.set_modified_by(modified_by);
    r.set_deleted(deleted);
    r.set_invalid(invalid);
    r.set_permissions(permissions);

    auto v = r.mutable_version();
    for (size_t i = 0; i < version.counters_size(); ++i) {
        *v->add_counters() = version.counters(i);
    }
    r.set_sequence(sequence);
    r.set_size(size);

    auto r_blocks = r.mutable_blocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto tb = r_blocks->Add();
        auto &sb = blocks[i];
        tb->set_offset(get_block_offset(i));
        tb->set_size(sb->get_size());
        tb->set_hash(sb->get_hash());
        tb->set_weak_hash(sb->get_weak_hash());
    }

    r.set_symlink_target(symlink_target);

    return r;
}
#endif


const boost::filesystem::path &file_info_t::get_path() const noexcept {
    if (!path) {
        path = folder_info->get_folder()->get_path() / std::string(get_name());
    }
    return path.value();
}

bool file_info_t::is_incomplete() const noexcept {
    if (blocks.empty()) {
        return false;;
    }
    for(auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
        if (!*it) {
            return true;
        }
    }
    return false;
}

auto file_info_t::local_file() noexcept -> file_info_ptr_t {
    auto device = folder_info->get_device();
    auto cluster = folder_info->get_folder()->get_cluster();
    auto& my_device = cluster->get_device();
    assert(*device != *my_device);
    auto my_folder_info = folder_info->get_folder()->get_folder_infos().by_device(my_device);
    if (!my_folder_info) {
        return {};
    }

    auto local_file = my_folder_info->get_file_infos().by_name(get_name());
    if (!local_file) {
        return {};
    }

    auto r = compare(local_file->version, version);
    if (r == version_relation_t::identity) {
        return local_file;
    } else if (r == version_relation_t::conflict) {
        auto log = utils::get_logger("model");
        LOG_CRITICAL(log, "conflict handling is not available");
    }

    return {};
}

void file_info_t::unlock() noexcept { flags = flags & ~flags_t::f_locked; }

void file_info_t::lock() noexcept { flags |= flags_t::f_locked; }

bool file_info_t::is_locked() const noexcept { return flags & flags_t::f_locked; }

void file_info_t::locally_unlock() noexcept { flags = flags & ~flags_t::f_local_locked; }

void file_info_t::locally_lock() noexcept { flags |= flags_t::f_local_locked; }

bool file_info_t::is_locally_locked() const noexcept { return flags & flags_t::f_local_locked; }


void file_info_t::assign_block(const model::block_info_ptr_t &block, size_t index) noexcept {
    assert(index < blocks.size() && "blocks should be reserve enough space");
    assert(!blocks[index]);
    blocks[index] = block;
    block->link(this, index);
}

void file_info_t::remove_blocks() noexcept {
    for (auto &it : blocks) {
        remove_block(it);
    }
    std::fill_n(marks.begin(), blocks.size(), false);
    missing_blocks = blocks.size();
}

void file_info_t::remove_block(block_info_ptr_t &block) noexcept {
    if (!block) {
        return;
    }
    auto indices = block->unlink(this, true);
    for (auto i : indices) {
        blocks[i] = nullptr;
    }
}

bool file_info_t::need_download(const file_info_t &other) noexcept {
    assert(folder_info->get_device() == folder_info->get_folder()->get_cluster()->get_device().get());
    assert(other.folder_info->get_device() != folder_info->get_folder()->get_cluster()->get_device().get());
    assert(name == other.name);
    if (is_locked()) {
        return false;
    }
    auto r = compare(version, other.version);
    if (r == version_relation_t::identity) {
        return !is_locally_available();
    } else if (r == version_relation_t::older) {
        return true;
    } else if (r == version_relation_t::newer) {
        return false;
    } else {
        assert(r == version_relation_t::conflict);
        auto log = utils::get_logger("model");
        LOG_CRITICAL(log, "conflict handling is not available");
        return false;
    }
}

template<> std::string_view get_index<0>(const file_info_ptr_t& item) noexcept { return item->get_uuid(); }
template<> std::string_view get_index<1>(const file_info_ptr_t& item) noexcept { return item->get_name(); }

file_info_ptr_t file_infos_map_t:: by_name(std::string_view name) noexcept {
    return get<1>(name);
}

} // namespace syncspirit::model

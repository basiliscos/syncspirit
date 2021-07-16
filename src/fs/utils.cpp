#include "utils.h"
#include <openssl/sha.h>
#include <zlib.h>

namespace syncspirit::fs {

static const std::string_view tmp_suffix = ".syncspirit-tmp";

static std::size_t block_sizes[] = {
    (1 << 7) * 1024,  (1 << 8) * 1024,  (1 << 9) * 1024,  (1 << 10) * 1024,
    (1 << 11) * 1024, (1 << 12) * 1024, (1 << 13) * 1024, (1 << 14) * 1024,
};
static const constexpr size_t block_sizes_count = sizeof(block_sizes) / sizeof(block_sizes[0]);
static const constexpr size_t max_blocks_count = 2000;

model::block_info_ptr_t compute(payload::scan_t::next_block_t &block) noexcept {
    // sha256
    static const constexpr size_t SZ = SHA256_DIGEST_LENGTH;
    unsigned char digest[SZ];
    auto ptr = (const unsigned char *)block.file->data();
    auto end = ptr + block.file_size;
    ptr += block.block_index * block.block_size;
    auto length = block.block_size;
    if (ptr + length > end) {
        length = end - ptr;
    }
    SHA256(ptr, length, digest);

    // alder32
    auto weak_hash = adler32(0L, Z_NULL, 0);
    weak_hash = adler32(weak_hash, ptr, length);

    db::BlockInfo info;
    info.set_hash((const char *)digest, SZ);
    info.set_weak_hash(weak_hash);
    info.set_size(length);

    return new model::block_info_t(info);
}

outcome::result<payload::scan_t::next_block_option_t> prepare(const bfs::path &file_path) noexcept {
    using result_t = payload::scan_t::next_block_option_t;
    using file_t = payload::scan_t::file_t;
    sys::error_code ec;
    auto sz = bfs::file_size(file_path, ec);
    if (ec) {
        return ec;
    }
    if (sz == 0) {
        return outcome::success(result_t{});
    }

    size_t bs = 0;
    for (size_t i = 0; i < block_sizes_count; ++i) {
        if (block_sizes[i] * max_blocks_count >= sz) {
            bs = block_sizes[i];
            if (bs > sz) {
                bs = sz;
            }
            break;
        }
    }
    if (bs == 0) {
        bs = block_sizes[block_sizes_count - 1];
    }

    bio::mapped_file_params params;
    params.path = file_path.string();
    params.flags = bio::mapped_file::mapmode::readonly;

    auto file = std::make_unique<file_t>(params);
    return result_t({file_path, bs, sz, 0, std::move(file)});
}

bfs::path make_temporal(const bfs::path &path) noexcept {
    auto copy = path;
    copy += tmp_suffix.data();
    return copy;
}

bool is_temporal(const bfs::path &path) noexcept {
    auto &name = path.string();
    if (name.length() < tmp_suffix.length()) {
        return false;
    }
    auto pos = name.find(tmp_suffix, name.length() - tmp_suffix.length());
    return (pos != name.npos);
}

relative_result_t relative(const bfs::path &path, const boost::filesystem::path &root) noexcept {
    auto sub = bfs::relative(path, root);
    if (!is_temporal(path)) {
        return {sub, false};
    }
    auto& str = sub.string();
    auto new_path = str.substr(0, str.size() - tmp_suffix.size());
    return {bfs::path(new_path), true};
}

} // namespace syncspirit::fs

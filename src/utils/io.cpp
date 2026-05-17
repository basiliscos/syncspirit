#include "io.h"

#include <cassert>
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sys = boost::system;
using namespace syncspirit::utils;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define SS_STAT_BUFF struct __stat64
#define SS_STAT_FN(PATH, BUFF) _wstat64((PATH).native().data(), (BUFF))
#define SS_FSTAT_FN(FD, BUFF) _fstat64(FD, (BUFF))
#define SS_FILE_NO(FILE) _fileno(FILE)
#define SS_RESIZE(FILE, SIZE) _chsize_s(FILE, SIZE)
#else
#define SS_STAT_FN(PATH, BUFF) stat((PATH).native().data(), (BUFF))
#define SS_FSTAT_FN(FD, BUFF) fstat(FD, (BUFF))
#define SS_STAT_BUFF struct stat
#define SS_RESIZE(FILE, SIZE) ftruncate(FILE, SIZE)
#endif

io_stream_t::io_stream_t(int fd_) noexcept : fd{fd_} {}

io_stream_t::io_stream_t(io_stream_t &&other) noexcept { std::swap(fd, other.fd); }

io_stream_t::~io_stream_t() {
    if (fd >= 0) {
        (void)close();
    }
}

auto io_stream_t::open_truncate(const bfs::path &path) noexcept -> outcome::result<io_stream_t> {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    int f;
    auto open_mode = _O_RDWR | _O_CREAT | _O_TRUNC | _O_BINARY;
    auto e = _wsopen_s(&f, path.native().data(), open_mode, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (e == 0) {
        return io_stream_t(f);
    }
    return sys::error_code{e, sys::system_category()};
#else
    auto f = open(path.native().data(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (f >= 0) {
        return io_stream_t(f);
    }
    return sys::error_code{errno, sys::system_category()};
#endif
}

auto io_stream_t::open_read(const bfs::path &path) noexcept -> outcome::result<io_stream_t> {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    int f;
    auto open_mode = _O_RDONLY | _O_BINARY;
    auto e = _wsopen_s(&f, path.native().data(), open_mode, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (e == 0) {
        return io_stream_t(f);
    }
    return sys::error_code{e, sys::system_category()};
#else
    auto f = open(path.native().data(), O_RDONLY, 0666);
    if (f >= 0) {
        return io_stream_t(f);
    }
    return sys::error_code{errno, sys::system_category()};
#endif
}

auto io_stream_t::open_write(const bfs::path &path, std::size_t file_size) noexcept -> opne_write_t {
    bool need_resize = true;
    SS_STAT_BUFF stat_info;
    auto r = SS_STAT_FN(path, &stat_info);
    if (r == 0) {
        need_resize = static_cast<uint64_t>(stat_info.st_size) != file_size;
    }
    if (need_resize) {
        auto opt = open_truncate(path);
        if (!opt) {
            return opt.assume_error();
        }
        auto &f = opt.assume_value();
        if (file_size) {
            if (SS_RESIZE(f.fd, file_size) != 0) {
                return sys::error_code{errno, sys::system_category()};
            }
            if (lseek(f.fd, 0, SEEK_SET) != 0) {
                return sys::error_code{errno, sys::system_category()};
            }
            return {std::move(f), true, true};
        }
        return {std::move(f), false, true};
    } else {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
        int f;
        auto open_mode = _O_RDWR | _O_BINARY;
        auto e = _wsopen_s(&f, path.native().data(), open_mode, _SH_DENYNO, _S_IREAD | _S_IWRITE);
        if (e == 0) {
            return {io_stream_t(f), false, false};
        }
        return sys::error_code{e, sys::system_category()};
#else
        auto fd = open(path.native().data(), O_RDWR, 0666);
        if (fd >= 0) {
            return {io_stream_t(fd), false, false};
        }
        return sys::error_code{errno, sys::system_category()};
#endif
    }
}

auto io_stream_t::close() noexcept -> outcome::result<void> {
    if (fd) {
        auto ok = ::close(fd) == 0;
        fd = -1;
        if (!ok) {
            return sys::error_code{errno, sys::system_category()};
        }
    }
    return outcome::success();
}

auto io_stream_t::get_position() const noexcept -> outcome::result<offset_t> {
    assert(fd);
    auto pos = lseek(fd, 0, SEEK_CUR);
    if (pos == -1) {
        auto ec = sys::error_code{errno, sys::system_category()};
        (void)const_cast<io_stream_t *>(this)->close();
        return ec;
    }
    return static_cast<offset_t>(pos);
}

auto io_stream_t::set_position(offset_t value) noexcept -> outcome::result<void> {
    if (lseek(fd, value, SEEK_SET) != value) {
        auto ec = sys::error_code{errno, sys::system_category()};
        (void)const_cast<io_stream_t *>(this)->close();
        return ec;
    }
    return outcome::success();
}

auto io_stream_t::read_whole() noexcept -> outcome::result<bytes_t> {
    SS_STAT_BUFF stat_info;
    auto r = SS_FSTAT_FN(fd, &stat_info);
    if (r != 0) {
        return sys::error_code{errno, sys::system_category()};
    }
    auto &sz = stat_info.st_size;
    auto str = utils::bytes_t();
    str.resize(sz);
    if (::read(fd, str.data(), sz) != sz) {
        return sys::error_code{errno, sys::system_category()};
    }
    return std::move(str);
}

auto io_stream_t::read(unsigned char *ptr, std::size_t sz) noexcept -> outcome::result<void> {
    if (::read(fd, ptr, sz) != sz) {
        return sys::error_code{errno, sys::system_category()};
    }
    return outcome::success();
}

auto io_stream_t::read_bytes(offset_t sz) noexcept -> outcome::result<bytes_t> {
    auto str = utils::bytes_t();
    str.resize(sz);
    if (::read(fd, str.data(), sz) != sz) {
        return sys::error_code{errno, sys::system_category()};
    }
    return std::move(str);
}

auto io_stream_t::write(unsigned const char *ptr, std::size_t sz) noexcept -> outcome::result<void> {
    if (::write(fd, ptr, sz) != sz) {
        return sys::error_code{errno, sys::system_category()};
    }
    return outcome::success();
}

auto io_stream_t::write(std::string_view str) noexcept -> outcome::result<void> {
    auto ptr = reinterpret_cast<const unsigned char *>(str.data());
    return write(ptr, str.size());
}

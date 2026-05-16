#include "io.h"

#include <cassert>
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace syncspirit::utils;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define SS_OPEN_FILE(PATH, MODE) _wfopen(PATH.native().data(), MODE)
#define SS_STAT_BUFF struct __stat64
#define SS_STAT_FN(PATH, BUFF) _wstat64((PATH).native().data(), (BUFF))
#define SS_FILE_NO(FILE) _fileno(FILE)
#define SS_RESIZE(FILE, SIZE) _chsize_s(FILE, SIZE)
#else
#define SS_OPEN_FILE(PATH, MODE) open(PATH.native().data(), MODE, 0666)
#define SS_STAT_FN(PATH, BUFF) stat((PATH).native().data(), (BUFF))
#define SS_FSTAT_FN(FD, BUFF) fstat(FD, (BUFF))
#define SS_STAT_BUFF struct stat
#define SS_FILE_NO(FILE) fileno(FILE)
#define SS_RESIZE(FILE, SIZE) ftruncate(FILE, SIZE)
#endif

io_stream_t::io_stream_t(int fd_) noexcept : fd{fd_} {}

io_stream_t::io_stream_t(io_stream_t &&other) noexcept { std::swap(fd, other.fd); }

io_stream_t::~io_stream_t() {
    if (fd >= 0) {
        close();
    }
}

io_stream_t io_stream_t::open_truncate(const bfs::path &path) noexcept {
    return SS_OPEN_FILE(path, O_RDWR | O_CREAT | O_TRUNC);
}

io_stream_t io_stream_t::open_read(const bfs::path &path) noexcept { return SS_OPEN_FILE(path, O_RDONLY); }

auto io_stream_t::open_write(const bfs::path &path, std::size_t file_size) noexcept -> details::open_write_result_t {
    bool need_resize = true;
    SS_STAT_BUFF stat_info;
    auto r = SS_STAT_FN(path, &stat_info);
    if (r == 0) {
        need_resize = static_cast<uint64_t>(stat_info.st_size) != file_size;
    }
    if (need_resize) {
        auto f = open_truncate(path);
        if (!f) {
            return {{}, false, false};
        }
        if (file_size) {
            if (SS_RESIZE(f.fd, file_size) != 0) {
                return {{}, false, true};
            }
            if (lseek(f.fd, 0, SEEK_SET) != 0) {
                return {{}, false, true};
            }
            return {std::move(f), true, true};
        }
        return {std::move(f), false, true};
    } else {
        auto f = SS_OPEN_FILE(path, O_RDWR);
        return {std::move(f), false, false};
    }
}

bool io_stream_t::close() noexcept {
    if (fd) {
        auto ok = ::close(fd) == 0;
        fd = -1;
        return ok;
    }
    return true;
}

io_stream_t::operator bool() const noexcept { return fd >= 0; }

auto io_stream_t::get_position() const noexcept -> offset_opt_t {
    assert(fd);
    auto pos = lseek(fd, 0, SEEK_CUR);
    if (pos == -1) {
        const_cast<io_stream_t *>(this)->close();
        return {};
    }
    return static_cast<offset_t>(pos);
}

bool io_stream_t::set_position(offset_t value) noexcept {
    if (lseek(fd, value, SEEK_SET) != value) {
        close();
        return false;
    }
    return true;
}

auto io_stream_t::read_whole() noexcept -> content_opt_t {
    SS_STAT_BUFF stat_info;
    auto r = SS_FSTAT_FN(fd, &stat_info);
    if (r != 0) {
        return {};
    }
    auto &sz = stat_info.st_size;
    auto str = std::string();
    str.resize(sz);
    if (::read(fd, str.data(), sz) != sz) {
        return {};
    }
    return std::move(str);
}

bool io_stream_t::read(unsigned char *ptr, std::size_t sz) noexcept { return ::read(fd, ptr, sz) == sz; }

bool io_stream_t::write(unsigned const char *ptr, std::size_t sz) noexcept { return ::write(fd, ptr, sz) == sz; }

bool io_stream_t::write(std::string_view str) noexcept {
    auto ptr = reinterpret_cast<const unsigned char *>(str.data());
    return write(ptr, str.size());
}

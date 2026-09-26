// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "platform.h"
#include "path_view.hpp"
#include "path_utils.h"
#include "syncspirit-config.h"
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <mutex>
#include <dbghelp.h>
#include <array>
#include <cstring>
#include <zlib.h>
#include <spdlog/spdlog.h>
#include <cxxabi.h>
#include "utils/path_view.hpp"
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <spdlog/spdlog.h>
#include "utils/format.hpp"

using namespace syncspirit::utils;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)

static DWORD thread_name_tls_index = TLS_OUT_OF_INDEXES;

struct handle_guard_t {
    handle_guard_t() = default;
    handle_guard_t(HANDLE handle_) noexcept : handle{handle_} {}
    ~handle_guard_t() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }

    handle_guard_t &operator=(handle_guard_t &&other) noexcept {
        std::swap(handle, other.handle);
        return *this;
    }

    operator HANDLE() const noexcept { return handle; }
    operator bool() const noexcept { return handle != INVALID_HANDLE_VALUE; }

    HANDLE handle{INVALID_HANDLE_VALUE};
};

static handle_guard_t open_dump_file() {
    char name[64];
    auto thread_id = GetCurrentThreadId();
    sprintf(name, "crash_dump-0x%x.txt", thread_id);
    return CreateFileA(name, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

static void dump_traces(EXCEPTION_POINTERS *ep) {
    auto ctx = *ep->ContextRecord;
    auto process = GetCurrentProcess();
    auto thread = GetCurrentThread();
    auto file = open_dump_file();
    if (!file) {
        return;
    }

    STACKFRAME64 frame;
    memset(&frame, 0, sizeof(frame));
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

#if defined(_M_X64) || defined(__x86_64__)
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
#else
    DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
#endif

    int frame_idx = 0;
    DWORD written = 0;
    char buff[512];
    auto out_bytes = snprintf(buff, sizeof(buff), "dump begin\n");
    WriteFile(file, buff, out_bytes, &written, {});

    while (true) {
        auto ok = StackWalk64(machine, process, thread, &frame, &ctx, NULL, SymFunctionTableAccess64,
                              SymGetModuleBase64, NULL);
        if (!ok) {
            DWORD e = GetLastError();
            out_bytes = snprintf(buff, sizeof(buff), "not ok, code: 0x%08x, stopping\n", e);
            WriteFile(file, buff, out_bytes, &written, {});
            break;
        }
        if (frame.AddrPC.Offset == 0) {
            out_bytes = snprintf(buff, sizeof(buff), "frame.AddrPC.Offset == 0, stopping\n");
            WriteFile(file, buff, out_bytes, &written, {});
            break;
        }
        fprintf(stderr, "ip = %p\n", frame.AddrPC.Offset);
        char sym_buff[sizeof(SYMBOL_INFO) + 512] = {0};
        auto sym_info = (PSYMBOL_INFO)sym_buff;
        sym_info->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym_info->MaxNameLen = 512;

        auto module_base = SymGetModuleBase64(process, frame.AddrPC.Offset);
        auto module_name = std::string_view("unknown module");
        auto symbol_name = std::string_view("unknown symbol");
        auto symbol_offset = DWORD64{0};

        auto module_info = IMAGEHLP_MODULE64{};
        std::memset(&module_info, 0, sizeof(module_info));
        module_info.SizeOfStruct = sizeof(module_info);

        if (module_base && SymGetModuleInfo64(process, module_base, &module_info)) {
            module_name = module_info.ModuleName;
        }

        if (SymFromAddr(process, frame.AddrPC.Offset, &symbol_offset, sym_info)) {
            char buff[512];
            auto ptr = sym_info->Name;
            if (*ptr != '_') {
                --ptr;
                *ptr = '_';
            }

            int status;
            auto sz = size_t{0};
            auto result = abi::__cxa_demangle(ptr, buff, &sz, &status);
            if (result) {
                std::memcpy(sym_buff, result, sz);
                symbol_name = std::string_view(sym_buff, sz);
            } else {
                ptr++;
                result = abi::__cxa_demangle(ptr, buff, &sz, &status);
            }
            if (result) {
                std::memcpy(sym_buff, result, sz);
                symbol_name = std::string_view(sym_buff, sz);
            } else {
                symbol_name = sym_info->Name;
            }
        } else if (module_base) {
            symbol_offset = (DWORD64)(frame.AddrPC.Offset - module_base);
        } else {
            symbol_offset = 0;
        }

        out_bytes =
            snprintf(buff, sizeof(buff), "(#%02d) %p %s(0x%p) %s + 0x%llx\n", frame_idx++, (void *)frame.AddrPC.Offset,
                     module_name.data(), (void *)module_base, symbol_name.data(), symbol_offset);
        WriteFile(file, buff, out_bytes, &written, {});
        (void)written;
    }

    const char *thread_name = "unknown";
    if (thread_name_tls_index != TLS_OUT_OF_INDEXES) {
        thread_name = (const char *)TlsGetValue(thread_name_tls_index);
    }

    out_bytes = snprintf(buff, sizeof(buff), "dump end, thread: %s (%d)\n", thread_name, GetCurrentThreadId());
    WriteFile(file, buff, out_bytes, &written, {});
}

std::mutex crash_mutex;

static LONG WINAPI seh_handler(EXCEPTION_POINTERS *ep) {
    auto guard = std::lock_guard<std::mutex>(crash_mutex);
    dump_traces(ep);

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

bool platform_t::startup() {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    // make explicit dependency on zlib, as openssl loads it via LoadLibrary,
    // and it cannot be found by scanning dlls
    (void)zlibVersion(); // make explict

    auto wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;

    auto err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        return false;
    }

    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);

    if (!SymInitialize(GetCurrentProcess(), NULL, TRUE)) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    SetUnhandledExceptionFilter(seh_handler);

    thread_name_tls_index = TlsAlloc();
#endif
    return true;
}

void platform_t::shutdown() noexcept {}

bool platform_t::symlinks_supported() noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    return false;
#else
    return true;
#endif
}

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
namespace {

using names_t = std::array<std::wstring_view, 1 + 13 + 1 + 1 + 13 + 1>;

// clang-format off
names_t reserved_names = {
    L"aux",
    L"com0", L"com1", L"com2", L"com3", L"com4", L"com5", L"com6", L"com7", L"com8", L"com9", L"com²", L"com³", L"com¹",
    L"con",
    L"lpt0", L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9",L"lpt²",  L"lpt³",  L"lpt¹",
    L"nul",
    L"prn",
};
// clang-format on

struct range_t {
    int a;
    int b;
    bool is_valid() const { return a <= b; };
};

range_t bisect(wchar_t needle, int offset, range_t r) {
    int a = r.a;
    bool found_a = false;

    for (; a <= r.b; ++a) {
        auto &source = reserved_names[a];
        if (offset >= static_cast<int>(source.size())) {
            continue;
        }
        auto symbol = source[offset];
        if (symbol == needle) {
            found_a = true;
            break;
        }
        if (symbol > needle) {
            break;
        }
    }

    if (!found_a) {
        return {0, -1};
    }

    int b = a;
    bool overmatch_b = false;
    for (; b <= r.b; ++b) {
        auto &source = reserved_names[b];
        if (offset >= static_cast<int>(source.size())) {
            overmatch_b = true;
            break;
        }
        auto symbol = source[offset];
        if (symbol > needle) {
            overmatch_b = true;
            break;
        }
    }
    if (b > r.b)
        overmatch_b = true;
    return {a, b - (overmatch_b ? 1 : 0)};
}

} // namespace
#endif

bool platform_t::path_supported(const poly_path_view_t &str_path) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto wname = str_path.get_full_wname(false);
    for (size_t i = 0; i < wname.size(); ++i) {
        auto symbol = wname[i];
        if (symbol < 31) {
            return false;
        }
        switch (symbol) {
            // clang-format off
            case L'<':
            case L'>':
            case L':':
            case L'"':
            case L'\\':
            case L'|':
            case L'?':
            case L'*':
                return false;
            // clang-format on
        default: /* noop */;
        }
    }

    auto tail = wname;
    while (tail.size()) {
        auto pos = tail.find(L'/');
        if (pos == 0) {
            tail = tail.substr(1);
            continue;
        }
        auto sz = pos == std::wstring::npos ? tail.size() : pos;
        auto name = tail.substr(0, sz);
        if (name.size() == tail.size()) {
            tail = {};
        } else {
            tail = tail.substr(name.size());
        }
        if (auto dot = name.rfind(L'.'); dot != std::wstring::npos) { // stem
            name = name.substr(0, dot);
        }
        auto range = range_t{0, static_cast<int>(reserved_names.size()) - 1};
        for (size_t i = 0; i < name.size(); ++i) {
            auto symbol = name[i];
            switch (symbol) {
                // clang-format off
                // case L'/':
                    // return false;
                case L'A': symbol = 'a'; break;
                case L'C': symbol = 'c'; break;
                case L'L': symbol = 'l'; break;
                case L'M': symbol = 'M'; break;
                case L'N': symbol = 'n'; break;
                case L'O': symbol = 'o'; break;
                case L'P': symbol = 'p'; break;
                case L'R': symbol = 'r'; break;
                case L'T': symbol = 't'; break;
                case L'X': symbol = 'x'; break;
                default: /* noop */ ;
                // clang-format on
            }

            range = bisect(symbol, static_cast<int>(i), range);
            if (!range.is_valid()) {
                break;
            }
            if (range.a == range.b && ((i + 1) == name.size())) {
                auto &reserved = reserved_names[range.a];
                if (reserved.size() == name.size()) {
                    return false;
                }
                break;
            }
        }
    }
#endif

    (void)str_path;
    return true;
}

bool platform_t::permissions_supported(const path_base_t &) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    return false;
#endif
    return true;
}

void platform_t::set_thread_name(std::string_view name) noexcept {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name.data());
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    if (thread_name_tls_index != TLS_OUT_OF_INDEXES) {
        TlsSetValue(thread_name_tls_index, (void *)name.data());
    }
#endif
}

static poly_path_view_t app_path(const allocator_t &allocator, std::error_code &ec, const char *argv0) noexcept {
    auto path = make_empty_view(allocator);
#if defined(__linux__)
    char buff[SYNCSPIRIT_PATH_MAX];
    if (::readlink("/proc/self/exe", buff, sizeof(buff)) == -1) {
        ec = std::error_code(errno, std::system_category());
    } else {
        path = make_native_view(buff, allocator).get_parent();
    }
#elif defined(_WIN32)
    wchar_t buff[SYNCSPIRIT_PATH_MAX] = {0};
    auto sz = ::GetModuleFileNameW(nullptr, buff, SYNCSPIRIT_PATH_MAX);
    if (sz > 0 && sz < SYNCSPIRIT_PATH_MAX) {
        path = make_native_view(buff, allocator);
    } else {
        ec = std::error_code(::GetLastError(), std::system_category());
    }
#elif defined(__APPLE__)
    char buff[SYNCSPIRIT_PATH_MAX] = {0};
    auto sz = std::uint32_t{0};
    _NSGetExecutablePath(nullptr, &sz);
    if (sz == 0) {
        ec = std::make_error_code(std::errc::io_error);
    } else {
        if (_NSGetExecutablePath(buff, &sz) != 0) {
            ec = std::make_error_code(std::errc::io_error);
        } else {
            path = make_native_view(buff, allocator);
        }
    }
#endif

    // fallback
#if defined(__unix__)
    if (path.empty() && argv0) {
        path = cwd(allocator, ec) / make_native_view(argv0, allocator);
    }
#endif
    return path;
}

auto platform_t::resources_dir(const allocator_t &allocator, std::error_code &ec, const char *argv0) noexcept
    -> poly_path_view_t {
    auto path = make_empty_view(allocator);
    auto exe_path = make_empty_view(allocator);
    bool need_res_dir{true};
    if (auto res_dir = std::getenv("SYNCSPIRIT_RES_DIR"); res_dir) {
        path = make_native_view(res_dir, allocator);
        need_res_dir = false;
    }
#if defined(__linux__)
    if (auto app_dir = std::getenv("APPDIR"); app_dir) {
        auto tail = make_native_view("usr/share/syncspirit/resources", allocator);
        path = make_native_view(app_dir, allocator) / tail;
    }
#endif

    if (path.empty()) {
        exe_path = app_path(allocator, ec, argv0);
    }

    if (need_res_dir && path.empty() && !exe_path.empty()) {
        auto exe_dir = exe_path.get_parent();
#if defined(__linux__)
        path = exe_dir / make_native_view("share/syncspirit/resources", allocator);
#elif defined(_WIN32)
        path = exe_dir / make_native_view("resources", allocator);
#elif defined(__APPLE__)
        auto parent = exe_dir.get_parent();
        path = parent / make_native_view("Resources", allocator);
#endif
    }

    if (!path.empty()) {
        auto ec = std::error_code{};
        if (!exists(path, ec)) {
            path = make_empty_view(allocator);
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
        }
    }

    return path;
}

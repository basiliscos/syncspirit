// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "platform.h"
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <dbghelp.h>
#include <array>
#include <cstring>
#include <zlib.h>
#include <spdlog/spdlog.h>
#endif

using namespace syncspirit::utils;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)

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
    return CreateFileA("crash_dump.txt", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

static void safe_write(const char *s) {
    HANDLE h = CreateFileA("crash_dump.txt", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(h, s, (DWORD)strlen(s), &written, NULL);
    CloseHandle(h);
}

static void dump_traces(EXCEPTION_POINTERS *ep) {
    auto ctx = ep->ContextRecord;
    auto process = GetCurrentProcess();
    auto thread = GetCurrentThread();
    auto file = open_dump_file();
    if (!file) {
        return;
    }

    STACKFRAME64 frame;
    memset(&frame, 0, sizeof(frame));
#if defined(_M_X64) || defined(__x86_64__)
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx->Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#endif

    int frame_idx = 0;
    while (true) {
        auto ok = StackWalk64(machine, process, thread, &frame, ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64,
                              NULL);
        if (!ok || frame.AddrPC.Offset == 0) {
            return;
        }

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
            symbol_name = sym_info->Name;
        } else if (module_base) {
            symbol_offset = (DWORD64)(frame.AddrPC.Offset - module_base);
        } else {
            symbol_offset = 0;
        }

        char buff[512];
        auto out_bytes =
            snprintf(buff, sizeof(buff), "(#%d) %p %s(0x%p) %s + 0x%llx\n", frame_idx++, (void *)frame.AddrPC.Offset,
                     module_name.data(), (void *)module_base, symbol_name.data(), symbol_offset);

        DWORD written = 0;
        WriteFile(file, buff, out_bytes, &written, {});
        (void)written;
    }
}

static LONG WINAPI seh_handler(EXCEPTION_POINTERS *ep) {
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

bool platform_t::path_supported(const bfs::path &path) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    for (auto it = path.begin(); it != path.end(); ++it) {
        auto name = it->stem().wstring();
        auto range = range_t{0, static_cast<int>(reserved_names.size()) - 1};
        for (size_t i = 0; i < name.size(); ++i) {
            auto symbol = name[i];
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
                case L'/':
                case L'|':
                case L'?':
                case L'*':
                    return false;
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

    (void)path;
    return true;
}

bool platform_t::permissions_supported(const bfs::path &) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    return false;
#endif
    return true;
}

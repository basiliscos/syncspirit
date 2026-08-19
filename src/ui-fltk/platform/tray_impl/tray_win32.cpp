// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_WIN32)

#include "tray_win32.h"
#include "main_window.h"
#include "utils/format.hpp"

#include <FL/platform.H>
#include <cstring>
#include <type_traits>

using namespace syncspirit::fltk;

static constexpr wchar_t tray_property_str[] = L"syncspirit.tray.prop";
static constexpr wchar_t tray_message_str[] = L"syncspirit.tray.message";
static constexpr wchar_t tray_window_class_str[] = L"syncspirit.tray.window";

struct popup_menu_deleter_t {
    void operator()(HMENU handle) const noexcept {
        if (handle) {
            DestroyMenu(handle);
        }
    }
};

using popup_menu_t = std::unique_ptr<std::remove_pointer_t<HMENU>, popup_menu_deleter_t>;

static popup_menu_t make_menu(tray_win32_t &tray) {
    auto popup_menu = CreatePopupMenu();
    if (popup_menu) {
        auto buffer = std::array<std::byte, 1024 * 32>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

        auto &menus = tray.menu_items;
        auto menu_id = UINT(0);
        for (auto &menu_source : menus) {
            if (menu_source.text) {
                auto label = menu_source.text;
                int sz = ::MultiByteToWideChar(CP_UTF8, 0, label, -1, nullptr, 0);
                if (sz) {
                    auto str = std::pmr::wstring(allocator);
                    str.resize(static_cast<std::size_t>(sz + 1));
                    auto out_sz = ::MultiByteToWideChar(CP_UTF8, 0, label, -1, str.data(), static_cast<int>(sz + 1));
                    if (out_sz >= 0) {
                        auto flags = MF_STRING;
                        if (menu_source.flags & FL_MENU_INACTIVE) {
                            flags |= MF_GRAYED;
                        }
                        AppendMenuW(popup_menu, flags, menu_id++, str.data());
                    }
                }
            }
        }
    }
    return popup_menu_t(popup_menu);
}

static LRESULT CALLBACK tray_proc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
    auto tray = reinterpret_cast<tray_win32_t *>(GetPropW(handle, tray_property_str));
    if (tray) {
        if (message == tray->tray_message) {
            auto main_window = tray->sup.get_main_window();
            switch (LOWORD(lParam)) {
            case WM_COMMAND:
                return 0;
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK: {
                Fl::awake(
                    [](void *p) {
                        auto main_window = reinterpret_cast<main_window_t *>(p);
                        HWND hwnd = (HWND)fl_xid(main_window);
                        auto is_visible = IsWindowVisible(hwnd) != FALSE;
                        if (is_visible) {
                            main_window->hide();
                        } else {
                            main_window->show();
                        }
                    },
                    main_window);
                return 0;
            }
            case WM_RBUTTONUP: {
                POINT pt{};
                auto log = tray->sup.get_logger();
                if (GetCursorPos(&pt)) {
                    HWND hwnd = (HWND)fl_xid(main_window);
                    if (hwnd) {
                        auto &log = tray->sup.get_logger();
                        LOG_TRACE(log, "displaying menu at ({},{})", pt.x, pt.y);
                        auto menu = make_menu(*tray);
                        TrackPopupMenu(menu.get(), TPM_LEFTBUTTON, pt.x, pt.y, 0, tray->handle, nullptr);
                    }
                }
                return 0;
            }
            }
        } else if (message == WM_COMMAND) {
            auto &log = tray->sup.get_logger();
            auto &menus = tray->menu_items;
            auto idx = LOWORD(wParam);
            if (idx < menus.size()) {
                auto item = menus[idx];
                if (item.text) {
                    item.do_callback(nullptr, tray);
                    return 0;
                }
            }
        }
    }
    return DefWindowProcW(handle, message, wParam, lParam);
}

// clang-format off
// adopted from FLTK sources,
// https://github.com/fltk/fltk/discussions/1478
static HICON image_to_icon(const Fl_RGB_Image *image) {
  BITMAPV5HEADER bi;
  HBITMAP bitmap, mask;
  DWORD *bits;
  HICON icon;

  memset(&bi, 0, sizeof(BITMAPV5HEADER));

  bi.bV5Size        = sizeof(BITMAPV5HEADER);
  bi.bV5Width       = image->data_w();
  bi.bV5Height      = -image->data_h(); // Negative for top-down
  bi.bV5Planes      = 1;
  bi.bV5BitCount    = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask     = 0x00FF0000;
  bi.bV5GreenMask   = 0x0000FF00;
  bi.bV5BlueMask    = 0x000000FF;
  bi.bV5AlphaMask   = 0xFF000000;

  HDC hdc;

  hdc = GetDC(NULL);
  bitmap = CreateDIBSection(hdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
  ReleaseDC(NULL, hdc);

  if (bits == NULL)
    return NULL;

  const uchar *i = (const uchar *)*image->data();
  const int extra_data = image->ld() ? (image->ld() - image->data_w() * image->d()) : 0;

  for (int y = 0; y < image->data_h(); y++) {
    for (int x = 0; x < image->data_w(); x++) {
      switch (image->d()) {
        case 1:
          *bits = (0xff << 24) | (i[0] << 16) | (i[0] << 8) | i[0];
          break;
        case 2:
          *bits = (i[1] << 24) | (i[0] << 16) | (i[0] << 8) | i[0];
          break;
        case 3:
          *bits = (0xff << 24) | (i[0] << 16) | (i[1] << 8) | i[2];
          break;
        case 4:
          *bits = (i[3] << 24) | (i[0] << 16) | (i[1] << 8) | i[2];
          break;
      }
      i += image->d();
      bits++;
    }
    i += extra_data;
  }

  // A mask bitmap is still needed even though it isn't used
  mask = CreateBitmap(image->data_w(), image->data_h(), 1, 1, NULL);
  if (mask == NULL) {
    DeleteObject(bitmap);
    return NULL;
  }

  ICONINFO ii;

  ii.fIcon    = true;
  ii.xHotspot = 0;
  ii.yHotspot = 0;
  ii.hbmMask  = mask;
  ii.hbmColor = bitmap;

  icon = CreateIconIndirect(&ii);

  DeleteObject(bitmap);
  DeleteObject(mask);

  return icon;
}
// clang-format on

tray_win32_t *tray_win32_t::init(app_supervisor_t &sup) noexcept {

    auto ptr = new tray_win32_t(sup);
    if (ptr->valid) {
        return ptr;
    }

    delete ptr;
    return nullptr;
}

tray_win32_t::tray_win32_t(app_supervisor_t &sup_) : tray_impl_t{sup_} {
    std::memset(&notify_data, 0, sizeof(notify_data));

    auto &log = sup.get_logger();

    instance = GetModuleHandle(nullptr);
    if (!instance) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot GetModuleHandle: {}", ec);
        return;
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = tray_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = tray_window_class_str;
    if (!RegisterClassW(&window_class)) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot RegisterClass: {}", ec);
        return;
    }
    has_window_class = true;

    handle = CreateWindowExW(0, tray_window_class_str, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!handle) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot CreateWindow: {}", ec);
        return;
    }

    auto main_window = sup.get_main_window();
    auto main_handle = reinterpret_cast<HWND>(fl_xid(main_window));
    if (!main_handle) {
        return;
    }

    auto icon = reinterpret_cast<HICON>(SendMessageW(main_handle, WM_GETICON, ICON_SMALL, 0));
    if (!icon) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot get icon via SendMessage: {}", ec);
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(main_handle, GCLP_HICONSM));
    }
    if (!icon) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot get icon via GetClassLongPtr: {}", ec);
        icon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    if (!icon) {
        return;
    }
    icon_default = CopyIcon(icon);

    tray_message = ::RegisterWindowMessageW(tray_message_str);
    if (!tray_message) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot get icon via RegisterWindowMessage: {}", ec);
        return;
    }

    auto pp = SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&tray_proc));
    if (!pp) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        LOG_WARN(log, "cannot SetWindowLongPtr: {}", ec);
        return;
    }
    parent_proc = reinterpret_cast<WNDPROC>(pp);

    notify_data.cbSize = sizeof(notify_data);
    notify_data.hWnd = handle;
    notify_data.uID = 1;
    notify_data.uFlags = NIF_MESSAGE | NIF_ICON;
    notify_data.uCallbackMessage = tray_message;
    notify_data.hIcon = icon_default;

    property = SetPropW(handle, tray_property_str, this);
    shown = Shell_NotifyIconW(NIM_ADD, &notify_data);
    if (shown) {
        valid = true;
    }
    make_traffic_icon();
}

tray_win32_t::~tray_win32_t() {
    if (shown) {
        Shell_NotifyIconW(NIM_DELETE, &notify_data);
    }
    if (parent_proc) {
        SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(parent_proc));
    }
    if (property) {
        RemovePropW(handle, tray_property_str);
    }
    if (icon_default) {
        DestroyIcon(icon_default);
    }
    if (icon_traffic) {
        DestroyIcon(icon_traffic);
    }
    if (handle) {
        DestroyWindow(handle);
    }
    if (has_window_class) {
        UnregisterClassW(tray_window_class_str, instance);
    }
}

void tray_win32_t::make_traffic_icon() noexcept {
    auto info = ICONINFO{};
    int w = GetSystemMetrics(SM_CXICON);
    int h = GetSystemMetrics(SM_CYICON);
    if (!w || !h) {
        return;
    }
    auto copy = image_icon_t(static_cast<Fl_RGB_Image *>(traffic_image->copy(w, h)));
    icon_traffic = image_to_icon(copy.get());
}

bool tray_win32_t::is_enabled() noexcept { return valid && shown; }

void tray_win32_t::set_default_icon() noexcept {
    if (valid && shown) {
        notify_data.hIcon = icon_default;
        shown = Shell_NotifyIconW(NIM_MODIFY, &notify_data);
    }
}

void tray_win32_t::set_traffic_icon() noexcept {
    if (valid && shown && icon_traffic) {
        notify_data.hIcon = icon_traffic;
        shown = Shell_NotifyIconW(NIM_MODIFY, &notify_data);
    }
}

#endif
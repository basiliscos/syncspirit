// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_MACOS)

#ifdef __cplusplus
extern "C" {
#endif

using quit_callback_t = void (*)(void *);

bool syncspirit_mac_app_install_quit_handler(quit_callback_t callback, void *);
void syncspirit_mac_app_helper_hide();
void syncspirit_mac_app_helper_show();

#ifdef __cplusplus
}
#endif

#endif

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_MACOS)

#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Menu_Item.H>

#ifdef __cplusplus
extern "C" {
#endif

using syncspirit_mac_tray_callback_t = void (*)(void *);

void *syncspirit_mac_tray_create(const Fl_RGB_Image *main);
void syncspirit_mac_tray_assing_menu(void *, Fl_Menu_Item *items);
void syncspirit_mac_tray_assing_traffic_icon(void *, const Fl_RGB_Image *image);
void syncspirit_mac_tray_assing_offline_icon(void *, const Fl_RGB_Image *image);
void syncspirit_mac_tray_destroy(void *);
bool syncspirit_mac_tray_enabled(void *);
void syncspirit_mac_tray_set_default_icon(void *);
void syncspirit_mac_tray_set_traffic_icon(void *);
void syncspirit_mac_tray_set_offline_icon(void *);

#ifdef __cplusplus
}
#endif

#endif

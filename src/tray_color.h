/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TRAY_COLOR_H
#define TRAY_COLOR_H
#include <cfg.h>
#include <ui.h>
enum tray_color_level { TRAY_NORMAL, TRAY_WARM, TRAY_HOT, TRAY_CRITICAL };
enum tray_color_level tray_color_value(const struct tray_color_rule *, double);
const char *tray_color_icon(enum tray_color_level, bool);
void tray_color_reload(struct ui_psensor *);
void tray_color_sample(struct ui_psensor *, bool);
void tray_color_render(void);
void tray_color_cleanup(void);
#endif

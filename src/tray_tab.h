/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TRAY_TAB_H
#define TRAY_TAB_H
#include <ui.h>
void tray_tab_create(GtkGrid *, struct psensor *(*)(void), struct ui_psensor *);
void tray_tab_update(struct psensor *);
#endif

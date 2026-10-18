/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef COOLING_TAB_H
#define COOLING_TAB_H
#include <ui.h>
GtkWidget *cooling_tab_create(struct psensor *(*)(void));
void cooling_tab_update(struct psensor *);
void cooling_tab_refresh(struct psensor *);
void cooling_tab_focus(void);
#endif

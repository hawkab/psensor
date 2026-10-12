/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ACTION_TAB_H
#define ACTION_TAB_H
#include <ui.h>
void action_tab_create(GtkNotebook *, struct psensor *(*)(void));
void action_tab_update(struct psensor *);
void action_tab_focus(void);
#endif

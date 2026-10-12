/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SENSOR_ACTION_H
#define SENSOR_ACTION_H
#include <cfg.h>
#include <ui.h>

struct action_timer {
	gint64 since, last;
	bool fired;
};
bool action_timer_step(struct action_timer *, const struct sensor_action *,
		       double, gint64, gint64);
void sensor_actions_tick(struct ui_psensor *);
void sensor_actions_init(struct ui_psensor *, GSourceFunc);
void sensor_actions_reload(void);
guint sensor_actions_timer_id(void);
void sensor_action_reset(const char *);
const char *sensor_action_status(const char *);
#endif

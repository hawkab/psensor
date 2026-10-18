/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef COOLING_POLICY_H
#define COOLING_POLICY_H
#include <cfg.h>
#include <ui.h>

enum cooling_profile { COOL_NONE, COOL_PERFORMANCE, COOL_BALANCED, COOL_SAVER };
enum cooling_reason { COOL_NORMAL, COOL_WARM, COOL_HOT, COOL_HOLD };
struct cooling_state {
	gint64 last, warm_since, hot_since, hold_until;
	enum cooling_profile hold;
};
enum cooling_profile cooling_decide(struct cooling_state *, const struct cooling_rule *,
		double, enum cooling_profile, gint64, gint64, enum cooling_reason *);
void cooling_applied(struct cooling_state *, const struct cooling_rule *,
		enum cooling_profile, enum cooling_reason, gint64);
bool cooling_policy_reload(struct ui_psensor *);
void cooling_policy_tick(void);
const char *cooling_policy_status(const char *);
#endif

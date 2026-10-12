/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ACTION_NOTIFY_H
#define ACTION_NOTIFY_H
#include <gio/gio.h>
char *sensor_action_uri(const char *id);
char *sensor_action_id_from_uri(const char *uri, GError **error);
void sensor_action_notify(const char *id, const char *condition,
			  const char *command, const char *result,
			  gboolean success, gboolean started);
#endif

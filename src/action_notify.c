/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <action_notify.h>
#include <plog.h>

struct action_notification {
	char *uri, *body, *title;
};
static GHashTable *notifications;
static GDBusConnection *notification_bus;

char *sensor_action_uri(const char *id)
{
	char *escaped = g_uri_escape_string(id, NULL, FALSE);
	char *uri = g_strdup_printf("psensor://sensor/%s?tab=action", escaped);
	g_free(escaped);
	return uri;
}

char *sensor_action_id_from_uri(const char *uri, GError **error)
{
	GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_ENCODED_PATH, error);
	char *id = NULL;
	if (!parsed)
		return NULL;
	if (g_strcmp0(g_uri_get_scheme(parsed), "psensor") == 0 &&
	    g_strcmp0(g_uri_get_host(parsed), "sensor") == 0 &&
	    g_uri_get_userinfo(parsed) == NULL &&
	    g_uri_get_port(parsed) == -1 &&
	    g_strcmp0(g_uri_get_query(parsed), "tab=action") == 0 &&
	    g_uri_get_fragment(parsed) == NULL && g_uri_get_path(parsed) &&
	    g_uri_get_path(parsed)[0] == '/')
		id = g_uri_unescape_string(g_uri_get_path(parsed) + 1, NULL);
	if (!id || !*id || !g_utf8_validate(id, -1, NULL)) {
		g_clear_pointer(&id, g_free);
		if (error && !*error)
			g_set_error_literal(
			    error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			    "Некорректная ссылка на датчик Psensor");
	}
	g_uri_unref(parsed);
	return id;
}

static void release_notification(gpointer data)
{
	struct action_notification *note = data;
	g_free(note->uri);
	g_free(note->body);
	g_free(note->title);
	g_free(note);
}

static void uri_launched(GObject *object, GAsyncResult *result, gpointer data)
{
	GError *error = NULL;
	if (!g_app_info_launch_default_for_uri_finish(result, &error)) {
		log_warn("Could not open sensor action link: %s",
			 error->message);
		g_error_free(error);
	}
}

static void notification_signal(GDBusConnection *connection,
				const gchar *sender, const gchar *path,
				const gchar *interface, const gchar *signal,
				GVariant *parameters, gpointer data)
{
	guint id;
	if (g_str_equal(signal, "ActionInvoked")) {
		const char *action;
		struct action_notification *note;
		g_variant_get(parameters, "(u&s)", &id, &action);
		note = g_hash_table_lookup(notifications, GUINT_TO_POINTER(id));
		if (note && g_str_equal(action, "default"))
			g_app_info_launch_default_for_uri_async(
			    note->uri, NULL, NULL, uri_launched, NULL);
	} else if (g_str_equal(signal, "NotificationClosed")) {
		guint reason;
		g_variant_get(parameters, "(uu)", &id, &reason);
		g_hash_table_remove(notifications, GUINT_TO_POINTER(id));
	}
}

static void sent(GObject *object, GAsyncResult *result, gpointer data)
{
	struct action_notification *note = data;
	GError *error = NULL;
	GVariant *reply = g_dbus_connection_call_finish(
	    G_DBUS_CONNECTION(object), result, &error);
	if (reply) {
		guint id;
		g_variant_get(reply, "(u)", &id);
		g_hash_table_replace(notifications, GUINT_TO_POINTER(id), note);
		g_variant_unref(reply);
	} else {
		log_warn("Could not show sensor action notification: %s",
			 error->message);
		g_error_free(error);
		release_notification(note);
	}
}

static void bus_ready(GObject *object, GAsyncResult *result, gpointer data)
{
	struct action_notification *note = data;
	GError *error = NULL;
	GDBusConnection *connection = g_bus_get_finish(result, &error);
	GVariantBuilder hints;
	const char *actions[] = {"default", "Открыть датчик", NULL};
	if (!connection) {
		log_warn("Could not connect to notification service: %s",
			 error->message);
		g_error_free(error);
		release_notification(note);
		return;
	}
	if (!notification_bus) {
		notification_bus = g_object_ref(connection);
		notifications = g_hash_table_new_full(
		    g_direct_hash, g_direct_equal, NULL, release_notification);
		g_dbus_connection_signal_subscribe(
		    connection, "org.freedesktop.Notifications",
		    "org.freedesktop.Notifications", NULL,
		    "/org/freedesktop/Notifications", NULL,
		    G_DBUS_SIGNAL_FLAGS_NONE, notification_signal, NULL, NULL);
	}
	g_variant_builder_init(&hints, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&hints, "{sv}", "desktop-entry",
			      g_variant_new_string("psensor"));
	g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(1));
	g_dbus_connection_call(connection, "org.freedesktop.Notifications",
			       "/org/freedesktop/Notifications",
			       "org.freedesktop.Notifications", "Notify",
			       g_variant_new("(susss^as@a{sv}i)", "Psensor", 0u,
					     "psensor", note->title, note->body,
					     actions,
					     g_variant_builder_end(&hints), -1),
			       G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE,
			       5000, NULL, sent, note);
	g_object_unref(connection);
}

void sensor_action_notify(const char *id, const char *condition,
			  const char *command, const char *result,
			  gboolean success, gboolean started)
{
	struct action_notification *note =
	    g_new0(struct action_notification, 1);
	GDateTime *now = g_date_time_new_now_local();
	char *stamp = g_date_time_format(now, "%H:%M:%S %d.%m.%Y");
	char *safe_condition = g_markup_escape_text(condition, -1);
	char *safe_command = g_markup_escape_text(command, -1);
	char *safe_result = g_markup_escape_text(result, -1);
	note->uri = sensor_action_uri(id);
	note->title =
	    g_strdup(success   ? "Psensor: команда выполнена"
		     : started ? "Psensor: команда завершилась с ошибкой"
			       : "Psensor: не удалось запустить команду");
	note->body =
	    success ? g_strdup_printf("%s\nКоманда: %s\n%s", safe_condition,
				      safe_command, stamp)
		    : g_strdup_printf("%s\nКоманда: %s\n%s\n%s", safe_condition,
				      safe_command, safe_result, stamp);
	g_free(safe_condition);
	g_free(safe_command);
	g_free(safe_result);
	g_free(stamp);
	g_date_time_unref(now);
	/* All desktop-service calls are asynchronous, keeping GTK responsive.
	 */
	g_bus_get(G_BUS_TYPE_SESSION, NULL, bus_ready, note);
}

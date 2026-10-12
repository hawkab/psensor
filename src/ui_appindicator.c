/*
 * Copyright (C) 2010-2016 jeanfi@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>

#include <cfg.h>
#include <psensor.h>
#include <ui.h>
#include <ui_appindicator.h>
#include <ui_sensorpref.h>
#include <ui_status.h>
#include <ui_pref.h>

static const char *ICON = "psensor_normal";
static const char *ATTENTION_ICON = "psensor_hot";

static const char *GLADE_FILE
= PACKAGE_DATA_DIR G_DIR_SEPARATOR_S "psensor-appindicator.glade";

static struct psensor **sensors;
static GtkMenuItem **menu_items;
static struct psensor **label_sensors;
static bool appindicator_supported = true;
static AppIndicator *indicator;
static struct ui_psensor *ui_psensor;
static GPid analysis_pid;

/* Normal make install uses datadir; retain the original per-user layout too. */
static gchar *analysis_file(const gchar *name)
{
	const gchar *override = g_getenv("PSENSOR_ANALYSIS_DIR");
	gchar *path;
	if (override && *override)
		return g_build_filename(override, name, NULL);
	path = g_build_filename(DATADIR, "psensor-analysis", name, NULL);
	if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
		return path;
	g_free(path);
	return g_build_filename(g_get_user_data_dir(), "psensor-analysis", name,
				NULL);
}

static void analysis_closed(GPid pid, gint status, gpointer data)
{
	analysis_pid = 0;
	g_spawn_close_pid(pid);
}

static void spawn_analysis(void)
{
	if (analysis_pid)
		return;
	gchar *path = analysis_file("load-window.py");
	gchar *args[] = {"/usr/bin/python3", path, NULL};
	GError *error = NULL;
	if (!g_spawn_async(NULL, args, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL,
			   NULL, &analysis_pid, &error)) {
		log_err("Cannot open load analysis: %s", error->message);
		g_error_free(error);
	} else
		g_child_watch_add(analysis_pid, analysis_closed, NULL);
	g_free(path);
}

static void analysis_owner(GObject *object, GAsyncResult *result, gpointer data)
{
	GError *error = NULL;
	GVariant *reply = g_dbus_connection_call_finish(
	    G_DBUS_CONNECTION(object), result, &error);
	gboolean owner = FALSE;
	if (reply) {
		g_variant_get(reply, "(b)", &owner);
		g_variant_unref(reply);
	}
	g_clear_error(&error);
	if (owner) {
		GVariantBuilder options;
		g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
		g_dbus_connection_call(
		    G_DBUS_CONNECTION(object), "local.psensor.LoadAnalysis",
		    "/local/psensor/LoadAnalysis",
		    "org.freedesktop.Application", "Activate",
		    g_variant_new("(@a{sv})", g_variant_builder_end(&options)),
		    NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
	} else
		spawn_analysis();
}

static void analysis_bus(GObject *object, GAsyncResult *result, gpointer data)
{
	GError *error = NULL;
	GDBusConnection *bus = g_bus_get_finish(result, &error);
	if (!bus) {
		g_clear_error(&error);
		spawn_analysis();
		return;
	}
	/* Publish the latest buffered sample before opening the first view. */
	g_dbus_connection_call(
	    bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
	    "org.freedesktop.systemd1.Manager", "KillUnit",
	    g_variant_new("(ssi)", "psensor-analysis.service", "main", SIGUSR1),
	    NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
	g_dbus_connection_call(
	    bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
	    "org.freedesktop.DBus", "NameHasOwner",
	    g_variant_new("(s)", "local.psensor.LoadAnalysis"),
	    G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL,
	    analysis_owner, NULL);
	g_object_unref(bus);
}

/* The analysis window has its own event loop and reads interval history. */
static void open_analysis(GtkMenuItem *item, gpointer unused)
{
	g_bus_get(G_BUS_TYPE_SESSION, NULL, analysis_bus, NULL);
}

static void set_menu_icons(GtkBuilder *builder)
{
	const char *ids[] = {"menuitem8", "menuitem1", "menuitem2", "menuitem5",
			     "menuitem7"};
	const char *names[] = {
	    "utilities-system-monitor-symbolic", "preferences-system-symbolic",
	    "preferences-other-symbolic", "help-about-symbolic",
	    "application-exit-symbolic"};
	unsigned int i;
	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		GtkWidget *item =
		    GTK_WIDGET(gtk_builder_get_object(builder, ids[i]));
		GtkWidget *image =
		    gtk_image_new_from_icon_name(names[i], GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_use_stock(GTK_IMAGE_MENU_ITEM(item),
						  FALSE);
		gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
		gtk_image_menu_item_set_always_show_image(
		    GTK_IMAGE_MENU_ITEM(item), TRUE);
		gtk_widget_show(image);
	}
}

static void create_analysis(GtkMenu *menu)
{
	GtkWidget *item =
	    gtk_image_menu_item_new_with_label("Анализ нагрузки и нагрева");
	GtkWidget *image = gtk_image_new_from_icon_name(
	    "utilities-system-monitor-symbolic", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
	gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),
						  TRUE);
	g_signal_connect(item, "activate", G_CALLBACK(open_analysis), NULL);
	gtk_menu_shell_insert(GTK_MENU_SHELL(menu), item, 0);
	gtk_menu_shell_insert(GTK_MENU_SHELL(menu),
			      gtk_separator_menu_item_new(), 1);
}

void ui_appindicator_menu_show_cb(GtkMenuItem *mi, gpointer data)
{
	ui_window_show((struct ui_psensor *)data);
}

void ui_appindicator_cb_preferences(GtkMenuItem *mi, gpointer data)
{
	ui_pref_dialog_run((struct ui_psensor *)data);
}

void ui_appindicator_cb_sensor_preferences(GtkMenuItem *mi, gpointer data)
{
	struct ui_psensor *ui = data;

	if (ui->sensors && *ui->sensors)
		ui_sensorpref_dialog_run(*ui->sensors, ui);
}

static void
update_menu_item(GtkMenuItem *item, struct psensor *s, int use_celsius)
{
	gchar *str;
	char *v;

	v = psensor_current_value_to_str(s, use_celsius);

	str = g_strdup_printf("%s: %s", s->name, v);

	if (g_strcmp0(gtk_menu_item_get_label(item), str) != 0)
		gtk_menu_item_set_label(item, str);

	free(v);
	g_free(str);
}

static void update_menu_items(int use_celsius)
{
	struct psensor **s;
	GtkMenuItem **m;

	if (!sensors)
		return;

	for (s = sensors, m = menu_items; *s; s++, m++)
		update_menu_item(*m, *s, use_celsius);
}

static void
create_sensor_menu_items(const struct ui_psensor *ui, GtkMenu *menu)
{
	int i, j, k, n, celsius;
	const char *name;
	struct psensor **sorted_sensors;

	if (config_get_temperature_unit() == CELSIUS)
		celsius = 1;
	else
		celsius = 0;

	sorted_sensors = ui_get_sensors_ordered_by_position(ui->sensors);
	n = psensor_list_size(sorted_sensors);
	menu_items = malloc((n + 1) * sizeof(GtkWidget *));

	sensors = malloc((n + 1) * sizeof(struct psensor *));
	label_sensors = malloc((n + 1) * sizeof(struct psensor *));
	for (i = 0, j = 0, k = 0; i < n; i++) {
		if (config_is_appindicator_label_enabled(sorted_sensors[i]->id))
			label_sensors[k++] = sorted_sensors[i];
		if (config_is_appindicator_enabled(sorted_sensors[i]->id)) {
			sensors[j] = sorted_sensors[i];
			name = sensors[j]->name;

			menu_items[j] = GTK_MENU_ITEM
				(gtk_menu_item_new_with_label(name));

			gtk_menu_shell_insert(GTK_MENU_SHELL(menu),
					      GTK_WIDGET(menu_items[j]),
					      j+2);

			update_menu_item(menu_items[j], sensors[j], celsius);

			j++;
		}
	}

	sensors[j] = NULL;
	label_sensors[k] = NULL;
	menu_items[j] = NULL;

	free(sorted_sensors);
}

static GtkMenu *load_menu(struct ui_psensor *ui)
{
	GError *error;
	GtkMenu *menu;
	guint ok;
	GtkBuilder *builder;

	log_fct_enter();

	builder = gtk_builder_new();

	error = NULL;
	ok = gtk_builder_add_from_file(builder, GLADE_FILE, &error);

	if (!ok) {
		log_err(_("Failed to load glade file %s: %s"),
			GLADE_FILE,
			error->message);
		g_error_free(error);
		return NULL;
	}

	menu = GTK_MENU(gtk_builder_get_object(builder, "appindicator_menu"));
	create_sensor_menu_items(ui, menu);
	create_analysis(menu);
	gtk_builder_connect_signals(builder, ui);
	set_menu_icons(builder);

	g_object_ref(G_OBJECT(menu));
	g_object_unref(G_OBJECT(builder));

	log_fct_exit();

	return menu;
}

static void update_label(struct ui_psensor *ui)
{
	char *label, *str, *tmp, *guide;
	struct psensor **p;
	int use_celsius;

	p = label_sensors;
	if (!p || !*p) {
		app_indicator_set_label(indicator, NULL, NULL);
		return;
	}
	label = NULL;
	guide = NULL;

	if (config_get_temperature_unit() == CELSIUS)
		use_celsius = 1;
	else
		use_celsius = 0;

	while (*p) {
		{
			str = psensor_current_value_to_str(*p, use_celsius);

			if (label == NULL) {
				label = str;
			} else {
				tmp = malloc(strlen(label)
					     + 1
					     + strlen(str)
					     + 1);
				sprintf(tmp, "%s %s", label, str);
				free(label);
				free(str);
				label = tmp;
			}

			if (is_temp_type((*p)->type))
				str = "999UUU";
			else if ((*p)->type & SENSOR_TYPE_RPM)
				str = "999UUU";
			else /* percent */
				str = "999%";

			if (guide == NULL) {
				guide = strdup(str);
			} else {
				tmp = malloc(strlen(guide)
					     + 1
					     + strlen(str)
					     + 1);
				sprintf(tmp, "%sW%s", guide, str);
				free(guide);
				guide = tmp;
			}
		}
		p++;
	}

	app_indicator_set_label(indicator, label, guide);
	free(label);
	free(guide);
}

void ui_appindicator_update(struct ui_psensor *ui, bool attention)
{
	AppIndicatorStatus status;

	if (!indicator)
		return;

	update_label(ui);

	status = app_indicator_get_status(indicator);

	if (!attention && status == APP_INDICATOR_STATUS_ATTENTION)
		app_indicator_set_status(indicator,
					 APP_INDICATOR_STATUS_ACTIVE);

	if (attention && status == APP_INDICATOR_STATUS_ACTIVE)
		app_indicator_set_status(indicator,
		APP_INDICATOR_STATUS_ATTENTION);

	if (config_get_temperature_unit() == CELSIUS)
		update_menu_items(1);
	else
		update_menu_items(0);
}

static GtkStatusIcon *unity_fallback(AppIndicator *indicator)
{
	GtkStatusIcon *ico;

	log_debug("ui_appindicator.unity_fallback()");

	appindicator_supported = false;

	ico = ui_status_get_icon(ui_psensor);

	ui_status_set_visible(1);

	return ico;
}

static void
unity_unfallback(AppIndicator *indicator, GtkStatusIcon *status_icon)
{
	log_debug("ui_appindicator.unity_unfallback()");

	ui_status_set_visible(0);

	appindicator_supported = true;
}

static void remove_sensor_menu_items(GtkMenu *menu)
{
	GtkMenuItem **items;

	if (!menu_items)
		return;

	items = menu_items;
	while (*items) {
		gtk_container_remove(GTK_CONTAINER(menu), GTK_WIDGET(*items));

		items++;
	}

	free(menu_items);
	free(sensors);
	free(label_sensors);
	label_sensors = NULL;
}

void ui_appindicator_update_menu(struct ui_psensor *ui)
{
	GtkMenu *menu;
	/* Preferences can change labels/units without a new hardware sample. */
	ui->indicated_generation = G_MAXUINT64;

	menu = GTK_MENU(app_indicator_get_menu(indicator));

	if (menu) {
		remove_sensor_menu_items(menu);
		create_sensor_menu_items(ui, menu);
	} else {
		menu = load_menu(ui);

		if (menu) {
			app_indicator_set_menu(indicator, menu);
			g_object_unref(G_OBJECT(menu));
		}
	}

	if (menu) {
		gtk_widget_show_all(GTK_WIDGET(menu));
	}
}

void ui_appindicator_init(struct ui_psensor *ui)
{
	ui_psensor = ui;
	/* Absolute PNG paths avoid icon-theme lookup failures in tray hosts. */
	ICON = analysis_file("icons/psensor_normal.png");
	ATTENTION_ICON = analysis_file("icons/psensor_hot.png");

	indicator = app_indicator_new
		("psensor",
		 ICON,
		 APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

	APP_INDICATOR_GET_CLASS(indicator)->fallback = unity_fallback;
	APP_INDICATOR_GET_CLASS(indicator)->unfallback = unity_unfallback;

	app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
	app_indicator_set_attention_icon(indicator, ATTENTION_ICON);

	ui_appindicator_update_menu(ui);
}

bool is_appindicator_supported(void)
{
	return appindicator_supported;
}

void ui_appindicator_cleanup(void)
{
	free(sensors);
	free(menu_items);
	free(label_sensors);
}

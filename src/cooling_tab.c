/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <cooling_tab.h>
#include <action_tab.h>
#include <cooling_policy.h>
#include <sensor_action.h>
#include <temperature.h>

static GtkWidget *box;
static GtkToggleButton *enabled, *restore;
static GtkSpinButton *warm, *hot, *warm_minutes, *hot_seconds, *balanced_minutes, *saver_minutes;
static GtkLabel *status, *unit;
static struct psensor *(*selected)(void);

static GtkWidget *label(const char *text)
{
	GtkWidget *w = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_label_set_line_wrap(GTK_LABEL(w), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(w), 54);
	return w;
}

static GtkSpinButton *number(GtkWidget *grid, const char *name, int row, double min, double max)
{
	GtkSpinButton *w = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(min, max, 1));
	gtk_grid_attach(GTK_GRID(grid), label(name), 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(w), 1, row, 1, 1);
	return w;
}

static void save(GtkButton *button, gpointer data)
{
	struct psensor *s = selected();
	if (!s || !(s->type & SENSOR_TYPE_TEMP)) return;
	struct cooling_rule r = {
		.enabled = gtk_toggle_button_get_active(enabled),
		.restore_performance = gtk_toggle_button_get_active(restore),
		.warm = gtk_spin_button_get_value(warm),
		.hot = gtk_spin_button_get_value(hot),
		.warm_seconds = gtk_spin_button_get_value_as_int(warm_minutes) * 60,
		.hot_seconds = gtk_spin_button_get_value_as_int(hot_seconds),
		.balanced_seconds = gtk_spin_button_get_value_as_int(balanced_minutes) * 60,
		.saver_seconds = gtk_spin_button_get_value_as_int(saver_minutes) * 60
	};
	if (config_get_temperature_unit() == FAHRENHEIT) {
		r.warm = fahrenheit_to_celsius(r.warm);
		r.hot = fahrenheit_to_celsius(r.hot);
	}
	if (r.warm >= r.hot) {
		gtk_label_set_text(status, "Порог энергосбережения должен быть выше порога balanced");
		return;
	}
	config_set_cooling_rule(s->id, &r);
	config_sync();
	sensor_action_reset(s->id);
	/* Also refresh the command checkbox, disabled for this sensor. */
	action_tab_update(s);
}

void cooling_tab_refresh(struct psensor *s)
{
	if (box && s)
		gtk_label_set_text(status, cooling_policy_status(s->id));
}

void cooling_tab_focus(void)
{
	if (box)
		gtk_widget_grab_focus(GTK_WIDGET(enabled));
}

void cooling_tab_update(struct psensor *s)
{
	if (!box || !s) return;
	gtk_widget_set_visible(box, (s->type & SENSOR_TYPE_TEMP) != 0);
	struct cooling_rule r;
	config_get_cooling_rule(s->id, &r);
	bool f = config_get_temperature_unit() == FAHRENHEIT;
	gtk_toggle_button_set_active(enabled, r.enabled);
	gtk_toggle_button_set_active(restore, r.restore_performance);
	gtk_spin_button_set_value(warm, f ? celsius_to_fahrenheit(r.warm) : r.warm);
	gtk_spin_button_set_value(hot, f ? celsius_to_fahrenheit(r.hot) : r.hot);
	gtk_spin_button_set_value(warm_minutes, r.warm_seconds / 60.0);
	gtk_spin_button_set_value(hot_seconds, r.hot_seconds);
	gtk_spin_button_set_value(balanced_minutes, r.balanced_seconds / 60.0);
	gtk_spin_button_set_value(saver_minutes, r.saver_seconds / 60.0);
	gtk_label_set_text(unit, f ? "Температурные пороги, °F" : "Температурные пороги, °C");
	cooling_tab_refresh(s);
}

static void destroyed(GtkWidget *widget, gpointer data) { box = NULL; }

GtkWidget *cooling_tab_create(struct psensor *(*get_selected)(void))
{
	selected = get_selected;
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget *heading = label("Автоматическое охлаждение ЦП");
	PangoAttrList *attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes(GTK_LABEL(heading), attrs);
	pango_attr_list_unref(attrs);
	gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);
	enabled = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label("Управлять режимом питания по этому датчику"));
	gtk_widget_set_name(GTK_WIDGET(enabled), "cooling-enabled");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(enabled), FALSE, FALSE, 0);
	unit = GTK_LABEL(label("Температурные пороги, °C"));
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(unit), FALSE, FALSE, 0);
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	warm = number(grid, "Порог balanced", 0, -100, 300);
	warm_minutes = number(grid, "Непрерывный нагрев, минут", 1, 1, 1440);
	balanced_minutes = number(grid, "Удерживать balanced, минут", 2, 1, 1440);
	hot = number(grid, "Порог энергосбережения (выше)", 3, -100, 300);
	hot_seconds = number(grid, "Задержка, секунд (0 — сразу)", 4, 0, 86400);
	saver_minutes = number(grid, "Удерживать энергосбережение, минут", 5, 1, 1440);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	restore = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label("Возвращать производительность после охлаждения"));
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(restore), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), label("После выдержки возврат возможен только ниже порога balanced. При сохраняющемся нагреве ограничение остаётся. Энергосбережение имеет приоритет."), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), label("Управляет одним общим режимом питания, пока работает Psensor. Включение отключит такое управление у других датчиков и произвольную команду у этого датчика. Каждая смена режима сопровождается уведомлением."), FALSE, FALSE, 0);
	GtkWidget *button = gtk_button_new_with_label("Сохранить охлаждение");
	g_signal_connect(button, "clicked", G_CALLBACK(save), NULL);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	status = GTK_LABEL(label("Автоматическое охлаждение выключено"));
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(status), FALSE, FALSE, 0);
	g_signal_connect(box, "destroy", G_CALLBACK(destroyed), NULL);
	return box;
}

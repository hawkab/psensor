/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <tray_tab.h>
#include <tray_color.h>
#include <temperature.h>

static GtkWidget *panel;
static GtkToggleButton *enabled;
static GtkSpinButton *warm, *hot;
static GtkLabel *status, *unit;
static struct psensor *(*selected)(void);
static struct ui_psensor *pref_ui;

static GtkWidget *text(const char *value)
{
	GtkWidget *w = gtk_label_new(value);
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_label_set_line_wrap(GTK_LABEL(w), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(w), 50);
	return w;
}

static void save(GtkButton *button, gpointer data)
{
	struct psensor *s = selected();
	if (!s || !(s->type & SENSOR_TYPE_TEMP)) return;
	struct tray_color_rule rule = {
		.enabled = gtk_toggle_button_get_active(enabled),
		.warm = gtk_spin_button_get_value(warm),
		.hot = gtk_spin_button_get_value(hot)
	};
	if (config_get_temperature_unit() == FAHRENHEIT) {
		rule.warm = fahrenheit_to_celsius(rule.warm);
		rule.hot = fahrenheit_to_celsius(rule.hot);
	}
	if (rule.warm >= rule.hot) {
		gtk_label_set_text(status, "Порог красного должен быть выше порога оранжевого");
		return;
	}
	config_set_tray_color(s->id, &rule);
	config_sync();
	tray_color_reload(pref_ui);
	gtk_label_set_text(status, rule.enabled ? "Цветовая индикация включена и сохранена" : "Цветовая индикация выключена");
}

void tray_tab_update(struct psensor *s)
{
	if (!panel || !s) return;
	gtk_widget_set_sensitive(panel, (s->type & SENSOR_TYPE_TEMP) != 0);
	struct tray_color_rule rule;
	config_get_tray_color(s->id, &rule);
	bool f = config_get_temperature_unit() == FAHRENHEIT;
	gtk_toggle_button_set_active(enabled, rule.enabled);
	gtk_spin_button_set_value(warm, f ? celsius_to_fahrenheit(rule.warm) : rule.warm);
	gtk_spin_button_set_value(hot, f ? celsius_to_fahrenheit(rule.hot) : rule.hot);
	gtk_label_set_text(unit, f ? "Температура, °F" : "Температура, °C");
	gtk_label_set_text(status, rule.enabled ? "Цветовая индикация включена" : "Цветовая индикация выключена");
}

static void destroyed(GtkWidget *w, gpointer unused) { panel = NULL; }

void tray_tab_create(GtkGrid *parent, struct psensor *(*get_selected)(void), struct ui_psensor *ui)
{
	selected = get_selected;
	pref_ui = ui;
	panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(panel), 12);
	gtk_widget_set_valign(GTK_WIDGET(parent), GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(panel), text("Цвет значка по температуре"), FALSE, FALSE, 0);
	enabled = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label("Использовать температуру этого датчика"));
	gtk_widget_set_name(GTK_WIDGET(enabled), "tray-color-enabled");
	gtk_box_pack_start(GTK_BOX(panel), GTK_WIDGET(enabled), FALSE, FALSE, 0);
	unit = GTK_LABEL(text("Температура, °C"));
	gtk_box_pack_start(GTK_BOX(panel), GTK_WIDGET(unit), FALSE, FALSE, 0);
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
	warm = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(-100, 500, 1));
	hot = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(-100, 500, 1));
	gtk_widget_set_name(GTK_WIDGET(warm), "tray-color-warm");
	gtk_widget_set_name(GTK_WIDGET(hot), "tray-color-hot");
	gtk_grid_attach(GTK_GRID(grid), text("Оранжевый — начиная с"), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(warm), 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), text("Мигающий красный — выше"), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(hot), 1, 1, 1, 1);
	gtk_box_pack_start(GTK_BOX(panel), grid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(panel), text("Значок меняется сразу по свежему измерению, независимо от таймеров охлаждения. При нескольких датчиках показывается самое серьёзное состояние."), FALSE, FALSE, 0);
	GtkWidget *button = gtk_button_new_with_label("Сохранить индикацию");
	g_signal_connect(button, "clicked", G_CALLBACK(save), NULL);
	gtk_box_pack_start(GTK_BOX(panel), button, FALSE, FALSE, 0);
	status = GTK_LABEL(text("Цветовая индикация выключена"));
	gtk_box_pack_start(GTK_BOX(panel), GTK_WIDGET(status), FALSE, FALSE, 0);
	gtk_grid_attach(parent, panel, 0, 2, 1, 1);
	g_signal_connect(panel, "destroy", G_CALLBACK(destroyed), NULL);
	gtk_widget_show_all(panel);
}

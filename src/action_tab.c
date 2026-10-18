/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <action_tab.h>
#include <cooling_tab.h>
#include <sensor_action.h>
#include <temperature.h>

static GtkWidget *panel, *command_expander;
static GtkToggleButton *enabled;
static GtkComboBox *direction;
static GtkSpinButton *threshold, *duration, *hysteresis;
static GtkEntry *command;
static GtkLabel *unit, *status;
static struct psensor *(*selected)(void);
static guint status_timer;

static GtkWidget *text(const char *value)
{
	GtkWidget *w = gtk_label_new(value);
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_label_set_line_wrap(GTK_LABEL(w), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(w), 55);
	return w;
}

static gboolean refresh_status(gpointer data)
{
	struct psensor *s = selected();
	if (s) {
		cooling_tab_refresh(s);
		struct sensor_action rule;
		config_get_sensor_action(s->id, &rule);
		gtk_label_set_text(status, rule.enabled
					       ? sensor_action_status(s->id)
					       : "Правило выключено");
		g_free(rule.command);
	}
	return G_SOURCE_CONTINUE;
}

static void destroyed(GtkWidget *widget, gpointer data)
{
	if (status_timer)
		g_source_remove(status_timer);
	status_timer = 0;
	panel = NULL;
}

static void save_clicked(GtkButton *button, gpointer data)
{
	struct psensor *s = selected();
	struct sensor_action rule;
	if (!s)
		return;
	rule.enabled = gtk_toggle_button_get_active(enabled);
	rule.below = gtk_combo_box_get_active(direction) == 1;
	rule.threshold = gtk_spin_button_get_value(threshold);
	rule.hysteresis = gtk_spin_button_get_value(hysteresis);
	rule.seconds = gtk_spin_button_get_value_as_int(duration);
	rule.command = (char *)gtk_entry_get_text(command);
	if (rule.enabled && !*rule.command) {
		gtk_label_set_text(status,
				   "Введите команду перед включением правила");
		return;
	}
	if ((s->type & SENSOR_TYPE_TEMP) &&
	    config_get_temperature_unit() == FAHRENHEIT) {
		rule.threshold = fahrenheit_to_celsius(rule.threshold);
		rule.hysteresis = rule.hysteresis * 5.0 / 9.0;
	}
	config_set_sensor_action(s->id, &rule);
	config_sync();
	sensor_action_reset(s->id);
	action_tab_update(s);
	gtk_label_set_text(status, rule.enabled
				       ? "Правило включено и сохранено"
				       : "Правило выключено и сохранено");
}

static void preset_clicked(GtkButton *button, gpointer data)
{
	gtk_entry_set_text(command, (const char *)data);
	gtk_label_set_text(
	    status,
	    "Команда подставлена. Проверьте условие и сохраните правило.");
}

void action_tab_update(struct psensor *s)
{
	struct sensor_action rule;
	bool fahrenheit;
	if (!panel || !s)
		return;
	cooling_tab_update(s);
	struct cooling_rule cooling;
	config_get_cooling_rule(s->id, &cooling);
	gtk_expander_set_expanded(GTK_EXPANDER(command_expander), !cooling.enabled);
	config_get_sensor_action(s->id, &rule);
	fahrenheit = (s->type & SENSOR_TYPE_TEMP) &&
		     config_get_temperature_unit() == FAHRENHEIT;
	gtk_toggle_button_set_active(enabled, rule.enabled);
	gtk_combo_box_set_active(direction, rule.below ? 1 : 0);
	gtk_spin_button_set_value(
	    threshold, fahrenheit ? celsius_to_fahrenheit(rule.threshold)
				  : rule.threshold);
	gtk_spin_button_set_value(hysteresis, fahrenheit
						  ? rule.hysteresis * 9.0 / 5.0
						  : rule.hysteresis);
	gtk_spin_button_set_value(duration, rule.seconds);
	gtk_entry_set_text(command, rule.command ? rule.command : "");
	gtk_label_set_text(unit,
			   psensor_type_to_unit_str(s->type, !fahrenheit));
	gtk_label_set_text(status, rule.enabled ? sensor_action_status(s->id)
						: "Правило выключено");
	g_free(rule.command);
}

void action_tab_create(GtkNotebook *notebook,
		       struct psensor *(*get_selected)(void))
{
	GtkWidget *commands, *grid, *condition, *preset, *save, *scroll, *advanced,
	    *options, *extra;
	selected = get_selected;
	panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(panel), 16);
	gtk_box_pack_start(GTK_BOX(panel), cooling_tab_create(get_selected), FALSE, FALSE, 0);
	commands = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(commands), 8);
	command_expander = gtk_expander_new("Произвольная команда");
	gtk_container_add(GTK_CONTAINER(command_expander), commands);
	gtk_box_pack_start(GTK_BOX(panel), command_expander, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(commands),
			   text("Команда при длительном условии"), FALSE, FALSE,
			   0);
	enabled = GTK_TOGGLE_BUTTON(
	    gtk_check_button_new_with_label("Включить для этого датчика"));
	gtk_box_pack_start(GTK_BOX(commands), GTK_WIDGET(enabled), FALSE, FALSE,
			   0);
	grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
	condition = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	direction = GTK_COMBO_BOX(gtk_combo_box_text_new());
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(direction),
				       "Не меньше");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(direction),
				       "Не больше");
	threshold = GTK_SPIN_BUTTON(
	    gtk_spin_button_new_with_range(-1000000, 1000000, 1));
	gtk_spin_button_set_digits(threshold, 1);
	unit = GTK_LABEL(text(""));
	gtk_box_pack_start(GTK_BOX(condition), GTK_WIDGET(direction), FALSE,
			   FALSE, 0);
	gtk_box_pack_start(GTK_BOX(condition), GTK_WIDGET(threshold), FALSE,
			   FALSE, 0);
	gtk_box_pack_start(GTK_BOX(condition), GTK_WIDGET(unit), FALSE, FALSE,
			   0);
	gtk_grid_attach(GTK_GRID(grid), text("Значение"), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), condition, 1, 0, 1, 1);
	duration = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 86400, 1));
	hysteresis =
	    GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 1000000, 1));
	gtk_spin_button_set_digits(hysteresis, 1);
	gtk_grid_attach(GTK_GRID(grid), text("Непрерывно, секунд"), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(duration), 1, 1, 1, 1);
	gtk_box_pack_start(GTK_BOX(commands), grid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(commands), text("Команда"), FALSE, FALSE, 0);
	command = GTK_ENTRY(gtk_entry_new());
	gtk_entry_set_placeholder_text(command,
				       "powerprofilesctl set power-saver");
	gtk_box_pack_start(GTK_BOX(commands), GTK_WIDGET(command), FALSE, FALSE,
			   0);
	preset = gtk_button_new_with_label("Команда: энергосбережение");
	g_signal_connect(preset, "clicked", G_CALLBACK(preset_clicked),
			 "powerprofilesctl set power-saver");
	gtk_widget_set_tooltip_text(preset, "Для снижения нагрева; задачи могут выполняться медленнее.");
	gtk_box_pack_start(GTK_BOX(commands), preset, FALSE, FALSE, 0);
	gtk_box_pack_start(
	    GTK_BOX(commands),
	    text("Команда запускается один раз и может повториться после "
		 "возврата к обычным значениям. Правило работает, пока запущен "
		 "Psensor."),
	    FALSE, FALSE, 0);
	gtk_box_pack_start(
	    GTK_BOX(commands),
	    text("После завершения придёт системное уведомление. Нажмите его, "
		 "чтобы открыть это правило."),
	    FALSE, FALSE, 0);
	advanced = gtk_expander_new("Дополнительно");
	options = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(options), 8);
	preset = gtk_button_new_with_label("Команда: сбалансированный режим");
	g_signal_connect(preset, "clicked", G_CALLBACK(preset_clicked),
			 "powerprofilesctl set balanced");
	gtk_widget_set_tooltip_text(preset, "Сбалансированный режим допускает турбобуст и высокую температуру под нагрузкой.");
	gtk_box_pack_start(GTK_BOX(options), preset, FALSE, FALSE, 0);
	extra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(extra), text("Запас для повторного запуска"),
			   TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(extra), GTK_WIDGET(hysteresis), FALSE, FALSE,
			   0);
	gtk_box_pack_start(GTK_BOX(options), extra, FALSE, FALSE, 0);
	gtk_box_pack_start(
	    GTK_BOX(options),
	    text("Команда выполняется через /bin/sh от вашего пользователя. "
		 "Доступны переменные PSENSOR_SENSOR_ID, PSENSOR_SENSOR_NAME, "
		 "PSENSOR_VALUE и PSENSOR_THRESHOLD. Для температуры значения "
		 "в них всегда в °C."),
	    FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(advanced), options);
	gtk_box_pack_start(GTK_BOX(commands), advanced, FALSE, FALSE, 0);
	save = gtk_button_new_with_label("Сохранить правило");
	g_signal_connect(save, "clicked", G_CALLBACK(save_clicked), NULL);
	gtk_box_pack_start(GTK_BOX(commands), save, FALSE, FALSE, 0);
	status = GTK_LABEL(text("Правило выключено"));
	gtk_box_pack_start(GTK_BOX(commands), GTK_WIDGET(status), FALSE, FALSE, 0);
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll),
						   360);
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll),
						  420);
	gtk_container_add(GTK_CONTAINER(scroll), panel);
	gtk_notebook_append_page(notebook, scroll, gtk_label_new("Действие"));
	gtk_widget_show_all(scroll);
	g_signal_connect(panel, "destroy", G_CALLBACK(destroyed), NULL);
	status_timer = g_timeout_add_seconds(2, refresh_status, NULL);
}

void action_tab_focus(void)
{
	GtkWidget *page, *notebook;
	if (!panel)
		return;
	notebook = gtk_widget_get_ancestor(panel, GTK_TYPE_NOTEBOOK);
	if (!notebook)
		return;
	page = panel;
	while (gtk_widget_get_parent(page) != notebook)
		page = gtk_widget_get_parent(page);
	gtk_notebook_set_current_page(
	    GTK_NOTEBOOK(notebook),
	    gtk_notebook_page_num(GTK_NOTEBOOK(notebook), page));
	struct psensor *s = selected();
	struct cooling_rule cooling = {0};
	if (s)
		config_get_cooling_rule(s->id, &cooling);
	if (cooling.enabled)
		cooling_tab_focus();
	else
		gtk_widget_grab_focus(GTK_WIDGET(command));
}

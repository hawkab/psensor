/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <tray_color.h>
#include <ui_appindicator.h>
#include <ui_status.h>
#include <math.h>

struct colored_sensor {
	struct psensor *sensor;
	struct tray_color_rule rule;
};
static GArray *chosen;
static char *icons[4];
static enum tray_color_level level;
static bool dim;
static guint blink_timer;
static gint64 fresh_until;

enum tray_color_level tray_color_value(const struct tray_color_rule *rule, double value)
{
	if (!rule->enabled || !isfinite(value) || value == UNKNOWN_DBL_VALUE)
		return TRAY_NORMAL;
	return value > rule->hot ? TRAY_CRITICAL
		: value >= rule->warm ? TRAY_WARM : TRAY_NORMAL;
}

const char *tray_color_icon(enum tray_color_level color, bool subdued)
{
	const char *files[] = {"psensor_normal.png", "psensor_warm.png",
		"psensor_hot.png", "psensor_hot_dim.png"};
	int index = color == TRAY_CRITICAL ? (subdued ? 3 : 2) : color;
	if (!icons[index]) {
		const char *override = g_getenv("PSENSOR_ANALYSIS_DIR");
		if (override && *override)
			icons[index] = g_build_filename(override, "icons", files[index], NULL);
		else {
			icons[index] = g_build_filename(DATADIR, "psensor-analysis", "icons", files[index], NULL);
			if (!g_file_test(icons[index], G_FILE_TEST_IS_REGULAR)) {
				g_free(icons[index]);
				icons[index] = g_build_filename(g_get_user_data_dir(), "psensor-analysis", "icons", files[index], NULL);
			}
		}
	}
	return icons[index];
}

void tray_color_render(void)
{
	const char *descriptions[] = {"Psensor", "Psensor: высокая температура",
		"Psensor: предупреждение датчика", "Psensor: превышен критический порог температуры"};
	const char *icon = tray_color_icon(level, dim);
	ui_appindicator_set_tray_icon(icon, descriptions[level], level == TRAY_HOT);
	if (is_status_supported())
		ui_status_set_tray_icon(icon, descriptions[level]);
}

static gboolean blink(gpointer data)
{
	/* The timer never polls sensors or takes their worker mutex. */
	if (g_get_monotonic_time() >= fresh_until) {
		blink_timer = 0;
		dim = false;
		tray_color_render(); /* Keep the last red warning, without stale animation. */
		return G_SOURCE_REMOVE;
	}
	dim = !dim;
	tray_color_render();
	return G_SOURCE_CONTINUE;
}

void tray_color_reload(struct ui_psensor *ui)
{
	if (!chosen)
		chosen = g_array_new(FALSE, FALSE, sizeof(struct colored_sensor));
	g_array_set_size(chosen, 0);
	for (struct psensor **p = ui->sensors; *p; p++) {
		if (!((*p)->type & SENSOR_TYPE_TEMP))
			continue;
		struct colored_sensor item = {.sensor = *p};
		config_get_tray_color((*p)->id, &item.rule);
		if (item.rule.enabled)
			g_array_append_val(chosen, item);
	}
	ui->indicated_generation = G_MAXUINT64;
}

/* Called with the sensor mutex held, once per new sample or preferences edit. */
void tray_color_sample(struct ui_psensor *ui, bool attention)
{
	enum tray_color_level next = attention ? TRAY_HOT : TRAY_NORMAL;
	gint64 real = g_get_real_time(), now = g_get_monotonic_time();
	gint64 gap = (gint64)MAX(4, ui->config->sensor_update_interval * 3) * G_USEC_PER_SEC;
	gint64 deadline = now;
	if (chosen)
		for (guint i = 0; i < chosen->len; i++) {
			struct colored_sensor *item = &g_array_index(chosen, struct colored_sensor, i);
			struct measure *m = psensor_get_current_measure(item->sensor);
			gint64 age = real - ((gint64)m->time.tv_sec * G_USEC_PER_SEC + m->time.tv_usec);
			if (age < 0 || age > gap)
				continue;
			enum tray_color_level value = tray_color_value(&item->rule, m->value);
			next = MAX(next, value);
			if (value == TRAY_CRITICAL)
				deadline = MAX(deadline, now + gap - age);
		}
	if (next != level) {
		level = next;
		dim = false;
	}
	fresh_until = deadline;
	if (next == TRAY_CRITICAL && !blink_timer) {
		dim = false;
		blink_timer = g_timeout_add(1000, blink, NULL);
	} else if (next != TRAY_CRITICAL && blink_timer) {
		g_source_remove(blink_timer);
		blink_timer = 0;
	}
	tray_color_render();
}

void tray_color_cleanup(void)
{
	if (blink_timer) g_source_remove(blink_timer);
	blink_timer = 0;
	level = TRAY_NORMAL;
	dim = false;
	fresh_until = 0;
	if (chosen) g_array_free(chosen, TRUE);
	chosen = NULL;
	for (guint i = 0; i < G_N_ELEMENTS(icons); i++)
		g_clear_pointer(&icons[i], g_free);
}

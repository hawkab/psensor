/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <cooling_policy.h>
#include <action_notify.h>
#include <plog.h>
#include <math.h>

/* No samples, processes or history are stored: only four monotonic clocks. */
enum cooling_profile cooling_decide(struct cooling_state *s, const struct cooling_rule *r,
		double value, enum cooling_profile current, gint64 now, gint64 gap,
		enum cooling_reason *reason)
{
	*reason = COOL_NORMAL;
	if (!r->enabled || !isfinite(value) || value == UNKNOWN_DBL_VALUE || current == COOL_NONE) {
		s->warm_since = s->hot_since = 0;
		s->last = 0;
		return COOL_NONE; /* Never restore performance from stale data. */
	}
	if (s->last && now - s->last > gap)
		s->warm_since = s->hot_since = 0;
	s->last = now;
	if (value >= r->warm) {
		if (!s->warm_since)
			s->warm_since = now;
	} else
		s->warm_since = 0;
	if (value > r->hot) {
		if (!s->hot_since) s->hot_since = now;
	} else
		s->hot_since = 0;
	if (s->hot_since && now - s->hot_since >= (gint64)r->hot_seconds * G_USEC_PER_SEC) {
		*reason = COOL_HOT;
		return COOL_SAVER;
	}
	if (s->hold != COOL_NONE && (now < s->hold_until ||
	    (s->hold == COOL_SAVER && value > r->hot))) {
		*reason = COOL_HOLD;
		return s->hold;
	}
	if (s->hold != COOL_NONE) {
		if (value >= r->warm) {
			*reason = s->hold == COOL_BALANCED ? COOL_HOLD : COOL_WARM;
			return COOL_BALANCED;
		}
		s->hold = COOL_NONE;
		s->hold_until = 0;
	}
	if (s->warm_since && now - s->warm_since >= (gint64)r->warm_seconds * G_USEC_PER_SEC) {
		*reason = COOL_WARM;
		/* Do not promote a manually selected power-saver profile. */
		return current == COOL_SAVER ? COOL_NONE : COOL_BALANCED;
	}
	return value < r->warm && r->restore_performance ? COOL_PERFORMANCE : COOL_NONE;
}

void cooling_applied(struct cooling_state *s, const struct cooling_rule *r,
		enum cooling_profile profile, enum cooling_reason reason, gint64 now)
{
	if (reason == COOL_HOLD)
		return;
	if (profile == COOL_PERFORMANCE) {
		s->hold = COOL_NONE;
		s->hold_until = 0;
	} else if (s->hold != profile) {
		s->hold = profile;
		s->hold_until = now + (gint64)(profile == COOL_SAVER ? r->saver_seconds : r->balanced_seconds) * G_USEC_PER_SEC;
	}
}

#define PPD_NAME "org.freedesktop.UPower.PowerProfiles"
#define PPD_PATH "/org/freedesktop/UPower/PowerProfiles"
static struct psensor *sensor;
static struct cooling_rule rule;
static struct cooling_state clocks;
static GDBusProxy *proxy;
static bool connecting, busy;
static guint generation;
static gint64 retry_at;
static enum cooling_profile retry_profile;
static char *status;
static gint64 sample_gap = 4 * G_USEC_PER_SEC;
static const char *profiles[] = {"", "performance", "balanced", "power-saver"};
static const char *titles[] = {"", "Производительность", "Сбалансированный", "Энергосбережение"};

struct transition {
	struct cooling_rule rule;
	enum cooling_profile target;
	enum cooling_reason reason;
	guint generation, timeout;
	GSubprocess *child;
	char *id, *condition, *command;
};

static void set_status(const char *message)
{
	if (g_strcmp0(status, message) == 0) return;
	g_free(status);
	status = g_strdup(message);
}

const char *cooling_policy_status(const char *id)
{
	return sensor && g_strcmp0(sensor->id, id) == 0 && status
	    ? status : "Автоматическое охлаждение выключено";
}

static enum cooling_profile active_profile(void)
{
	GVariant *value = proxy ? g_dbus_proxy_get_cached_property(proxy, "ActiveProfile") : NULL;
	enum cooling_profile result = COOL_NONE;
	if (value && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
		for (int i = 1; i <= COOL_SAVER; i++)
			if (g_str_equal(g_variant_get_string(value, NULL), profiles[i]))
				result = i;
	}
	if (value) g_variant_unref(value);
	return result;
}

static void proxy_ready(GObject *object, GAsyncResult *result, gpointer data)
{
	GError *error = NULL;
	proxy = g_dbus_proxy_new_for_bus_finish(result, &error);
	connecting = false;
	if (!proxy) {
		set_status(error->message);
		g_error_free(error);
		retry_at = g_get_monotonic_time() + 30 * G_USEC_PER_SEC;
	} else if (!sensor)
		g_clear_object(&proxy);
}

bool cooling_policy_reload(struct ui_psensor *ui)
{
	struct psensor *next = NULL;
	struct cooling_rule next_rule = {0};
	sample_gap = (gint64)MAX(4, ui->config->sensor_update_interval * 3) * G_USEC_PER_SEC;
	for (struct psensor **p = ui->sensors; *p; p++) {
		if (!((*p)->type & SENSOR_TYPE_TEMP))
			continue;
		config_get_cooling_rule((*p)->id, &next_rule);
		if (next_rule.enabled) {
			next = *p;
			break;
		}
	}
	/* Preserve timers when unrelated command rules are edited. */
	if (next == sensor && (!next || (rule.warm == next_rule.warm && rule.hot == next_rule.hot &&
	    rule.restore_performance == next_rule.restore_performance &&
	    rule.hot_seconds == next_rule.hot_seconds && rule.warm_seconds == next_rule.warm_seconds &&
	    rule.balanced_seconds == next_rule.balanced_seconds && rule.saver_seconds == next_rule.saver_seconds)))
		return sensor != NULL;
	sensor = next;
	rule = next_rule;
	clocks = (struct cooling_state){0};
	generation++;
	retry_at = 0;
	set_status(sensor ? "Ожидание свежего измерения и режима питания" : "Автоматическое охлаждение выключено");
	if (!sensor && !busy)
		g_clear_object(&proxy);
	return sensor != NULL;
}

static void completed(struct transition *t, bool success, const char *message)
{
	if (t->timeout)
		g_source_remove(t->timeout);
	if (generation == t->generation) {
		busy = false;
		if (success) {
			cooling_applied(&clocks, &t->rule, t->target, t->reason, g_get_monotonic_time());
			retry_at = 0;
		} else {
			retry_at = g_get_monotonic_time() + 60 * G_USEC_PER_SEC;
			retry_profile = t->target;
		}
		set_status(message);
	} else
		busy = false;
	log_warn("Cooling profile %s: %s", profiles[t->target], message);
	sensor_action_notify(t->id, t->condition, t->command, message, success, TRUE);
	g_clear_object(&t->child);
	g_free(t->id);
	g_free(t->condition);
	g_free(t->command);
	g_free(t);
}

static void verified(GObject *object, GAsyncResult *result, gpointer data)
{
	struct transition *t = data;
	GError *error = NULL;
	GVariant *reply = g_dbus_proxy_call_finish(G_DBUS_PROXY(object), result, &error);
	if (!reply) {
		completed(t, false, error->message);
		g_error_free(error);
		return;
	}
	GVariant *boxed, *value;
	g_variant_get(reply, "(@v)", &boxed);
	value = g_variant_get_variant(boxed);
	bool matches = g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) &&
		g_str_equal(g_variant_get_string(value, NULL), profiles[t->target]);
	if (matches)
		g_dbus_proxy_set_cached_property(G_DBUS_PROXY(object), "ActiveProfile", value);
	completed(t, matches, matches ? "Режим подтверждён ОС" : "ОС не подтвердила выбранный режим; повтор через минуту");
	g_variant_unref(value);
	g_variant_unref(boxed);
	g_variant_unref(reply);
}

static gboolean command_timeout(gpointer data)
{
	struct transition *t = data;
	t->timeout = 0;
	g_subprocess_force_exit(t->child);
	return G_SOURCE_REMOVE;
}

static void command_finished(GObject *object, GAsyncResult *result, gpointer data)
{
	struct transition *t = data;
	GError *error = NULL;
	char *output = NULL;
	bool ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(object), result, NULL, &output, &error);
	if (t->timeout) {
		g_source_remove(t->timeout);
		t->timeout = 0;
	}
	if (!ok || !g_subprocess_get_successful(t->child))
		completed(t, false, error ? error->message : output && *output ? output : "Команда не выполнилась; повтор через минуту");
	else
		g_dbus_proxy_call(proxy, "org.freedesktop.DBus.Properties.Get",
			g_variant_new("(ss)", PPD_NAME, "ActiveProfile"), G_DBUS_CALL_FLAGS_NONE,
			5000, NULL, verified, t);
	g_clear_error(&error);
	g_free(output);
}

void cooling_policy_tick(void)
{
	if (!sensor || busy)
		return;
	gint64 now = g_get_monotonic_time();
	if (!proxy) {
		if (!connecting && now >= retry_at) {
			connecting = true;
			g_dbus_proxy_new_for_bus(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
				NULL, PPD_NAME, PPD_PATH, PPD_NAME, NULL, proxy_ready, NULL);
		}
		return;
	}
	struct measure *m = psensor_get_current_measure(sensor);
	gint64 age = g_get_real_time() - ((gint64)m->time.tv_sec * G_USEC_PER_SEC + m->time.tv_usec);
	const gint64 gap = sample_gap;
	double value = age >= 0 && age <= gap ? m->value : UNKNOWN_DBL_VALUE;
	enum cooling_profile current = active_profile();
	enum cooling_reason reason;
	enum cooling_profile target = cooling_decide(&clocks, &rule, value, current, now, gap, &reason);
	if (target == COOL_NONE) {
		set_status(value == UNKNOWN_DBL_VALUE || !isfinite(value)
			? "Нет свежего измерения — возврат производительности приостановлен"
			: current == COOL_NONE ? "Системная служба режимов питания недоступна" : titles[current]);
		return;
	}
	if (current == target) {
		cooling_applied(&clocks, &rule, target, reason, now);
		char *message = clocks.hold_until > now
			? g_strdup_printf("%s · осталось %02d:%02d", titles[target],
				(int)((clocks.hold_until - now + G_USEC_PER_SEC - 1) / G_USEC_PER_SEC) / 60,
				(int)((clocks.hold_until - now + G_USEC_PER_SEC - 1) / G_USEC_PER_SEC) % 60)
			: g_strdup(titles[target]);
		set_status(message);
		g_free(message);
		return;
	}
	if (now < retry_at && target == retry_profile)
		return;
	struct transition *t = g_new0(struct transition, 1);
	t->rule = rule;
	t->generation = generation;
	t->target = target;
	t->reason = reason;
	t->id = g_strdup(sensor->id);
	t->command = g_strdup_printf("powerprofilesctl set %s", profiles[target]);
	const char *why = reason == COOL_HOT ? "превышен верхний порог" : reason == COOL_WARM
		? "длительный нагрев" :  reason == COOL_HOLD ? "сохраняется период охлаждения" : "ЦП остыл; возврат производительности";
	if (target == COOL_PERFORMANCE)
		t->condition = g_strdup_printf("%s: %.1f °C — %s. %s", sensor->name, value, why, titles[target]);
	else
		t->condition = g_strdup_printf("%s: %.1f °C — %s. %s минимум на %d мин.", sensor->name, value,
			why, titles[target], (target == COOL_SAVER ? rule.saver_seconds : rule.balanced_seconds) / 60);
	GError *error = NULL;
	busy = true;
	t->child = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
		&error, "powerprofilesctl", "set", profiles[target], NULL);
	if (!t->child) {
		completed(t, false, error->message);
		g_error_free(error);
		return;
	}
	set_status("Переключение режима питания…");
	t->timeout = g_timeout_add_seconds(10, command_timeout, t);
	g_subprocess_communicate_utf8_async(t->child, NULL, NULL, command_finished, t);
}

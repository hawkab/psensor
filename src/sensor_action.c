/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <action_notify.h>
#include <math.h>
#include <plog.h>
#include <sensor_action.h>
#include <cooling_policy.h>
#include <sys/wait.h>

struct running_action {
	struct action_timer timer;
	GPid pid;
	gchar *status;
	gchar *id, *condition, *command;
};
static GHashTable *actions;
struct configured_action {
	struct psensor *sensor;
	struct sensor_action rule;
};
static GArray *configured;
static struct ui_psensor *action_ui;
static GSourceFunc refresh_callback;
static guint timer_id;

guint sensor_actions_timer_id(void)
{
	return timer_id;
}

void sensor_actions_reload(void)
{
	struct psensor **cur;
	if (!action_ui)
		return;
	if (!configured)
		configured =
		    g_array_new(FALSE, FALSE, sizeof(struct configured_action));
	for (guint i = 0; i < configured->len; i++)
		g_free(g_array_index(configured, struct configured_action, i)
			   .rule.command);
	g_array_set_size(configured, 0);
	for (cur = action_ui->sensors; *cur; cur++) {
		struct configured_action action = {.sensor = *cur};
		config_get_sensor_action((*cur)->id, &action.rule);
		if (action.rule.enabled && action.rule.command &&
		    *action.rule.command)
			g_array_append_val(configured, action);
		else
			g_free(action.rule.command);
	}
	bool cooling = cooling_policy_reload(action_ui);
	if (refresh_callback && (configured->len || cooling) && !timer_id)
		timer_id =
		    g_timeout_add_seconds(1, refresh_callback, action_ui);
	else if (!configured->len && !cooling && timer_id) {
		g_source_remove(timer_id);
		timer_id = 0;
	}
}

void sensor_actions_init(struct ui_psensor *ui, GSourceFunc callback)
{
	action_ui = ui;
	refresh_callback = callback;
	sensor_actions_reload();
}

/* Times are monotonic microseconds. Missing samples break the duration. */
bool action_timer_step(struct action_timer *state,
		       const struct sensor_action *rule, double value,
		       gint64 now, gint64 max_gap)
{
	bool hit, recovered;
	if (!rule->enabled || !isfinite(value) || value == UNKNOWN_DBL_VALUE) {
		state->since = 0;
		state->last = 0;
		return false;
	}
	if (state->last && now - state->last > max_gap)
		state->since = 0;
	state->last = now;
	hit = rule->below ? value <= rule->threshold : value >= rule->threshold;
	recovered =
	    !hit && (rule->below ? value >= rule->threshold + rule->hysteresis
				 : value <= rule->threshold - rule->hysteresis);
	if (recovered)
		state->fired = false;
	if (!hit) {
		state->since = 0;
		return false;
	}
	if (state->fired)
		return false;
	if (!state->since)
		state->since = now;
	if (now - state->since < (gint64)rule->seconds * G_USEC_PER_SEC)
		return false;
	state->fired = true;
	return true;
}

static struct running_action *get_action(const char *id)
{
	struct running_action *state;
	if (!actions)
		actions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
						NULL);
	state = g_hash_table_lookup(actions, id);
	if (!state) {
		state = g_new0(struct running_action, 1);
		state->status = g_strdup("Правило ещё не срабатывало");
		g_hash_table_insert(actions, g_strdup(id), state);
	}
	return state;
}

void sensor_action_reset(const char *id)
{
	struct running_action *state = get_action(id);
	state->timer = (struct action_timer){0};
	g_free(state->status);
	state->status = g_strdup("Правило сохранено; ожидание условия");
	sensor_actions_reload();
}

const char *sensor_action_status(const char *id)
{
	return get_action(id)->status;
}

static void finished(GPid pid, gint status, gpointer data)
{
	struct running_action *state = data;
	state->pid = 0;
	g_free(state->status);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		state->status = g_strdup("Команда выполнена успешно");
	else if (WIFEXITED(status))
		state->status = g_strdup_printf(
		    "Команда завершилась с кодом %d", WEXITSTATUS(status));
	else
		state->status = g_strdup_printf("Команда прервана сигналом %d",
						WTERMSIG(status));
	log_warn("Sensor action result: %s", state->status);
	sensor_action_notify(
	    state->id, state->condition, state->command, state->status,
	    WIFEXITED(status) && WEXITSTATUS(status) == 0, TRUE);
	g_spawn_close_pid(pid);
}

void sensor_actions_tick(struct ui_psensor *ui)
{
	cooling_policy_tick();
	gint64 now = g_get_monotonic_time();
	gint64 gap = (gint64)MAX(4, ui->config->sensor_update_interval * 3) *
		     G_USEC_PER_SEC;
	if (!configured)
		return;
	for (guint i = 0; i < configured->len; i++) {
		struct configured_action *action =
		    &g_array_index(configured, struct configured_action, i);
		struct psensor *sensor = action->sensor;
		const struct sensor_action rule = action->rule;
		struct running_action *state;
		struct measure *measure;
		double value;
		state = get_action(sensor->id);
		measure = psensor_get_current_measure(sensor);
		value = measure->value;
		if (g_get_real_time() -
			((gint64)measure->time.tv_sec * G_USEC_PER_SEC +
			 measure->time.tv_usec) >
		    gap)
			value = UNKNOWN_DBL_VALUE;
		if (state->pid) {
			/* Observe recovery during a running command, but do not
			 * queue jobs. */
			bool hit = rule.below ? value <= rule.threshold
					      : value >= rule.threshold;
			if (!hit || value == UNKNOWN_DBL_VALUE)
				action_timer_step(&state->timer, &rule, value,
						  now, gap);
			state->timer.since = 0;
		} else if (!rule.command || !*rule.command) {
			state->timer.since = 0;
		} else if (action_timer_step(&state->timer, &rule, value, now,
					     gap)) {
			gchar *args[] = {"/bin/sh", "-c", rule.command, NULL};
			gchar **env = g_get_environ();
			gchar number[G_ASCII_DTOSTR_BUF_SIZE],
			    threshold[G_ASCII_DTOSTR_BUF_SIZE];
			GError *error = NULL;
			g_ascii_dtostr(number, sizeof(number), value);
			g_ascii_dtostr(threshold, sizeof(threshold),
				       rule.threshold);
			env = g_environ_setenv(env, "PSENSOR_SENSOR_ID",
					       sensor->id, TRUE);
			env = g_environ_setenv(env, "PSENSOR_SENSOR_NAME",
					       sensor->name, TRUE);
			env = g_environ_setenv(env, "PSENSOR_VALUE", number,
					       TRUE);
			env = g_environ_setenv(env, "PSENSOR_THRESHOLD",
					       threshold, TRUE);
			g_free(state->id);
			g_free(state->condition);
			g_free(state->command);
			state->id = g_strdup(sensor->id);
			state->command = g_strdup(rule.command);
			state->condition = g_strdup_printf(
			    "%s: %s %g %s в течение %u с.", sensor->name,
			    rule.below ? "≤" : "≥", rule.threshold,
			    psensor_type_to_unit_str(sensor->type, TRUE),
			    rule.seconds);
			g_free(state->status);
			if (g_spawn_async(NULL, args, env,
					  G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
					  &state->pid, &error)) {
				state->status = g_strdup("Команда выполняется");
				g_child_watch_add(state->pid, finished, state);
				log_warn("Sensor action started: %s",
					 sensor->id);
			} else {
				state->status = g_strdup_printf(
				    "Не удалось запустить: %s", error->message);
				sensor_action_notify(
				    state->id, state->condition, state->command,
				    state->status, FALSE, FALSE);
				g_error_free(error);
			}
			g_strfreev(env);
		}
	}
}

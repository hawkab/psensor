#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <glib/gstdio.h>
#include <sensor_action.h>
#include <action_notify.h>
#include <cooling_policy.h>
bool cooling_policy_reload(struct ui_psensor *ui) { return false; }
void cooling_policy_tick(void) {}

static struct sensor_action test_rule;
static int notifications;
const char *psensor_type_to_unit_str(unsigned int type, int use_celsius) { return "°C"; }
void sensor_action_notify(const char *id, const char *condition, const char *command,
                          const char *result, gboolean success, gboolean started) {
    notifications++;
    assert(started);
    if (g_strcmp0(id,"synthetic-tctl")==0) {
        assert(success); assert(strstr(command,"sleep 2"));
        assert(strstr(condition,"85 °C в течение 1 с."));
    } else {
        assert(!success); assert(strstr(result,"кодом 7"));
    }
}
void config_get_sensor_action(const char *id, struct sensor_action *out) {
    *out = test_rule; out->command = g_strdup(test_rule.command);
}
struct measure *psensor_get_current_measure(struct psensor *s) { return s->measures; }
void log_warn(const char *format, ...) { va_list a; va_start(a, format); vfprintf(stderr, format, a); fputc('\n', stderr); va_end(a); }

static bool step(struct action_timer *timer, struct sensor_action *rule, double value, int seconds) {
    return action_timer_step(timer, rule, value, (gint64)seconds * G_USEC_PER_SEC, 4 * G_USEC_PER_SEC);
}
static void poll(void) { while (g_main_context_iteration(NULL, FALSE)); }
int main(void) {
    struct action_timer timer = {0};
    struct sensor_action rule = {.enabled=true, .threshold=85, .hysteresis=2, .seconds=30};
    assert(!step(&timer,&rule,84,10));
    for(int s=11;s<41;s++) assert(!step(&timer,&rule,86,s));
    assert(step(&timer,&rule,86,41));
    assert(!step(&timer,&rule,90,42));
    assert(!step(&timer,&rule,84,43)); /* not yet beyond hysteresis */
    assert(timer.fired);
    assert(!step(&timer,&rule,83,44)); assert(!timer.fired);
    assert(!step(&timer,&rule,86,45));
    assert(!step(&timer,&rule,84,46)); assert(!timer.since);
    assert(!step(&timer,&rule,86,47));
    assert(!step(&timer,&rule,UNKNOWN_DBL_VALUE,48)); assert(!timer.since);
    assert(!step(&timer,&rule,NAN,49)); assert(!timer.since);
    rule.seconds=3;
    assert(!step(&timer,&rule,86,50));
    assert(!step(&timer,&rule,86,60)); /* missing interval restarts timer */
    assert(!step(&timer,&rule,86,61));
    assert(!step(&timer,&rule,86,62));
    assert(step(&timer,&rule,86,63));
    rule.enabled=false; assert(!step(&timer,&rule,99,64));
    timer=(struct action_timer){0};
    rule=(struct sensor_action){.enabled=true,.below=true,.threshold=500,.hysteresis=50,.seconds=3};
    assert(!step(&timer,&rule,0,10)); /* zero RPM is valid, not unknown */
    assert(!step(&timer,&rule,0,11)); assert(!step(&timer,&rule,0,12));
    assert(step(&timer,&rule,0,13));
    assert(!step(&timer,&rule,520,14)); assert(timer.fired);
    assert(!step(&timer,&rule,550,15)); assert(!timer.fired);
    puts("PASS: sustained duration, short spike, missing/invalid samples, hysteresis, disabled rule, zero RPM");

    gchar *directory=g_dir_make_tmp("psensor-actions-test-XXXXXX",NULL);
    assert(directory);
    gchar *marker=g_build_filename(directory,"marker.txt",NULL);
    gchar *quoted=g_shell_quote(marker);
    gchar *command=g_strdup_printf("printf '%%s:%%s\\n' \"$PSENSOR_SENSOR_ID\" \"$PSENSOR_VALUE\" >> %s; sleep 2",quoted);
    g_free(quoted);
    test_rule=(struct sensor_action){.enabled=true,.threshold=85,.hysteresis=2,.seconds=1,
        .command=command};
    struct config cfg={.sensor_update_interval=1};
    struct measure measure={.value=90};
    struct psensor sensor={.id="synthetic-tctl",.name="Test",.measures=&measure};
    struct psensor *sensors[]={&sensor,NULL};
    struct ui_psensor ui={.config=&cfg,.sensors=sensors};
    sensor_actions_init(&ui,NULL);
    gettimeofday(&measure.time,NULL); sensor_actions_tick(&ui);
    g_usleep(1100000);
    gettimeofday(&measure.time,NULL);
    gint64 before=g_get_monotonic_time(); sensor_actions_tick(&ui);
    assert(g_get_monotonic_time()-before < 500000); /* command is asynchronous */
    assert(notifications==0); /* toast only after the child finishes */
    test_rule.command="false"; test_rule.threshold=99; /* edits do not alter the pending event */
    sensor_actions_reload();
    for(int n=0;n<12;n++) { g_usleep(250000); poll(); gettimeofday(&measure.time,NULL); sensor_actions_tick(&ui); }
    char *content=NULL; gsize size=0;
    assert(g_file_get_contents(marker,&content,&size,NULL));
    assert(g_strcmp0(content,"synthetic-tctl:90\n")==0); g_free(content);
    assert(g_strcmp0(sensor_action_status("synthetic-tctl"),"Команда выполнена успешно")==0);
    assert(notifications==1);
    test_rule.command="exit 7";test_rule.threshold=85;sensor.id="synthetic-failure";
    sensor_actions_reload();
    gettimeofday(&measure.time,NULL);sensor_actions_tick(&ui);g_usleep(1100000);
    gettimeofday(&measure.time,NULL);sensor_actions_tick(&ui);
    for(int n=0;n<8;n++) {g_usleep(100000);poll();}
    assert(notifications==2);
    test_rule.enabled=false;sensor_actions_reload();
    assert(sensor_actions_timer_id()==0);
    puts("PASS: async command executes once; success/error toasts follow completion and preserve the triggering rule");
    g_remove(marker);g_rmdir(directory);g_free(marker);g_free(directory);g_free(command);
    return 0;
}

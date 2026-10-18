/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdarg.h>
#include "../../src/cfg.c"
void log_warn(const char *format, ...) {}
void log_err(const char *format, ...) {}
void _log(const char *function, const char *format, ...) {}
char *path_append(const char *dir, const char *path) {assert(0);return NULL;}
int main(void) {
    key_file=g_key_file_new();
    struct cooling_rule r,read;
    config_get_cooling_rule("cpu",&r);
    assert(!r.enabled&&r.restore_performance&&r.warm==90&&r.hot==95&&r.hot_seconds==0);
    assert(r.warm_seconds==300&&r.balanced_seconds==600&&r.saver_seconds==600);
    struct sensor_action action={.enabled=true,.threshold=85,.seconds=30,.command="true"};
    config_set_sensor_action("cpu",&action);
    r.enabled=true;config_set_cooling_rule("cpu",&r);
    config_get_cooling_rule("cpu",&read);assert(read.enabled);
    struct sensor_action saved;config_get_sensor_action("cpu",&saved);assert(!saved.enabled);g_free(saved.command);
    config_set_cooling_rule("other-cpu",&r);config_get_cooling_rule("cpu",&read);assert(!read.enabled);
    config_get_cooling_rule("other-cpu",&read);assert(read.enabled);
    config_set_sensor_action("other-cpu",&action);config_get_cooling_rule("other-cpu",&read);assert(!read.enabled);
    r.hot=80;config_set_cooling_rule("cpu",&r);config_get_cooling_rule("cpu",&read);assert(!read.enabled);
    r.hot=NAN;config_set_cooling_rule("cpu",&r);config_get_cooling_rule("cpu",&read);assert(!read.enabled);
    r.hot=95;r.hot_seconds=-7;config_set_cooling_rule("cpu",&r);config_get_cooling_rule("cpu",&read);assert(read.hot_seconds==0);
    gchar *serialized=g_key_file_to_data(key_file,NULL,NULL);g_key_file_unref(key_file);key_file=g_key_file_new();
    assert(g_key_file_load_from_data(key_file,serialized,-1,0,NULL));
    config_get_cooling_rule("cpu",&read);assert(read.enabled&&read.warm==90&&read.hot==95&&read.balanced_seconds==600);
    g_free(serialized);g_key_file_unref(key_file);key_file=NULL;
    puts("PASS: real config roundtrip, immediate >95 default, one controlling sensor, mutually exclusive command, malformed thresholds, duration validation");
}

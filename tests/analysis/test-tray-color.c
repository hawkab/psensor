/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#define DATADIR "/tmp"
#include "../../src/tray_color.c"
static int preferences, paints;
static char *painted;
static struct tray_color_rule configured_rule={.enabled=true,.warm=90,.hot=95};
void config_get_tray_color(const char *id, struct tray_color_rule *out) {preferences++;*out=configured_rule;}
struct measure *psensor_get_current_measure(struct psensor *s) {return s->measures;}
void ui_appindicator_set_tray_icon(const char *path,const char *description,bool attention) {
    assert(attention==(level==TRAY_HOT));
    paints++;g_free(painted);painted=g_strdup(path);assert(description&&*description);
}
int is_status_supported(void) {return false;}
void ui_status_set_tray_icon(const char *path,const char *description) {assert(0);}
static void poll(void) {while(g_main_context_iteration(NULL,FALSE));}
int main(void) {
    assert(tray_color_value(&configured_rule,89.99)==TRAY_NORMAL);
    assert(tray_color_value(&configured_rule,90)==TRAY_WARM);
    assert(tray_color_value(&configured_rule,95)==TRAY_WARM);
    assert(tray_color_value(&configured_rule,95.01)==TRAY_CRITICAL);
    assert(tray_color_value(&configured_rule,NAN)==TRAY_NORMAL);
    assert(tray_color_value(&configured_rule,INFINITY)==TRAY_NORMAL);
    assert(tray_color_value(&configured_rule,UNKNOWN_DBL_VALUE)==TRAY_NORMAL);
    struct config config={.sensor_update_interval=1};
    struct measure m={.value=89};gettimeofday(&m.time,NULL);
    struct psensor cpu={.id="cpu",.type=SENSOR_TYPE_TEMP,.measures=&m};
    struct psensor fan={.id="fan",.type=SENSOR_TYPE_RPM};
    struct psensor *sensors[]={&cpu,&fan,NULL};struct ui_psensor ui={.config=&config,.sensors=sensors};
    tray_color_reload(&ui);assert(preferences==1);
    for(int i=0;i<10000;i++)tray_color_sample(&ui,false);
    assert(!blink_timer&&preferences==1&&strstr(painted,"psensor_normal.png"));
    m.value=90;tray_color_sample(&ui,false);assert(!blink_timer&&strstr(painted,"psensor_warm.png"));
    m.value=95;tray_color_sample(&ui,false);assert(!blink_timer&&level==TRAY_WARM);
    m.value=96;tray_color_sample(&ui,false);guint timer=blink_timer;
    assert(timer&&strstr(painted,"psensor_hot.png"));
    for(int i=0;i<100;i++)tray_color_sample(&ui,false);assert(blink_timer==timer&&!dim);
    gint64 until=g_get_monotonic_time()+1300000;
    while(!dim&&g_get_monotonic_time()<until){poll();g_usleep(1000);}
    assert(dim&&strstr(painted,"psensor_hot_dim.png"));
    tray_color_sample(&ui,false);assert(dim&&blink_timer==timer); /* new sample preserves blink phase */
    m.value=95;tray_color_sample(&ui,false);assert(!blink_timer&&!dim&&level==TRAY_WARM);
    tray_color_sample(&ui,true);assert(level==TRAY_HOT&&!blink_timer); /* existing alarms retain their red icon */
    m.value=96;tray_color_sample(&ui,false);assert(blink_timer);
    fresh_until=g_get_monotonic_time()-1;
    until=g_get_monotonic_time()+1300000;
    while(blink_timer&&g_get_monotonic_time()<until){poll();g_usleep(1000);}
    assert(!blink_timer&&!dim&&strstr(painted,"psensor_hot.png")); /* stale reading stops animation */
    m.time.tv_sec-=20;tray_color_sample(&ui,false);assert(!blink_timer&&level==TRAY_NORMAL);
    gettimeofday(&m.time,NULL);m.value=96;tray_color_sample(&ui,false);assert(blink_timer);
    configured_rule.enabled=false;tray_color_reload(&ui);tray_color_sample(&ui,false);
    assert(!blink_timer&&level==TRAY_NORMAL&&preferences==2);
    tray_color_cleanup();assert(!chosen&&!blink_timer);g_free(painted);
    puts("PASS: exact 90/95 boundaries, real GLib blink cycle, stable phase and tray slot, no animation outside critical, stale reading timeout, disabled rule, cached preferences, existing alarms");
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#define DATADIR "/tmp"
#define PACKAGE_DATA_DIR "/tmp"
#include "../../src/ui_appindicator.c"
#include "../../src/tray_color.c"
static GMainLoop *loop;
static gint64 began;
static struct measure measure;
void config_get_tray_color(const char *id,struct tray_color_rule *rule) {
    *rule=(struct tray_color_rule){.enabled=true,.warm=90,.hot=95};
}
struct measure *psensor_get_current_measure(struct psensor *sensor) {return &measure;}
int is_status_supported(void) {return false;}
void log_debug(const char *format, ...) {}
GtkStatusIcon *ui_status_get_icon(struct ui_psensor *ui) {assert(0);return NULL;}
void ui_status_set_visible(unsigned int visible) {assert(0);}
void ui_status_set_tray_icon(const char *path,const char *description) {assert(0);}
static gboolean sample(gpointer data) {
    gint64 elapsed=g_get_monotonic_time()-began;
    measure.value=elapsed<1500000?90:elapsed<6500000?96:elapsed<8000000?95:89;
    gettimeofday(&measure.time,NULL);tray_color_sample(data,false);
    if(elapsed>9500000){g_main_loop_quit(loop);return G_SOURCE_REMOVE;}
    return G_SOURCE_CONTINUE;
}
int main(int argc,char **argv) {
    gtk_init(&argc,&argv);
    indicator=app_indicator_new("psensor-color-test",tray_color_icon(TRAY_NORMAL,false),APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    GtkWidget *menu=gtk_menu_new();app_indicator_set_menu(indicator,GTK_MENU(menu));
    app_indicator_set_label(indicator,"CPU 90°C","CPU 999°C");
    app_indicator_set_status(indicator,APP_INDICATOR_STATUS_ACTIVE);
    ui_appindicator_set_tray_icon(tray_color_icon(TRAY_HOT,false),"Alarm",true);
    assert(app_indicator_get_status(indicator)==APP_INDICATOR_STATUS_ATTENTION);
    ui_appindicator_set_tray_icon(tray_color_icon(TRAY_NORMAL,false),"Psensor",false);
    assert(app_indicator_get_status(indicator)==APP_INDICATOR_STATUS_ACTIVE);
    struct config config={.sensor_update_interval=1};
    struct psensor cpu={.id="cpu",.type=SENSOR_TYPE_TEMP};struct psensor *sensors[]={&cpu,NULL};
    struct ui_psensor ui={.config=&config,.sensors=sensors};tray_color_reload(&ui);
    began=g_get_monotonic_time();loop=g_main_loop_new(NULL,FALSE);
    sample(&ui);g_timeout_add(200,sample,&ui);g_main_loop_run(loop);
    tray_color_cleanup();g_object_unref(indicator);g_main_loop_unref(loop);
}

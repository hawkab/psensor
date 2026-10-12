#include <assert.h>
#include <stdio.h>
#define DATADIR "/tmp"
#define PACKAGE_DATA_DIR "/tmp"
#define LOCALEDIR "/tmp"
#define main unused_psensor_main
#define gtk_widget_get_mapped test_mapped
#include "../../src/main.c"
#undef main
static gboolean mapped;
static unsigned graphs,rows,indicators;
gboolean test_mapped(GtkWidget *w) {return mapped;}
GtkWidget *ui_get_graph(void) {return NULL;}
void graph_update(struct psensor **s,GtkWidget *g,struct config *c,GtkWidget *w) {graphs++;}
void ui_sensorlist_update(struct ui_psensor *u,bool complete) {rows++;}
bool is_appindicator_supported(void) {return true;}
int is_status_supported(void) {return 0;}
void ui_appindicator_update(struct ui_psensor *u,bool attention) {indicators++;}
void ui_status_update(struct ui_psensor *u,unsigned int attention) {assert(0);}
bool config_get_sensor_alarm_enabled(const char *id) {return false;}
int pmutex_unlock(pthread_mutex_t *m) {return pthread_mutex_unlock(m);}
int main(void) {
    struct psensor *sensors[]={NULL};struct config cfg={.graph_update_interval=1};
    struct ui_psensor ui={.config=&cfg,.sensors=sensors,.graph_update_interval=1};
    pthread_mutex_init(&ui.sensors_mutex,NULL);
    ui.sample_generation=1;ui_refresh_thread(&ui);
    assert(indicators==1&&graphs==0&&rows==0); /* hidden window */
    for(int n=0;n<100;n++) ui_refresh_thread(&ui);
    assert(indicators==1&&graphs==0&&rows==0); /* unchanged sample */
    mapped=TRUE;ui_refresh_thread(&ui);
    assert(graphs==1&&rows==1&&indicators==1); /* fresh view on opening */
    ui.sample_generation++;ui_refresh_thread(&ui);
    assert(graphs==2&&rows==2&&indicators==2);
    ui.indicated_generation=G_MAXUINT64;ui_refresh_thread(&ui);
    assert(graphs==3&&rows==3&&indicators==3); /* preferences invalidate display */
    pthread_mutex_lock(&ui.sensors_mutex);ui_refresh_thread(&ui);pthread_mutex_unlock(&ui.sensors_mutex);
    assert(graphs==3&&rows==3&&indicators==3);
    puts("PASS: hidden/unchanged UI does no rendering, opening refreshes current values, new samples/preferences redraw, busy sensor stays responsive");
}

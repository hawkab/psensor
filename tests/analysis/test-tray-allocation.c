#include <assert.h>
#include <malloc.h>
#include <stdio.h>
#define DATADIR "/tmp"
#define PACKAGE_DATA_DIR "/tmp"
#define app_indicator_set_label test_set_label
#include "../../src/ui_appindicator.c"
static unsigned labels, preferences;
void test_set_label(AppIndicator *i,const gchar *label,const gchar *guide) {
    assert(g_strcmp0(label,"70°C 1400RPM")==0);assert(g_strcmp0(guide,"999UUUW999UUU")==0);labels++;
}
enum temperature_unit config_get_temperature_unit(void) {return CELSIUS;}
char *psensor_current_value_to_str(const struct psensor *s,unsigned int celsius) {return g_strdup(s->type&SENSOR_TYPE_TEMP?"70°C":"1400RPM");}
int is_temp_type(unsigned int type) {return type&SENSOR_TYPE_TEMP;}
bool config_is_appindicator_label_enabled(const char *id) {preferences++;return true;}
int main(void) {
    struct psensor temp={.type=SENSOR_TYPE_TEMP},fan={.type=SENSOR_TYPE_RPM};
    struct psensor *chosen[]={&temp,&fan,NULL};label_sensors=chosen;
    for(int i=0;i<100;i++) update_label(NULL);
    size_t before=mallinfo2().uordblks;
    for(int i=0;i<100000;i++) update_label(NULL);
    size_t after=mallinfo2().uordblks;
    assert(after<=before+65536);assert(labels==100100);assert(preferences==0);
    printf("PASS: 100000 tray label updates preserve values/order, no growing heap (%zu → %zu bytes), no repeated sensor-preference scan\n",before,after);
}

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <action_tab.h>
#include <sensor_action.h>

static struct sensor_action saved={.threshold=85,.hysteresis=2,.seconds=30};
static struct psensor sensor={.id="test-tctl",.name="Tctl",.type=SENSOR_TYPE_TEMP};
static enum temperature_unit temperature_unit=CELSIUS;
static int writes, resets;
static GtkWidget *window;
static GtkEntry *entry;
static GtkToggleButton *checkbox;
static GtkButton *preset, *save;
void config_get_sensor_action(const char *id, struct sensor_action *out) { *out=saved; out->command=g_strdup(saved.command); }
void config_set_sensor_action(const char *id, const struct sensor_action *rule) {
    g_free(saved.command); saved=*rule; saved.command=g_strdup(rule->command); writes++;
}
void config_sync(void) {}
enum temperature_unit config_get_temperature_unit(void) { return temperature_unit; }
const char *psensor_type_to_unit_str(unsigned int type, int celsius) { return type&SENSOR_TYPE_TEMP ? (celsius?"°C":"°F") : "об/мин"; }
void sensor_action_reset(const char *id) { resets++; }
const char *sensor_action_status(const char *id) { return "Правило выключено"; }
static struct psensor *selected(void) { return &sensor; }
static void locate(GtkWidget *w, gpointer unused) {
    if(GTK_IS_ENTRY(w) && !GTK_IS_SPIN_BUTTON(w)) entry=GTK_ENTRY(w);
    if(GTK_IS_CHECK_BUTTON(w)) checkbox=GTK_TOGGLE_BUTTON(w);
    if(GTK_IS_BUTTON(w)) {
        const char *text=gtk_button_get_label(GTK_BUTTON(w));
        if(g_strcmp0(text,"Команда: сбалансированный режим")==0) preset=GTK_BUTTON(w);
        if(g_strcmp0(text,"Сохранить правило")==0) save=GTK_BUTTON(w);
    }
    if(GTK_IS_CONTAINER(w)) gtk_container_foreach(GTK_CONTAINER(w),locate,NULL);
}
static gboolean capture(gpointer unused) {
    gtk_widget_destroy(window);gtk_main_quit();
    return G_SOURCE_REMOVE;
}
int main(int argc, char **argv) {
    gtk_init(&argc,&argv);
    window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),"Датчик Tctl · Действие");
    gtk_window_set_default_size(GTK_WINDOW(window),570,650);
    GtkWidget *notebook=gtk_notebook_new();gtk_container_add(GTK_CONTAINER(window),notebook);
    action_tab_create(GTK_NOTEBOOK(notebook),selected);action_tab_update(&sensor);gtk_widget_show_all(window);
    locate(window,NULL);assert(entry&&checkbox&&preset&&save);
    assert(!gtk_toggle_button_get_active(checkbox));
    gtk_button_clicked(preset);assert(g_strcmp0(gtk_entry_get_text(entry),"powerprofilesctl set balanced")==0);
    gtk_toggle_button_set_active(checkbox,TRUE);gtk_button_clicked(save);
    assert(saved.enabled&&saved.seconds==30&&saved.threshold==85&&writes==1&&resets==1);
    temperature_unit=FAHRENHEIT;action_tab_update(&sensor);gtk_button_clicked(save);
    assert(fabs(saved.threshold-85)<.001&&fabs(saved.hysteresis-2)<.001);
    temperature_unit=CELSIUS;action_tab_update(&sensor);
    gtk_toggle_button_set_active(checkbox,FALSE);gtk_button_clicked(save);assert(!saved.enabled);
    puts("PASS: native action tab, explicit enable/save, preset only fills command, Fahrenheit roundtrip, disabled rule");
    g_timeout_add(1500,capture,NULL);gtk_main();g_free(saved.command);
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <tray_tab.h>
#include <tray_color.h>
static struct tray_color_rule saved={.warm=90,.hot=95};
static struct psensor sensor={.id="cpu",.type=SENSOR_TYPE_TEMP};
static enum temperature_unit units=CELSIUS;
static int writes,reloads;
static GtkToggleButton *checkbox;
static GtkSpinButton *warm,*hot;
static GtkButton *save;
static GtkWidget *window;
void config_get_tray_color(const char *id,struct tray_color_rule *r) {*r=saved;}
void config_set_tray_color(const char *id,const struct tray_color_rule *r) {saved=*r;writes++;}
void config_sync(void) {}
enum temperature_unit config_get_temperature_unit(void) {return units;}
void tray_color_reload(struct ui_psensor *ui) {reloads++;}
static struct psensor *selected(void) {return &sensor;}
static void locate(GtkWidget *w,gpointer data) {
    const char *name=gtk_widget_get_name(w);
    if(g_str_equal(name,"tray-color-enabled"))checkbox=GTK_TOGGLE_BUTTON(w);
    if(g_str_equal(name,"tray-color-warm"))warm=GTK_SPIN_BUTTON(w);
    if(g_str_equal(name,"tray-color-hot"))hot=GTK_SPIN_BUTTON(w);
    if(GTK_IS_BUTTON(w)&&g_strcmp0(gtk_button_get_label(GTK_BUTTON(w)),"Сохранить индикацию")==0)save=GTK_BUTTON(w);
    if(GTK_IS_CONTAINER(w))gtk_container_foreach(GTK_CONTAINER(w),locate,NULL);
}
static gboolean quit(gpointer data) {
    const char *file=g_getenv("PSENSOR_TEST_TRAY_SCREENSHOT");
    if(file) {
        GdkWindow *w=gtk_widget_get_window(window);
        GdkPixbuf *pixbuf=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));
        assert(pixbuf);assert(gdk_pixbuf_save(pixbuf,file,"png",NULL,NULL));g_object_unref(pixbuf);
    }
    gtk_widget_destroy(window);gtk_main_quit();return G_SOURCE_REMOVE;
}
int main(int argc,char **argv) {
    gtk_init(&argc,&argv);window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window),"Цвет значка Psensor");gtk_window_set_default_size(GTK_WINDOW(window),550,440);
    GtkWidget *grid=gtk_grid_new();gtk_container_add(GTK_CONTAINER(window),grid);
    struct ui_psensor ui={0};tray_tab_create(GTK_GRID(grid),selected,&ui);tray_tab_update(&sensor);
    gtk_widget_show_all(window);locate(window,NULL);assert(checkbox&&warm&&hot&&save);
    assert(!saved.enabled&&!writes);gtk_toggle_button_set_active(checkbox,TRUE);gtk_button_clicked(save);
    assert(saved.enabled&&saved.warm==90&&saved.hot==95&&writes==1&&reloads==1);
    gtk_spin_button_set_value(warm,100);gtk_button_clicked(save);assert(writes==1);
    units=FAHRENHEIT;tray_tab_update(&sensor);
    assert(fabs(gtk_spin_button_get_value(warm)-194)<.001&&fabs(gtk_spin_button_get_value(hot)-203)<.001);
    gtk_button_clicked(save);assert(writes==2&&fabs(saved.warm-90)<.001&&fabs(saved.hot-95)<.001);
    sensor.type=SENSOR_TYPE_RPM;tray_tab_update(&sensor);assert(!gtk_widget_is_sensitive(GTK_WIDGET(checkbox)));
    sensor.type=SENSOR_TYPE_TEMP;units=CELSIUS;tray_tab_update(&sensor);
    assert(gtk_widget_is_sensitive(GTK_WIDGET(checkbox)));
    puts("PASS: native tray settings enable/save, validation, Fahrenheit roundtrip, temperature-only controls");
    g_timeout_add(1000,quit,NULL);gtk_main();
}

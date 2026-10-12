#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <action_notify.h>
void log_warn(const char *format, ...) { va_list args;va_start(args,format);vfprintf(stderr,format,args);fputc('\n',stderr);va_end(args); }
static gboolean quit(gpointer loop) {g_main_loop_quit(loop);return G_SOURCE_REMOVE;}
int main(void) {
    const char *id="lmsensor k10temp-pci-00c3 Tctl / % & тест";
    char *uri=sensor_action_uri(id), *parsed=sensor_action_id_from_uri(uri,NULL);
    assert(g_strcmp0(id,parsed)==0);g_free(parsed);g_free(uri);
    const char *bad[]={"file:///tmp/run","psensor://sensor/abc?tab=other","psensor://user@sensor/abc?tab=action",
                       "psensor://sensor:80/abc?tab=action","psensor://sensor/?tab=action","psensor://sensor/%00?tab=action",NULL};
    for(int i=0;bad[i];i++) { GError *error=NULL;parsed=sensor_action_id_from_uri(bad[i],&error);assert(!parsed&&error);g_error_free(error); }
    puts("PASS: sensor URI roundtrip and invalid URI rejection");
    gint64 before=g_get_monotonic_time();
    sensor_action_notify(id,"Tctl: ≥ 85 °C в течение 30 с.","printf '<ok>&'","Команда выполнена успешно",TRUE,TRUE);
    sensor_action_notify(id,"Tctl: ≥ 85 °C в течение 30 с.","exit 7","Команда завершилась с кодом 7",FALSE,TRUE);
    sensor_action_notify(id,"fan1: ≤ 500 RPM в течение 10 с.","test-command","Не удалось запустить",FALSE,FALSE);
    assert(g_get_monotonic_time()-before<500000);
    GMainLoop *loop=g_main_loop_new(NULL,FALSE);g_timeout_add_seconds(7,quit,loop);g_main_loop_run(loop);g_main_loop_unref(loop);
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A private D-Bus and a disposable powerprofilesctl; never touches host profiles. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <glib/gstdio.h>
#include "../../src/cooling_policy.c"

static struct cooling_rule configured_rule = {.restore_performance=true,.warm=90,.hot=95,
    .warm_seconds=300,.hot_seconds=0,.balanced_seconds=600,.saver_seconds=600};
static char *fake_active;
static int sets, gets, notifications, successes;
static void config_get_cooling_rule_stub(const char *id, struct cooling_rule *r) { *r=configured_rule; }
void config_get_cooling_rule(const char *id, struct cooling_rule *r) {config_get_cooling_rule_stub(id,r);}
struct measure *psensor_get_current_measure(struct psensor *s) {return s->measures;}
void log_warn(const char *format, ...) {}
void sensor_action_notify(const char *id,const char *condition,const char *command,const char *message,gboolean success,gboolean started) {
    assert(g_str_equal(id,"test-cpu"));assert(started);assert(strstr(command,"powerprofilesctl set "));
    assert(strstr(condition,"°C"));notifications++;successes+=success;
}
static GVariant *get_property(GDBusConnection *c,const char *sender,const char *path,const char *interface,const char *property,GError **error,gpointer data) {
    gets++;return g_variant_new_string(fake_active);
}
static gboolean set_property(GDBusConnection *c,const char *sender,const char *path,const char *interface,const char *property,GVariant *value,GError **error,gpointer data) {
    sets++;g_free(fake_active);fake_active=g_variant_dup_string(value,NULL);
    GVariantBuilder changed,invalid;g_variant_builder_init(&changed,G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&changed,"{sv}","ActiveProfile",value);
    g_variant_builder_init(&invalid,G_VARIANT_TYPE_STRING_ARRAY);
    g_dbus_connection_emit_signal(c,NULL,PPD_PATH,"org.freedesktop.DBus.Properties","PropertiesChanged",
        g_variant_new("(s@a{sv}@as)",PPD_NAME,g_variant_builder_end(&changed),g_variant_builder_end(&invalid)),NULL);
    return TRUE;
}
static void spin(void) {for(int i=0;i<10;i++){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}}
static void until(int expected) {
    gint64 end=g_get_monotonic_time()+5*G_USEC_PER_SEC;
    while(notifications<expected && g_get_monotonic_time()<end) spin();
    assert(notifications==expected);assert(!busy);
}
int main(void) {
    GTestDBus *bus=g_test_dbus_new(G_TEST_DBUS_NONE);g_test_dbus_up(bus);
    g_setenv("DBUS_SYSTEM_BUS_ADDRESS",g_test_dbus_get_bus_address(bus),TRUE);
    GDBusConnection *connection=g_bus_get_sync(G_BUS_TYPE_SYSTEM,NULL,NULL);assert(connection);
    GVariant *reply=g_dbus_connection_call_sync(connection,"org.freedesktop.DBus","/org/freedesktop/DBus",
        "org.freedesktop.DBus","RequestName",g_variant_new("(su)",PPD_NAME,0),NULL,0,2000,NULL,NULL);assert(reply);g_variant_unref(reply);
    GDBusNodeInfo *info=g_dbus_node_info_new_for_xml("<node><interface name='org.freedesktop.UPower.PowerProfiles'><property name='ActiveProfile' type='s' access='readwrite'/></interface></node>",NULL);
    GDBusInterfaceVTable vtable={.get_property=get_property,.set_property=set_property};
    g_dbus_connection_register_object(connection,PPD_PATH,info->interfaces[0],&vtable,NULL,NULL,NULL);
    fake_active=g_strdup("balanced");
    char *dir=g_dir_make_tmp("psensor-cooling-runtime-XXXXXX",NULL);
    char *command=g_build_filename(dir,"powerprofilesctl",NULL),*flag=g_build_filename(dir,"mode",NULL);
    char *script=g_strdup_printf("#!/bin/sh\ncase \"$(cat '%s' 2>/dev/null)\" in fail) echo 'test failure' >&2;exit 7;; mismatch) exit 0;; esac\nexec /usr/bin/gdbus call --system --dest %s --object-path %s --method org.freedesktop.DBus.Properties.Set %s ActiveProfile \"<'$2'>\"\n",flag,PPD_NAME,PPD_PATH,PPD_NAME);
    assert(g_file_set_contents(command,script,-1,NULL));assert(!g_chmod(command,0700));
    char *old_path=g_strdup(g_getenv("PATH")),*path=g_strdup_printf("%s:%s",dir,old_path);g_setenv("PATH",path,TRUE);
    struct config config={.sensor_update_interval=1};struct measure m={.value=96};
    struct psensor cpu={.id="test-cpu",.name="CPU",.type=SENSOR_TYPE_TEMP,.measures=&m};
    struct psensor *sensors[]={&cpu,NULL};struct ui_psensor ui={.config=&config,.sensors=sensors};
    assert(!cooling_policy_reload(&ui));cooling_policy_tick();assert(!connecting&&!proxy&&!sets);
    configured_rule.enabled=true;assert(cooling_policy_reload(&ui));
    gettimeofday(&m.time,NULL);cooling_policy_tick();
    for(int i=0;i<300&&!proxy;i++)spin();assert(proxy);
    gint64 before=g_get_monotonic_time();cooling_policy_tick();assert(g_get_monotonic_time()-before<500000);
    until(1);assert(successes==1&&sets==1&&g_str_equal(fake_active,"power-saver"));
    gint64 hold=clocks.hold_until;int old_gets=gets;
    m.value=50;
    for(int i=0;i<100;i++){gettimeofday(&m.time,NULL);cooling_policy_tick();}
    assert(sets==1&&notifications==1&&gets==old_gets&&clocks.hold_until==hold); /* no polling or repeated commands */
    clocks.hold_until=g_get_monotonic_time()-1;cooling_policy_tick();until(2);
    assert(successes==2&&sets==2&&g_str_equal(fake_active,"performance"));
    assert(g_file_set_contents(flag,"fail",-1,NULL));m.value=96;gettimeofday(&m.time,NULL);cooling_policy_tick();until(3);
    assert(successes==2&&sets==2&&!clocks.hold);
    for(int i=0;i<100;i++)cooling_policy_tick();assert(notifications==3);
    assert(g_file_set_contents(flag,"mismatch",-1,NULL));retry_at=0;cooling_policy_tick();until(4);
    assert(successes==2&&sets==2&&!clocks.hold); /* exit 0 alone is insufficient */
    configured_rule.enabled=false;assert(!cooling_policy_reload(&ui));assert(!proxy);
    cooling_policy_tick();assert(notifications==4);
    g_setenv("PATH",old_path,TRUE);g_remove(command);g_remove(flag);g_rmdir(dir);
    g_free(path);g_free(old_path);g_free(script);g_free(command);g_free(flag);g_free(dir);g_free(fake_active);
    g_dbus_connection_close_sync(connection,NULL,NULL);g_object_unref(connection);g_dbus_node_info_unref(info);
    g_test_dbus_down(bus);g_object_unref(bus);
    puts("PASS: private PPD, async real command, verified readback, minimum hold, recovery, no background D-Bus polling/duplicates, failure backoff, mismatch rejection, lazy connection");
}

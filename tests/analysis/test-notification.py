"""Exercise the native toast through an isolated standard desktop notification service."""
import json
import re
from pathlib import Path
from datetime import datetime
import os
import tempfile
ROOT=Path(__file__).resolve().parents[2]
fixture=tempfile.TemporaryDirectory(prefix='psensor-notification-test-')
root=Path(fixture.name)
marker=root/'notification-uri.txt'
data=root/'data';config=root/'config'
(data/'applications').mkdir(parents=True);config.mkdir()
os.environ['XDG_DATA_HOME']=str(data);os.environ['XDG_CONFIG_HOME']=str(config)
launcher=root/'launch.py'
launcher.write_text("import sys\nfrom pathlib import Path\nPath("+repr(str(marker))+").write_text(sys.argv[1])\n")
(data/'applications/psensor.desktop').write_text('[Desktop Entry]\nType=Application\nName=Psensor test\nExec=/usr/bin/python3 '+str(launcher)+' %U\nMimeType=x-scheme-handler/psensor;\n')
(config/'mimeapps.list').write_text('[Default Applications]\nx-scheme-handler/psensor=psensor.desktop\n')
from gi.repository import Gio, GLib
connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
connection.call_sync('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','RequestName',
                     GLib.Variant('(su)',('org.freedesktop.Notifications',0)),None,Gio.DBusCallFlags.NONE,2000,None)
info = Gio.DBusNodeInfo.new_for_xml('''<node><interface name="org.freedesktop.Notifications">
<method name="Notify"><arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
<arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="as" direction="in"/>
<arg type="a{sv}" direction="in"/><arg type="i" direction="in"/><arg type="u" direction="out"/></method>
<signal name="ActionInvoked"><arg type="u"/><arg type="s"/></signal>
<signal name="NotificationClosed"><arg type="u"/><arg type="u"/></signal>
</interface></node>''')
messages = []
def click():
    connection.emit_signal(None,'/org/freedesktop/Notifications','org.freedesktop.Notifications',
                           'ActionInvoked', GLib.Variant('(us)',(1,'default')))
    return GLib.SOURCE_REMOVE
def method(conn, sender, path, interface, name, parameters, invocation):
    values = parameters.unpack()
    messages.append(values)
    invocation.return_value(GLib.Variant('(u)',(len(messages),)))
    if len(messages)==3: GLib.timeout_add(500,click)
connection.register_object('/org/freedesktop/Notifications',info.interfaces[0],method,None,None)
child=Gio.Subprocess.new([os.environ['PSENSOR_TEST_NOTIFICATION_BINARY']],Gio.SubprocessFlags.NONE)
loop=GLib.MainLoop()
def done(process, result):
    assert process.wait_finish(result)
    loop.quit()
child.wait_async(None,done)
GLib.timeout_add_seconds(12,lambda:(loop.quit(),False)[1])
loop.run()
assert child.get_if_exited() and child.get_exit_status()==0
assert len(messages)==3
for app, replace, icon, title, body, actions, hints, timeout in messages:
    assert app=='Psensor' and icon=='psensor' and actions==['default','Открыть датчик']
    assert hints['desktop-entry']=='psensor' and hints['urgency']==1
    assert re.search(r'\d{2}:\d{2}:\d{2} \d{2}\.\d{2}\.\d{4}$',body)
    datetime.strptime(body.splitlines()[-1],'%H:%M:%S %d.%m.%Y')
# Independent asynchronous bus connections may deliver in any order.
success=next(m for m in messages if m[3]=='Psensor: команда выполнена')
failure=next(m for m in messages if 'ошибкой' in m[3])
not_started=next(m for m in messages if 'не удалось запустить' in m[3])
assert '≥ 85 °C в течение 30 с.' in success[4]
assert '&lt;ok&gt;&amp;' in success[4]
assert 'кодом 7' in failure[4]
assert 'fan1' in not_started[4]
assert marker.read_text().strip()=='psensor://sensor/lmsensor%20k10temp-pci-00c3%20Tctl%20%2F%20%25%20%26%20%D1%82%D0%B5%D1%81%D1%82?tab=action'
fixture.cleanup()
print('PASS: standard desktop toasts, success/error/launch failure, command escaping, local timestamp, click launches sensor deep link')

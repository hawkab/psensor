"""Verify actual AppIndicator icon changes on a private tray bus and Xvfb."""
import os
from pathlib import Path
from gi.repository import Gio, GLib

ROOT=Path(__file__).resolve().parents[2]
os.environ['PSENSOR_ANALYSIS_DIR']=str(ROOT/'analysis')
bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
bus.call_sync('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','RequestName',
              GLib.Variant('(su)',('org.kde.StatusNotifierWatcher',0)),None,0,2000,None)
info=Gio.DBusNodeInfo.new_for_xml('''<node><interface name="org.kde.StatusNotifierWatcher">
<method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
<method name="RegisterStatusNotifierHost"><arg type="s" direction="in"/></method>
<property name="RegisteredStatusNotifierItems" type="as" access="read"/>
<property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
<property name="ProtocolVersion" type="i" access="read"/>
<signal name="StatusNotifierHostRegistered"/>
</interface></node>''')
snapshots=[]
errors=[]
watched=[]
def ready(connection,result):
    try:
        props=connection.call_finish(result).unpack()[0]
        snapshots.append({k:props.get(k) for k in ('IconName','Status','XAyatanaLabel')})
    except Exception as e: errors.append(str(e))
def observe(sender,path):
    bus.call(sender,path,'org.freedesktop.DBus.Properties','GetAll',
             GLib.Variant('(s)',('org.kde.StatusNotifierItem',)),None,0,2000,None,ready)
def method(connection,sender,path,interface,name,parameters,invocation):
    service=parameters.unpack()[0]
    invocation.return_value(None)
    if name=='RegisterStatusNotifierItem':
        owner,item=(sender,service) if service.startswith('/') else (service,'/StatusNotifierItem')
        if (owner,item) in watched: return
        watched.append((owner,item))
        bus.signal_subscribe(owner,'org.kde.StatusNotifierItem','NewIcon',item,None,0,
                             lambda *args:observe(owner,item))
        observe(owner,item)
def prop(connection,sender,path,interface,name):
    return GLib.Variant({'RegisteredStatusNotifierItems':'as','IsStatusNotifierHostRegistered':'b','ProtocolVersion':'i'}[name],
                        {'RegisteredStatusNotifierItems':[],'IsStatusNotifierHostRegistered':True,'ProtocolVersion':0}[name])
bus.register_object('/StatusNotifierWatcher',info.interfaces[0],method,prop,None)
child=Gio.Subprocess.new([os.environ['PSENSOR_TEST_TRAY_BINARY']],Gio.SubprocessFlags.NONE)
loop=GLib.MainLoop()
def done(process,result):
    assert process.wait_finish(result)
    loop.quit()
child.wait_async(None,done)
GLib.timeout_add_seconds(15,lambda:(loop.quit(),False)[1])
loop.run()
assert child.get_if_exited() and child.get_exit_status()==0
assert watched and not errors,(watched,errors)
assert snapshots,snapshots
assert all(s['Status']=='Active' and s['XAyatanaLabel']=='CPU 90°C' for s in snapshots),snapshots
icons=[]
for snapshot in snapshots:
    path=Path(snapshot['IconName'])
    assert path.is_file(),path
    if not icons or path.name!=icons[-1]:icons.append(path.name)
assert icons[0]=='psensor_warm.png' and icons[-2:]==['psensor_warm.png','psensor_normal.png'],icons
critical=icons[1:-2]
assert len(critical)>=4 and set(critical)=={'psensor_hot.png','psensor_hot_dim.png'},icons
assert all(a!=b for a,b in zip(critical,critical[1:])),icons
print('PASS: real AppIndicator D-Bus exposes orange at 90/95, alternating red frames above 95, normal below 90; status/slot/label stay stable:',icons)

# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
from pathlib import Path
import subprocess
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'analysis'))
from ui_fixture import setup
fixture=setup()
import gi
gi.require_version('Gtk','3.0')
from gi.repository import Gtk, Gio, GLib
from ui_common import process_link, update_rows
import process_control as control


def drain():
    while GLib.MainContext.default().iteration(False): pass


def target(child):
    raw=Path(f'/proc/{child.pid}/stat').read_text()
    return dict(pid=child.pid,start=int(raw[raw.rfind(')')+2:].split()[19]),name='Test worker',
                cpu=100,machine_cpu=3.125,rss_mb=1)


first=subprocess.Popen(['/bin/sleep','60']);second=subprocess.Popen(['/bin/sleep','60'])
try:
    process=target(first);other=target(second)
    spec=importlib.util.spec_from_file_location('load_window',ROOT/'analysis/load-window.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    app=module.App();app.audit_at=__import__('time').monotonic();app.register(None);app.activate()
    snapshot=app.window.snapshot
    snapshot['current']['processes']=[process,other]
    snapshot['recent']=[]
    app.window.facts[0].set_markup(process_link(process))
    seen=[]
    link=app.window.facts[0]
    link.connect('populate-popup',lambda widget,menu:seen.append(menu))
    link.grab_focus();drain()
    link.emit('popup-menu');drain()
    assert seen and any(item.get_label()=='Завершить процесс…' for item in seen[-1].get_children())
    seen[-1].popdown()
    class Link:
        def get_current_uri(self): return f'process:{process["pid"]}:{process["start"]}'
    menu=Gtk.Menu();app.window.process_context(Link(),menu)
    item=next(item for item in menu.get_children() if item.get_label()=='Завершить процесс…')
    assert item.get_sensitive()
    item.activate();key=(process['pid'],process['start'])
    request=control._pending[key]
    assert f'PID {first.pid}' in request.dialog.get_property('secondary-text')
    assert 'объединено процессов: 2' in request.dialog.get_property('secondary-text')
    assert control.request_termination(app.window,process,snapshot) is request
    request.dialog.response(Gtk.ResponseType.CANCEL)
    assert key not in control._pending and first.poll() is None

    # Reopening a menu captures the identity, and cancellation never signals.
    item.activate();request=control._pending[key]
    request.dialog.response(Gtk.ResponseType.ACCEPT)
    assert first.wait(timeout=3)==-15 and second.poll() is None
    assert request.result.get_property('text')=='Запрос на завершение отправлен'
    request.result.destroy()
    ended=control.append_terminate(Gtk.Menu(),app.window,process,snapshot)
    assert not ended.get_sensitive()

    # Table identities preserve two different lifetimes of one PID.
    app.window.open_details(None)
    store=app.details.recent_store
    rows=[['old',1.,1.,'Завершён',second.pid,f'{second.pid}:{other["start"]-1}'],
          ['new',2.,2.,'Работает',second.pid,f'{second.pid}:{other["start"]}']]
    update_rows(store,rows)
    assert len(store)==2
    update_rows(store,rows[::-1]);assert len(store)==2

    # An authorization cancellation is simulated; never launch a real prompt.
    calls=[]
    class Child:
        def wait_async(self,cancel,callback): GLib.idle_add(lambda:(callback(self,None),False)[1])
        def wait_finish(self,result): return True
        def get_if_exited(self): return True
        def get_exit_status(self): return 126
        def force_exit(self): pass
    def launch(argv,flags): calls.append(argv);return Child()
    with patch.object(control,'terminate',side_effect=PermissionError()), \
         patch.object(control,'helper_path',return_value='/test/helper'), \
         patch.object(control.shutil,'which',return_value='/test/pkexec'), \
         patch.object(Gio.Subprocess,'new',side_effect=launch):
        request=control.request_termination(app.window,other,snapshot)
        request.dialog.response(Gtk.ResponseType.ACCEPT)
        drain()
    assert calls==[['/test/pkexec','/test/helper',str(other['pid']),str(other['start'])]]
    assert request.result.get_property('text')=='Завершение не подтверждено'
    assert second.poll() is None
    request.result.destroy()

    # Profile controls are mocked: tests must never change the host power mode.
    profile_calls=[]
    class ProfileChild:
        def communicate_utf8_async(self,text,cancel,callback):
            GLib.idle_add(lambda:(callback(self,None),False)[1])
        def communicate_utf8_finish(self,result): return True,'',''
        def get_successful(self): return True
    def profile_launch(argv,flags): profile_calls.append(argv);return ProfileChild()
    def read_profile(callback): callback(profile_calls[-1][-1],None)
    with patch.object(Gio.Subprocess,'new',side_effect=profile_launch), \
         patch.object(app.window,'read_profile',side_effect=read_profile):
        app.window.energy.clicked();drain()
        assert not app.window.energy.get_sensitive()
        assert 'Энергосбережение' in app.window.facts[2].get_text()
        app.window.balance.clicked();drain()
        assert not app.window.balance.get_sensitive()
        assert 'Сбалансированный' in app.window.facts[2].get_text()
    assert profile_calls==[['powerprofilesctl','set','power-saver'],['powerprofilesctl','set','balanced']]
    app.details.destroy();app.window.destroy();app.reader.close()
    assert not control._pending
    print('PASS: overview context menu, explicit confirmation/cancel, single PID in a group, exact table lifetime, ended target disabled, simulated administrator cancellation; only disposable children signalled')
    print('PASS: separate power-saver/balanced controls, exact commands, immediate profile feedback; host power mode untouched')
finally:
    for child in (first,second):
        if child.poll() is None: child.kill()
        child.wait(timeout=3)
    fixture.cleanup()

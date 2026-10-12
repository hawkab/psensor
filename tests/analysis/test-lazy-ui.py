from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0, str(ROOT / 'analysis'))
from ui_fixture import setup
fixture=setup()
import importlib.util,sys,threading
from gi.repository import GLib
spec=importlib.util.spec_from_file_location('load_window',str(ROOT/'analysis/load-window.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
assert 'analysis_details' not in sys.modules and 'process_inspector' not in sys.modules
app=m.App();app.audit_at=__import__("time").monotonic();app.register(None);app.activate();loop=GLib.MainLoop();completed=False
def check():
    assert 'analysis_details' not in sys.modules and 'process_inspector' not in sys.modules
    assert 'series' not in app.window.snapshot
    assert not any(t.name.startswith('ThreadPoolExecutor') for t in threading.enumerate())
    app.window.open_details(None)
    assert 'analysis_details' in sys.modules and 'process_inspector' not in sys.modules
    assert 'series' in app.details.snapshot
    css=app.css
    app.window.destroy();app.activate()
    assert app.css is css and len(app.get_windows())==2
    p=app.window.snapshot['current']['processes'][0]
    dialog=m.Inspector(app.window,p['pid'],p['start'],app.window.snapshot)
    assert 'process_inspector' in sys.modules
    GLib.timeout_add(1800,close_inspector,dialog)
    return False
def close_inspector(dialog):
    assert not dialog.busy
    dialog.response(dialog,m.Gtk.ResponseType.APPLY)
    dialog.response(dialog,m.Gtk.ResponseType.APPLY) # repeated refresh while busy must not queue more tasks
    assert dialog.busy
    GLib.timeout_add(1800,finish,dialog)
    return False
def finish(dialog):
    assert not dialog.busy
    dialog.destroy();app.details.destroy()
    assert 'series' not in app.reader.cache
    GLib.timeout_add(500,check_closed)
    return False
def check_closed():
    global completed
    assert not any(t.name.startswith('ThreadPoolExecutor') for t in threading.enumerate())
    app.window.destroy();app.reader.close();completed=True;loop.quit()
    print('PASS: lazy Details/Inspector imports, no background inspector thread, one refresh job, worker/graph/lease cleanup')
    return False
GLib.timeout_add(1500,check);GLib.timeout_add(15000,lambda:(loop.quit(),False)[1]);loop.run()
assert completed, "GTK callbacks did not complete successfully"

fixture.cleanup()

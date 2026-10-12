from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0, str(ROOT / 'analysis'))
from ui_fixture import setup
fixture=setup()
import importlib.util,sys
from gi.repository import GLib
spec=importlib.util.spec_from_file_location('load_window',str(ROOT/'analysis/load-window.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
app=m.App();app.audit_at=__import__("time").monotonic()
def close():
    assert app.window is not None
    assert 'analysis_details' not in sys.modules and 'process_inspector' not in sys.modules
    app.window.destroy()
    return False
GLib.timeout_add(2000,close)
assert app.run(['psensor-analysis-test'])==0
assert app.reader.db is None
print('PASS: actual GTK application run/shutdown closes DB and releases viewer lease')

fixture.cleanup()
assert not (m.STATE/'viewer.json').exists()

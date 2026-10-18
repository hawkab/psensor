# SPDX-License-Identifier: GPL-2.0-or-later
import datetime
from gi.repository import Gtk, GLib

def stamp(ts):
    return datetime.datetime.fromtimestamp(ts).astimezone().strftime('%H:%M:%S')

def Inspector(parent, pid, start, snapshot):
    from process_inspector import Inspector as ProcessInspector
    return ProcessInspector(parent, pid, start, snapshot)


def process_link_menu(widget, menu, parent, snapshot):
    from process_control import populate_link_menu
    populate_link_menu(widget, menu, parent, snapshot)

def label(text='', css=None, wrap=False):
    item = Gtk.Label(label=text, xalign=0)
    item.set_line_wrap(wrap)
    if css: item.get_style_context().add_class(css)
    return item


def update_rows(store, rows):
    """Update in place so selection and the user's sorting survive refreshes."""
    existing = {row[-1]: row.iter for row in store}
    for row in rows:
        iterator = existing.pop(row[-1], None)
        if iterator is None:
            store.append(row)
        else:
            store.set(iterator, dict(enumerate(row)))
    for iterator in existing.values():
        store.remove(iterator)


def process_link(process, title=None):
    if not process: return ''
    return f'<a href="process:{process["pid"]}:{process["start"]}">{GLib.markup_escape_text(title or process["name"])}</a>'

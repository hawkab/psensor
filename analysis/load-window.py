#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Interactive GTK resource/heat overview opened from the Psensor tray."""
import datetime
import json
import os
from pathlib import Path
import sys
import time
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, Gio, GLib
from analysis_model import explain, category
from ui_common import label, process_link, Inspector
from history_store import SnapshotReader

STATE = Path(os.environ.get('XDG_STATE_HOME', str(Path.home() / '.local/state'))) / 'psensor-analysis'


def stamp(ts):
    return datetime.datetime.fromtimestamp(ts).astimezone().strftime('%H:%M:%S')


class OverviewWindow(Gtk.ApplicationWindow):
    def __init__(self, app):
        super().__init__(application=app, title='Почему греется процессор')
        self.set_resizable(False)
        self.set_default_size(580, 480)
        self.profile_pending = False
        self.feedback = ('', 0)
        self.set_icon_name('utilities-system-monitor')
        self.set_titlebar(Gtk.HeaderBar(title='Нагрев процессора', show_close_button=True))
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        outer.set_border_width(22)
        self.add(outer)
        self.status = label('Собираю данные…', 'muted')
        outer.pack_start(self.status, False, False, 0)
        hero = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=9)
        hero.get_style_context().add_class('hero')
        hero.pack_start(label('Вероятная причина', 'eyebrow'), False, False, 0)
        self.headline = label('Проверяю текущую нагрузку', 'headline', True)
        self.headline.set_max_width_chars(38)
        hero.pack_start(self.headline, False, False, 0)
        self.subtitle = label('', 'body', True)
        self.subtitle.set_max_width_chars(58)
        hero.pack_start(self.subtitle, False, False, 0)
        outer.pack_start(hero, False, False, 0)
        metrics = Gtk.Box(spacing=20)
        self.temperature = label('—', 'temperature')
        self.cpu = label('—', 'temperature')
        for title, value in [('Температура ЦП', self.temperature), ('Общая загрузка', self.cpu)]:
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3)
            box.pack_start(label(title, 'muted'), False, False, 0)
            box.pack_start(value, False, False, 0)
            metrics.pack_start(box, True, True, 0)
        outer.pack_start(metrics, False, False, 0)
        self.thermal_history = label('', 'muted', True)
        outer.pack_start(self.thermal_history, False, False, 0)
        self.facts = []
        for icon, title in [('media-playback-start-symbolic', 'Сейчас'), ('document-open-recent-symbolic', 'Недавно'), ('power-profile-performance-symbolic', 'Режим питания')]:
            row = Gtk.Box(spacing=12)
            image = Gtk.Image.new_from_icon_name(icon, Gtk.IconSize.BUTTON)
            image.set_valign(Gtk.Align.START)
            row.pack_start(image, False, False, 0)
            column = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            column.pack_start(label(title, 'section-title'), False, False, 0)
            text = label('', 'body', True)
            text.set_max_width_chars(57)
            text.connect('activate-link', self.inspect_link)
            column.pack_start(text, False, False, 0)
            row.pack_start(column, True, True, 0)
            self.facts.append(text)
            outer.pack_start(row, False, False, 0)
        self.journal = label('', 'muted', True)
        self.journal.set_max_width_chars(65)
        outer.pack_start(self.journal, False, False, 0)
        buttons = Gtk.Box(spacing=12)
        details = Gtk.Button(label='Подробнее')
        details.connect('clicked', self.open_details)
        buttons.pack_start(details, True, True, 0)
        self.balance = Gtk.Button(label='Сбалансированный режим')
        self.balance.set_tooltip_text('Переключить режим питания в balanced: нагрев может снизиться, выполнение задач — замедлиться.')
        self.balance.connect('clicked', self.set_balanced)
        buttons.pack_start(self.balance, True, True, 0)
        outer.pack_start(buttons, False, False, 0)
        self.timer = GLib.timeout_add_seconds(2, self.refresh)
        self.connect('destroy', self.destroyed)
        self.refresh()
        self.show_all()
        monitor = self.get_display().get_monitor_at_window(self.get_window())
        if monitor:
            area = monitor.get_workarea()
            self.move(area.x + max(0, area.width - 620), area.y + 28)

    def inspect_link(self, widget, uri):
        try:
            _, pid, start = uri.split(':')
            Inspector(self, int(pid), int(start), self.snapshot)
        except (ValueError, AttributeError): pass
        return True

    def open_details(self, button):
        app = self.get_application()
        if app.details is None:
            from analysis_details import DetailsWindow
            app.details = DetailsWindow(app)
            app.details.connect('destroy', lambda *_: setattr(app, 'details', None))
        app.details.present()

    def set_balanced(self, button):
        self.profile_pending = True
        self.balance.set_sensitive(False)
        try:
            process = Gio.Subprocess.new(['powerprofilesctl', 'set', 'balanced'], Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE)
            process.communicate_utf8_async(None, None, self.profile_finished)
        except GLib.Error as e:
            self.journal.set_text('Не удалось переключить режим: ' + e.message)
            self.balance.set_sensitive(True)
            self.profile_pending = False

    def profile_finished(self, process, result):
        try:
            ok, out, error = process.communicate_utf8_finish(result)
            message = 'Включён сбалансированный режим. Нагрев может снизиться; задачи могут выполняться дольше.' if process.get_successful() else 'Не удалось переключить режим: ' + error.strip()[:180]
        except GLib.Error as e: message = 'Не удалось переключить режим: ' + e.message
        if self.timer:
            self.feedback = (message, time.monotonic() + 10)
            self.journal.set_text(message)
            self.balance.set_sensitive(True)
        self.profile_pending = False

    def refresh(self):
        try:
            s = self.get_application().snapshot()
            if s is None: raise ValueError('Нет измерений')
            self.snapshot = s
            facts = explain(s)
            age = datetime.datetime.now().timestamp() - s['current']['ts']
            if age > 12:
                self.status.set_text('Замеры приостановлены · последние данные ' + stamp(s['current']['ts']))
                self.headline.set_text('Нужны свежие данные')
                self.subtitle.set_text('Сборщик временно не обновляет измерения. Последние показания доступны в подробностях.')
            else:
                self.status.set_text('Обновлено ' + stamp(s['current']['ts']) + ' · история 15 минут')
                self.headline.set_text(facts['headline'])
                self.subtitle.set_text(facts['subtitle'])
            self.temperature.set_text(f'{facts["temp"]:.0f} °C' if facts['temp'] is not None else '—')
            self.temperature.set_tooltip_text(facts['temperature'])
            span = facts['temperature_range']
            thermal_text = f'За 15 минут: {span[0]:.0f}–{span[1]:.0f} °C.' if span else 'История температуры ещё записывается.'
            if facts['chiplet_peak'] is not None: thermal_text += f' Пик на чиплетах: {facts["chiplet_peak"]:.0f} °C.'
            self.thermal_history.set_text(thermal_text)
            self.cpu.set_text(f'{facts["cpu"]:.0f}%')
            current = facts['current']
            current_text = process_link(current['process'], current['name']) if current else 'Активных задач пока не видно'
            emulators = next((g for g in facts['current_groups'] if g['name'] == 'Android-эмуляторы'), None)
            if facts['emulator_count'] and (not current or current['name'] != 'Android-эмуляторы'):
                if emulators: current_text += ' и ' + process_link(emulators['process'], f'Android-эмуляторы ({facts["emulator_count"]})')
                else: current_text += f'. Android-эмуляторов: {facts["emulator_count"]}'
            elif facts['emulator_count']:
                current_text += f' · запущено {facts["emulator_count"]}'
            current_text += '. Большой нагрузки нет.' if facts['cpu'] < 12 else '. ЦП активно работает.'
            self.facts[0].set_markup(current_text)
            recent = facts['recent']
            text = GLib.markup_escape_text(facts['recent_text'])
            if recent:
                text = text.replace(GLib.markup_escape_text(recent['name']), process_link(recent['process'], recent['name']), 1)
            self.facts[1].set_markup(text)
            profile_name = {'performance': 'Производительность', 'balanced': 'Сбалансированный', 'power-saver': 'Энергосбережение'}.get(facts['profile'], facts['profile'])
            self.facts[2].set_text('«' + profile_name + '».' + (' Позволяет ЦП работать на высоких частотах.' if facts['profile'] == 'performance' else ''))
            self.balance.set_sensitive(facts['profile'] != 'balanced' and not self.profile_pending)
            audit = facts['journal']
            self.journal.set_text(('В журнале за 2 часа есть сообщения о нагреве — см. подробнее.' if audit.get('events') else 'За 2 часа сообщений о критическом нагреве в журнале нет.') if audit.get('accessible') else 'Вывод — оценка по нагрузке и времени. Графики и точные замеры — в подробностях.')
            if time.monotonic() < self.feedback[1]: self.journal.set_text(self.feedback[0])
        except (OSError, ValueError, KeyError):
            self.status.set_text('Ожидаю первые замеры…')
        return GLib.SOURCE_CONTINUE

    def destroyed(self, widget):
        if self.timer: GLib.source_remove(self.timer)
        self.timer = 0


class App(Gtk.Application):
    def __init__(self):
        super().__init__(application_id='local.psensor.LoadAnalysis', flags=Gio.ApplicationFlags.FLAGS_NONE)
        self.window = None
        self.details = None
        self.reader = SnapshotReader()
        self.audit_at = -1e9
        self.audit_process = None
        self.audit_timeout = 0
        self.css = None

    def snapshot(self, details=False):
        result = self.reader.snapshot(details)
        if time.monotonic() - self.audit_at >= 60 and self.audit_process is None:
            self.audit_at = time.monotonic()
            pattern = 'critical.*temperature|temperature.*critical|thermal.*(shutdown|throttl|trip)|overheat|temperature above threshold|cpu.*throttl'
            try:
                self.audit_process = Gio.Subprocess.new(['journalctl','-k','--since','2 hours ago','--no-pager','-o','cat','--case-sensitive=no','--grep',pattern],Gio.SubprocessFlags.STDOUT_PIPE|Gio.SubprocessFlags.STDERR_PIPE)
                self.audit_process.communicate_utf8_async(None,None,self.audit_finished)
                self.audit_timeout = GLib.timeout_add(2000,self.audit_expired)
            except GLib.Error:
                self.reader.journal = {'accessible':False,'checked':time.time()}
                self.audit_process = None
        return result

    def audit_expired(self):
        self.audit_timeout = 0
        if self.audit_process: self.audit_process.force_exit()
        return GLib.SOURCE_REMOVE

    def audit_finished(self, process, result):
        if self.audit_timeout: GLib.source_remove(self.audit_timeout)
        self.audit_timeout = 0
        try:
            _,out,error = process.communicate_utf8_finish(result)
            denied = any(word in error.lower() for word in ('permission','not seeing messages','no journal files'))
            ok = process.get_if_exited() and (process.get_exit_status()==0 or (process.get_exit_status()==1 and not error.strip())) and not denied
            self.reader.journal = {'accessible':ok,'events':out.splitlines()[-5:],'checked':time.time()}
        except GLib.Error:
            self.reader.journal = {'accessible':False,'checked':time.time()}
        self.audit_process = None

    def do_shutdown(self):
        if self.audit_process: self.audit_process.force_exit()
        if self.audit_timeout: GLib.source_remove(self.audit_timeout)
        self.audit_timeout = 0
        self.reader.close()
        Gtk.Application.do_shutdown(self)

    def do_activate(self):
        if self.window is None:
            if self.css is None:
                self.css = Gtk.CssProvider()
                self.css.load_from_data(b'.metric-card { background: alpha(@theme_fg_color,0.06); border-radius: 10px; padding: 12px; } .hero { background: alpha(@theme_selected_bg_color,0.10); border: 1px solid alpha(@theme_selected_bg_color,0.18); border-radius: 14px; padding: 18px; } .headline { font-size: 23px; font-weight: bold; } .body { font-size: 15px; } .eyebrow { color: alpha(@theme_fg_color,0.65); font-size: 13px; } .temperature { font-size: 23px; font-weight: bold; } .big-value { font-size: 28px; font-weight: bold; } .section-title { font-weight: bold; font-size: 14px; } .muted { color: alpha(@theme_fg_color,0.65); font-size: 12px; } treeview { font-size: 11px; }')
                Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), self.css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
            self.window = OverviewWindow(self)
            self.window.connect('destroy', lambda _: setattr(self, 'window', None))
        self.window.present()


if __name__ == '__main__':
    sys.exit(App().run(sys.argv))

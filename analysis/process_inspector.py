# SPDX-License-Identifier: GPL-2.0-or-later
"""Process inspection outside GTK's event loop; termination is explicitly requested."""
import concurrent.futures
from itertools import islice
import datetime
import json
import os
from pathlib import Path
import pwd
import re
import subprocess
import time
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib
from analysis_model import category

def read(path, default=''):
    try: return Path(path).read_text().strip()
    except OSError: return default


def link(path):
    try: return os.readlink(path)
    except OSError: return 'Недоступно с текущими правами'


def inspect_process(pid, start, snapshot):
    p = Path('/proc') / str(pid)
    raw = read(p / 'stat')
    fields = raw[raw.rfind(')') + 2:].split() if raw else []
    historical = next((r for r in snapshot['recent'] if r['pid'] == pid and r['start'] == start), None)
    history_title = 'CPU за 15 минут'
    if historical is None:
        historical = next((r for r in (snapshot.get('peak') or {}).get('contributors', []) if r['pid'] == pid and r['start'] == start), None)
        history_title = 'CPU перед пиком температуры (30 с)'
    current = next((r for r in snapshot['current']['processes'] if r['pid'] == pid and r['start'] == start), None)
    if not fields or int(fields[19]) != start:
        return {'title': historical['name'] if historical else f'Процесс {pid}', 'ended': True,
                'description': 'Процесс уже завершён. Его вклад в недавнюю нагрузку сохранён.',
                'rows': [('PID', str(pid)), (history_title, f'{historical["cpu_seconds"]:.1f} с' if historical else 'Нет записи')]}
    info = {}
    for row in read(p / 'status').splitlines():
        key, _, value = row.partition(':')
        info[key] = value.strip()
    try:
        with (p / 'cmdline').open('rb') as stream:
            args = [v.decode(errors='replace') for v in stream.read(8192).split(b'\0') if v]
    except OSError: args = []
    name = (current or historical or {}).get('name', info.get('Name', f'Процесс {pid}'))
    exe, cwd = link(p / 'exe'), link(p / 'cwd')
    uid = int(info.get('Uid', '0').split()[0])
    try: user = pwd.getpwuid(uid).pw_name
    except KeyError: user = str(uid)
    state = {'R': 'Выполняется на CPU', 'S': 'Ожидает событие или работу', 'D': 'Ожидает ввод-вывод ядра',
             'T': 'Приостановлен', 'Z': 'Завершён, ожидает родителя'}.get(fields[0], fields[0])
    parent = fields[1]
    rows = [('PID · пользователь', f'{pid} · {user}'), ('Состояние', state),
            ('Потоки', info.get('Threads', '—')), ('Родитель', f'{parent} · {read(Path("/proc") / parent / "comm", "—")}')]
    cpu = current['cpu'] if current else 0
    fresh = time.time() - snapshot['current']['ts'] < 12
    rows.append(('CPU сейчас', f'{cpu / snapshot["logical_cpus"]:.2f}% мощности ЦП · {cpu:.1f}% одного потока' if fresh else 'Данные устарели'))
    rss = int(info.get('VmRSS', '0 kB').split()[0]) / 1024
    rows.extend([('Память', f'{rss:.0f} МиБ'), ('Программа', exe), ('Рабочая папка', cwd)])
    if historical: rows.append((history_title, f'{historical["cpu_seconds"]:.1f} с · пик {historical["peak"] / snapshot["logical_cpus"]:.1f}% мощности ЦП'))
    description = 'Данные ОС показывают активность и ресурсы. Точную выполняемую инструкцию или пользовательскую задачу /proc не раскрывает.'
    if name.startswith('Gradle') or any('GradleDaemon' in a or 'GradleWorkerMain' in a for a in args):
        description = 'Сервис Gradle выполняет задачи сборки: компиляцию, тесты и упаковку. Когда новых задач нет, фоновый сервис остаётся запущенным и ждёт.'
        log = Path(cwd) / f'daemon-{pid}.out.log'
        try:
            with log.open('rb') as f:
                f.seek(max(0, log.stat().st_size - 131072))
                tail = f.read().decode(errors='replace')
            dirs = re.findall(r'currentDir=([^,}\]\n]+)', tail)
            if dirs: rows.append(('Последний проект', dirs[-1].strip()))
            tasks = re.findall(r'(?:> Task |Task :)(:[\w:.-]+|[\w:.-]+)', tail)
            if tasks: rows.append(('Последняя задача в журнале', tasks[-1]))
        except OSError: pass
    elif 'qemu-system' in exe or name.startswith('Эмулятор'):
        description = 'Эмулятор Android поддерживает виртуальное устройство: исполняет Android и приложения, рисует интерфейс, обслуживает виртуальное оборудование и ADB. Фоновая работа возможна и при закрытом окне эмулятора.'
        if '-avd' in args and args.index('-avd') + 1 < len(args): rows.insert(0, ('Устройство Android', args[args.index('-avd') + 1]))
    elif name.startswith('ChatGPT'):
        description = 'Приложение ChatGPT/Codex обслуживает интерфейс и локальные задачи. Отдельный процесс может быть частью браузерного движка приложения.'
    elif category(name) == 'Компиляция C/C++':
        description = 'Компилятор преобразует исходный код C/C++ в исполняемый код. Обычно этот процесс запускается сборкой приложения или модуля.'
        sources = [a for a in args if a.endswith(('.c', '.cc', '.cpp', '.cxx')) and not a.startswith('-')]
        if sources: rows.insert(0, ('Исходный файл', sources[-1]))
    cgroup = read(p / 'cgroup')
    container = re.search(r'docker[-/]([a-f0-9]{64})', cgroup)
    if container:
        try:
            result = subprocess.run(['docker', 'inspect', '--format', '{{.Name}}|{{.Config.Image}}', container[1]],
                                    capture_output=True, text=True, timeout=1.5)
            if result.returncode == 0:
                container_name, image = result.stdout.strip().split('|', 1)
                container_name = container_name.lstrip('/')
                rows.extend([('Контейнер', container_name), ('Образ', image)])
                if 'ldap' in name.lower():
                    description = 'Служба LDAP обслуживает запросы к каталогу в контейнере ' + container_name + '. Какой именно запрос выполняется, ОС не сообщает.'
        except (OSError, ValueError, subprocess.TimeoutExpired): pass
    wait = read(p / 'wchan')
    if wait and wait != '0': rows.append(('Ожидание ядра', wait))
    opened = []
    try:
        with os.scandir(p / 'fd') as entries:
            for fd in islice(entries,256):
                target = link(fd.path)
                if target.startswith('/') and target not in opened: opened.append(target)
        if opened: rows.append(('Примеры открытых файлов', '\n'.join(opened[:6])))
    except OSError: pass
    return {'title': 'Служба LDAP' if category(name) == 'Служба LDAP' else name, 'description': description, 'rows': rows, 'ended': False}


class Inspector(Gtk.Dialog):
    def __init__(self, parent, pid, start, snapshot):
        super().__init__(title='Инспектор процесса', transient_for=parent, modal=True, use_header_bar=True, destroy_with_parent=True)
        self.add_button('Закрыть', Gtk.ResponseType.CLOSE)
        self.add_button('Обновить', Gtk.ResponseType.APPLY)
        self.stop_button = self.add_button('Завершить…', 1001)
        self.stop_button.get_style_context().add_class('destructive-action')
        self.stop_button.set_sensitive(False)
        self.set_default_size(620, 560)
        self.closed = False
        self.pool = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        self.reader = getattr(parent.get_application(), 'reader', None)
        self.busy = False
        self.pid, self.start, self.snapshot = pid, start, snapshot
        self.connect('response', self.response)
        self.connect('destroy', self.destroyed)
        area = self.get_content_area()
        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        area.pack_start(scroll, True, True, 0)
        self.box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        self.box.set_border_width(20)
        scroll.add(self.box)
        self.heading = Gtk.Label(label='Загружаю сведения…', xalign=0)
        self.heading.get_style_context().add_class('section-title')
        self.box.pack_start(self.heading, False, False, 0)
        self.show_all()
        self.load()

    def destroyed(self, *_):
        self.closed = True
        self.pool.shutdown(wait=False, cancel_futures=True)

    def load(self):
        if self.busy: return
        self.busy = True
        self.set_response_sensitive(Gtk.ResponseType.APPLY, False)
        self.pool.submit(inspect_process, self.pid, self.start, self.snapshot).add_done_callback(self.loaded)

    def response(self, dialog, response):
        if response == 1001:
            from process_control import resolve_process, request_termination
            process = resolve_process(self.snapshot, self.pid, self.start)
            if process:
                request_termination(self, process, self.snapshot,
                                    finished=lambda: GLib.timeout_add(300, self.refresh_after_termination))
            return
        if response != Gtk.ResponseType.APPLY:
            self.destroy()
            return
        if self.busy: return
        if self.reader:
            fresh = self.reader.snapshot()
            if fresh: self.snapshot = fresh
        self.load()

    def refresh_after_termination(self):
        if not self.closed: self.load()
        return GLib.SOURCE_REMOVE

    def loaded(self, future):
        try: data = future.result()
        except Exception: data = {'title': 'Сведения недоступны', 'description': 'Процесс мог завершиться во время чтения.', 'rows': []}
        GLib.idle_add(self.render, data)

    def render(self, data):
        if self.closed: return GLib.SOURCE_REMOVE
        self.busy = False
        self.set_response_sensitive(Gtk.ResponseType.APPLY, True)
        self.stop_button.set_sensitive(not data.get('ended', True))
        self.heading.set_text(data['title'])
        for widget in self.box.get_children():
            if widget is not self.heading: self.box.remove(widget)
        description = Gtk.Label(label=data['description'], xalign=0)
        description.set_line_wrap(True); description.set_max_width_chars(65)
        description.get_style_context().add_class('body')
        self.box.pack_start(description, False, False, 0)
        for title, value in data['rows']:
            row = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3)
            key = Gtk.Label(label=title, xalign=0)
            key.get_style_context().add_class('muted')
            text = Gtk.Label(label=value, xalign=0)
            text.set_line_wrap(True); text.set_max_width_chars(65); text.set_selectable(True)
            row.pack_start(key, False, False, 0); row.pack_start(text, False, False, 0)
            self.box.pack_start(row, False, False, 0)
        self.box.show_all()
        return GLib.SOURCE_REMOVE

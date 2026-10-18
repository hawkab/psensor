# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit, on-demand process termination; never signal a reused PID."""
import os
from pathlib import Path
import pwd
import shutil
import signal


class ProcessEnded(Exception):
    pass


def identity(pid, start):
    if pid <= 1 or pid == os.getpid() or start <= 0:
        raise ValueError('Этот процесс нельзя завершить из Psensor.')
    try:
        with open(f'/proc/{pid}/stat', 'rb', buffering=0) as stream:
            raw = stream.read(4096)
        fields = raw[raw.rfind(b')') + 2:].split()
        if int(fields[19]) != start or fields[0] in (b'Z', b'X'):
            raise ProcessEnded()
        return Path(f'/proc/{pid}').stat().st_uid
    except (FileNotFoundError, ProcessLookupError):
        raise ProcessEnded() from None


def terminate(pid, start):
    """Bind the target with pidfd, then verify its recorded start time again."""
    identity(pid, start)
    if not hasattr(os, 'pidfd_open') or not hasattr(signal, 'pidfd_send_signal'):
        raise OSError('Для безопасного завершения нужен Linux с поддержкой pidfd.')
    try:
        descriptor = os.pidfd_open(pid)
    except ProcessLookupError:
        raise ProcessEnded() from None
    try:
        identity(pid, start)
        signal.pidfd_send_signal(descriptor, signal.SIGTERM)
    except ProcessLookupError:
        raise ProcessEnded() from None
    finally:
        os.close(descriptor)


def helper_path():
    directory = Path(__file__).resolve().parent
    for path in (directory.parent.parent/'bin/psensor-process-terminate',
                 directory/'runtime/bin/psensor-process-terminate',
                 directory.parent/'src/psensor-process-terminate'):
        if path.is_file() and os.access(path, os.X_OK):
            return str(path)
    return None


def resolve_process(snapshot, pid, start):
    rows = (snapshot.get('current', {}).get('processes', []) + snapshot.get('recent', [])
            + (snapshot.get('peak') or {}).get('contributors', [])
            + snapshot.get('current', {}).get('applications', []))
    return next((dict(row) for row in rows if row['pid'] == pid and row['start'] == start), None)


def append_terminate(menu, parent, process, snapshot):
    from gi.repository import Gtk
    item = Gtk.MenuItem(label='Завершить процесс…')
    item.get_style_context().add_class('destructive-action')
    try:
        identity(process['pid'], process['start'])
    except (OSError, ValueError, ProcessEnded):
        item.set_sensitive(False)
        item.set_tooltip_text('Этот процесс уже завершён или недоступен.')
    # Capture the identity now: the table/history may refresh while a menu is open.
    target = dict(process)
    item.connect('activate', lambda *_: request_termination(parent, target, snapshot))
    menu.append(item)
    return item


def populate_link_menu(widget, menu, parent, snapshot):
    from gi.repository import Gtk
    uri = widget.get_current_uri() or ''
    try:
        prefix, pid, start = uri.split(':')
        if prefix != 'process': return
        process = resolve_process(snapshot, int(pid), int(start))
    except ValueError:
        return
    if not process: return
    menu.append(Gtk.SeparatorMenuItem())
    append_terminate(menu, parent, process, snapshot)
    menu.show_all()


_pending = {}


def request_termination(parent, process, snapshot, finished=None):
    key = (process['pid'], process['start'])
    if key in _pending:
        _pending[key].dialog.present()
        return _pending[key]
    request = TerminationRequest(parent, dict(process), snapshot, finished)
    return request


class TerminationRequest:
    def __init__(self, parent, process, snapshot, finished):
        from gi.repository import Gtk
        self.parent, self.process, self.finished = parent, process, finished
        self.key = (process['pid'], process['start'])
        self.child = None
        self.closed = False
        self.completing = False
        name = process.get('name', f'Процесс {process["pid"]}')
        self.dialog = Gtk.MessageDialog(transient_for=parent, modal=True, destroy_with_parent=True,
                                        message_type=Gtk.MessageType.QUESTION,
                                        text=f'Завершить «{name}»?')
        self.dialog.add_button('Отмена', Gtk.ResponseType.CANCEL)
        button = self.dialog.add_button('Завершить процесс', Gtk.ResponseType.ACCEPT)
        button.get_style_context().add_class('destructive-action')
        self.dialog.set_default_response(Gtk.ResponseType.CANCEL)
        try:
            uid = identity(*self.key)
            try: owner = pwd.getpwuid(uid).pw_name
            except KeyError: owner = f'UID {uid}'
            detail = f'PID {self.key[0]} · {owner}\nБудет отправлен штатный запрос завершения (SIGTERM).'
            from analysis_model import category
            count = sum(category(p['name']) == category(name) for p in snapshot.get('current', {}).get('processes', []))
            if count > 1:
                detail += f'\nВ обзоре объединено процессов: {count}. Завершится только выбранный PID.'
            if uid != os.geteuid():
                detail += '\nДля процесса другого пользователя система запросит права администратора.'
            detail += '\nНесохранённые данные этого процесса могут быть потеряны.'
            self.dialog.format_secondary_text(detail)
        except (ProcessEnded, OSError, ValueError) as error:
            self.dialog.destroy()
            self.message('Процесс уже завершён' if isinstance(error, ProcessEnded) else str(error))
            return
        _pending[self.key] = self
        self.dialog.connect('response', self.respond)
        self.dialog.connect('destroy', self.destroyed)
        self.dialog.show_all()

    def destroyed(self, *_):
        self.closed = True
        if self.child and not self.completing:
            self.child.force_exit()
        _pending.pop(self.key, None)

    def respond(self, dialog, response):
        from gi.repository import Gtk
        if response != Gtk.ResponseType.ACCEPT:
            dialog.destroy()
            return
        dialog.set_response_sensitive(Gtk.ResponseType.ACCEPT, False)
        dialog.set_response_sensitive(Gtk.ResponseType.CANCEL, False)
        try:
            terminate(*self.key)
        except PermissionError:
            self.authorize()
        except ProcessEnded:
            self.complete('Процесс уже завершён')
        except (OSError, ValueError) as error:
            self.complete('Не удалось завершить процесс', str(error))
        else:
            self.complete('Запрос на завершение отправлен')

    def authorize(self):
        from gi.repository import Gio, GLib
        helper = helper_path()
        pkexec = shutil.which('pkexec')
        if not helper or not pkexec:
            self.complete('Недостаточно прав', 'Для завершения чужого процесса нужны polkit (pkexec) и psensor-process-terminate.')
            return
        try:
            # No shell, no persistent privileged daemon, and the helper rechecks identity after authentication.
            self.child = Gio.Subprocess.new([pkexec, helper, str(self.key[0]), str(self.key[1])],
                                            Gio.SubprocessFlags.STDOUT_SILENCE | Gio.SubprocessFlags.STDERR_SILENCE)
            self.dialog.set_markup('Ожидается подтверждение системы…')
            self.child.wait_async(None, self.authorized)
        except GLib.Error as error:
            self.complete('Не удалось запросить права', error.message)

    def authorized(self, child, result):
        from gi.repository import GLib
        try:
            child.wait_finish(result)
            status = child.get_exit_status() if child.get_if_exited() else -1
        except GLib.Error:
            status = -1
        if status == 0:
            self.complete('Запрос на завершение отправлен')
        elif status == 3:
            self.complete('Процесс уже завершён', 'Сигнал не отправлен: процесс завершился или PID уже принадлежит другой задаче.')
        elif status in (126, 127):
            self.complete('Завершение не подтверждено', 'Система не предоставила права администратора.')
        else:
            self.complete('Не удалось завершить процесс', 'ОС отклонила запрос. Процесс может быть недоступен с текущими правами.')

    def message(self, title, detail=None):
        from gi.repository import Gtk
        self.result = Gtk.MessageDialog(transient_for=self.parent, modal=True, destroy_with_parent=True,
                                        message_type=Gtk.MessageType.INFO, buttons=Gtk.ButtonsType.CLOSE, text=title)
        self.result.format_secondary_text(detail or f'{self.process.get("name", "Процесс")} · PID {self.key[0]}')
        self.result.connect('response', lambda dialog, *_: dialog.destroy())
        self.result.show_all()

    def complete(self, title, detail=None):
        if self.closed: return
        self.completing = True
        self.dialog.destroy()
        self.message(title, detail)
        if self.finished: self.finished()

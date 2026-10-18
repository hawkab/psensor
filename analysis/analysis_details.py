# SPDX-License-Identifier: GPL-2.0-or-later
"""Graphs and tables are imported only when the user opens Details."""
import datetime
import math
from gi.repository import Gtk, Gdk, GLib
from ui_common import stamp, label, update_rows, Inspector

class HistoryGraph(Gtk.DrawingArea):
    def __init__(self, temperature=False):
        super().__init__()
        self.temperature = temperature
        self.series = []
        self.set_size_request(260, 110)
        self.connect('draw', self.draw_graph)

    def draw_graph(self, widget, cr):
        width, height = self.get_allocated_width(), self.get_allocated_height()
        x0, y0, w, h = 40, 10, width - 52, height - 33
        if not self.series:
            cr.set_source_rgb(.65, .68, .72)
            cr.move_to(x0, y0 + 35)
            cr.show_text('Записывается первый интервал…')
            return False
        ts0, ts1 = self.series[0][0], self.series[-1][0]
        if self.temperature:
            vals = [v for row in self.series for v in row[2:5] if v is not None]
            lo = max(0, math.floor((min(vals or [45]) - 3) / 10) * 10)
            hi = max(lo + 20, math.ceil((max(vals or [75]) + 3) / 10) * 10)
            channels = [(2, (.98, .64, .25)), (3, (.90, .38, .44)), (4, (.55, .66, .96))]
        else:
            lo, hi, channels = 0, 100, [(1, (.32, .75, .88))]
        cr.select_font_face('Sans')
        cr.set_font_size(10)
        for i in range(3):
            y = y0 + h * i / 2
            cr.set_source_rgba(.6, .65, .7, .18)
            cr.move_to(x0, y); cr.line_to(x0 + w, y); cr.stroke()
            cr.set_source_rgb(.63, .66, .70)
            cr.move_to(3, y + 4)
            cr.show_text(f'{hi - (hi - lo) * i / 2:.0f}' + ('°' if self.temperature else '%'))
        for index, color in channels:
            cr.set_source_rgb(*color)
            cr.set_line_width(1.8)
            started = False
            for row in self.series:
                value = row[index]
                if value is None:
                    if started: cr.stroke()
                    started = False
                    continue
                x = x0 + w * (row[0] - ts0) / max(1, ts1 - ts0)
                y = y0 + h * (1 - max(0, min(1, (value - lo) / (hi - lo))))
                if started: cr.line_to(x, y)
                else: cr.move_to(x, y); started = True
            cr.stroke()
        cr.set_source_rgb(.63, .66, .70)
        cr.move_to(x0, height - 3); cr.show_text(stamp(ts0))
        cr.move_to(width - 64, height - 3); cr.show_text(stamp(ts1))
        return False


class DetailsWindow(Gtk.ApplicationWindow):
    def __init__(self, app):
        super().__init__(application=app, title='Подробнее о нагрузке и нагреве')
        self.set_default_size(650, 800)
        self.set_border_width(0)
        self.set_icon_name('utilities-system-monitor')
        self.connect('destroy', self.on_destroy)
        header = Gtk.HeaderBar(title='Подробнее о нагрузке и нагреве', show_close_button=True)
        header.set_subtitle('Сейчас и за последние 15 минут')
        self.set_titlebar(header)
        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.add(scroll)
        content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        content.set_border_width(16)
        scroll.add(content)
        self.status = label('Ожидается замер…', 'muted')
        content.pack_start(self.status, False, False, 0)
        cards = Gtk.Box(spacing=12)
        self.cpu = label('—', 'big-value')
        self.temp = label('—', 'big-value')
        self.cpu_sub = label('', 'muted')
        self.temp_sub = label('', 'muted')
        for title, value, sub in [('Загрузка ЦП', self.cpu, self.cpu_sub), ('Температура ЦП · Tctl', self.temp, self.temp_sub)]:
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            box.get_style_context().add_class('metric-card')
            box.pack_start(label(title, 'section-title'), False, False, 0)
            box.pack_start(value, False, False, 0)
            box.pack_start(sub, False, False, 0)
            cards.pack_start(box, True, True, 0)
        content.pack_start(cards, False, False, 0)
        self.cpu_graph, self.temp_graph = HistoryGraph(), HistoryGraph(True)
        graphs = Gtk.Box(spacing=12)
        for title, graph in [('Загрузка всех потоков', self.cpu_graph), ('Tctl / Tccd1 / Tccd2', self.temp_graph)]:
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            heading = label(title, 'section-title')
            if graph.temperature:
                heading.set_markup('<span foreground="#faa340">Tctl</span> / <span foreground="#e66170">Tccd1</span> / <span foreground="#8ca8f5">Tccd2</span>')
            box.pack_start(heading, False, False, 0)
            box.pack_start(graph, False, False, 0)
            graphs.pack_start(box, True, True, 0)
        content.pack_start(graphs, False, False, 0)
        self.fans = label('', 'muted', True)
        content.pack_start(self.fans, False, False, 0)
        notebook = Gtk.Notebook()
        # Hidden identity column keeps selection/menu targets safe across PID reuse.
        self.now_store = Gtk.ListStore(str, float, float, float, int, str)
        self.recent_store = Gtk.ListStore(str, float, float, str, int, str)
        self.now_store.set_sort_column_id(1, Gtk.SortType.DESCENDING)
        self.recent_store.set_sort_column_id(1, Gtk.SortType.DESCENDING)
        self.add_table(notebook, 'Сейчас', self.now_store,
                       [('Процесс', 0, 'text'), ('CPU¹ %', 1, 'percent'), ('Доля ЦП %', 2, 'percent'), ('RAM МиБ', 3, 'number'), ('PID', 4, 'text')])
        self.add_table(notebook, 'Недавняя нагрузка', self.recent_store,
                       [('Процесс', 0, 'text'), ('CPU, с', 1, 'number'), ('Пик CPU¹ %', 2, 'percent'), ('Состояние', 3, 'text'), ('PID', 4, 'text')])
        content.pack_start(notebook, True, True, 0)
        content.pack_start(label('¹ 100% CPU = один логический поток. Строки можно выбирать и копировать правой кнопкой.', 'muted', True), False, False, 0)
        explanation = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        explanation.get_style_context().add_class('metric-card')
        explanation.pack_start(label('Что могло повлиять на нагрев', 'section-title'), False, False, 0)
        self.explanation = label('', wrap=True)
        self.explanation.set_max_width_chars(70)
        explanation.pack_start(self.explanation, False, False, 0)
        content.pack_start(explanation, False, False, 0)
        content.pack_start(label('Оценка по активности CPU и времени. Тепло отдельного процесса не измеряется; короткие задачи между замерами могут быть пропущены.', 'muted', True), False, False, 0)
        self.timer = GLib.timeout_add_seconds(2, self.refresh)
        self.refresh()
        self.show_all()
        display = self.get_display()
        monitor = display.get_monitor_at_window(self.get_window()) or display.get_primary_monitor()
        if monitor:
            area = monitor.get_workarea()
            self.resize(min(650, area.width - 40), min(800, area.height - 70))
            self.move(area.x + max(0, area.width - 680), area.y + 20)

    def add_table(self, notebook, title, store, columns):
        view = Gtk.TreeView(model=store)
        view.set_headers_visible(True)
        view.set_tooltip_text('Правый клик → сведения, завершение процесса или копирование')
        for title_text, index, kind in columns:
            cell = Gtk.CellRendererText()
            if index == 0:
                cell.set_property('ellipsize', 3)
            col = Gtk.TreeViewColumn(title_text)
            col.pack_start(cell, True)
            if kind == 'text': col.add_attribute(cell, 'text', index)
            else:
                cell.set_property('xalign', 1.0)
                def render(column, renderer, model, iterator, unused, i=index, fmt=kind):
                    renderer.set_property('text', f'{model[iterator][i]:.1f}' if fmt == 'percent' else f'{model[iterator][i]:.0f}')
                col.set_cell_data_func(cell, render)
            col.set_sort_column_id(index)
            col.set_resizable(True)
            col.set_sizing(Gtk.TreeViewColumnSizing.FIXED)
            if index == 0:
                col.set_expand(True); col.set_fixed_width(210)
            else:
                col.set_fixed_width(100 if index == 3 and kind == 'text' else 80)
            view.append_column(col)
        view.connect('button-press-event', self.row_context)
        view.connect('row-activated', self.inspect_row)
        pane = Gtk.ScrolledWindow()
        pane.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        pane.set_min_content_height(190)
        pane.add(view)
        notebook.append_page(pane, Gtk.Label(label=title))

    def row_context(self, view, event):
        if event.button != 3: return False
        hit = view.get_path_at_pos(int(event.x), int(event.y))
        if not hit: return False
        view.get_selection().select_path(hit[0])
        model, iterator = view.get_selection().get_selected()
        row = list(model[iterator])
        pid, start = map(int, row[5].split(':'))
        from process_control import resolve_process, append_terminate
        process = resolve_process(self.snapshot, pid, start)
        if not process: return False
        snapshot = self.snapshot
        menu = Gtk.Menu()
        inspect = Gtk.MenuItem(label='О процессе…')
        inspect.connect('activate', lambda *_: Inspector(self, pid, start, snapshot))
        menu.append(inspect)
        append_terminate(menu, self, process, snapshot)
        menu.append(Gtk.SeparatorMenuItem())
        clipboard = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
        for title, text in [('Копировать PID', str(pid)), ('Копировать строку', ' | '.join(map(str, row[:5])))]:
            item = Gtk.MenuItem(label=title)
            item.connect('activate', lambda item, value=text: clipboard.set_text(value, -1))
            menu.append(item)
        menu.show_all(); menu.popup_at_pointer(event)
        return True

    def inspect_row(self, view, path, column):
        pid, start = map(int, view.get_model()[path][5].split(':'))
        from process_control import resolve_process
        process = resolve_process(self.snapshot, pid, start)
        if process: Inspector(self, pid, process['start'], self.snapshot)

    def refresh(self):
        try:
            s = self.get_application().snapshot(details=True)
            if s is None: raise ValueError('Нет измерений')
            self.snapshot = s
            now = s['current']
            age = datetime.datetime.now().timestamp() - now['ts']
            stale = age > 12
            self.status.set_text(('Данные устарели — проверь сервис записи. ' if stale else 'Обновлено ') + stamp(now['ts']) + f' · собрано {s["covered_seconds"]/60:.1f} мин истории')
            self.cpu.set_text(f'{now["cpu"]:.1f}%')
            self.cpu_sub.set_text(f'От всех {s["logical_cpus"]} логических потоков')
            temp = now['hardware']['temps'].get('Tctl')
            self.temp.set_text(f'{temp:.1f} °C' if temp is not None else '—')
            peak = s.get('peak')
            self.temp_sub.set_text(f'Пик {peak["temperature"]:.1f} °C · {stamp(peak["ts"])}' if peak else 'Пик пока не измерен')
            for graph in (self.cpu_graph, self.temp_graph):
                graph.series = s.get('series', [])
                graph.queue_draw()
            fans = sorted((f for f in now['hardware']['fans'] if not f['gpu']), key=lambda f: f['label'])
            self.fans.set_text('Вентиляторы платы: ' + ' · '.join(f'{f["label"]}: {f["rpm"]} RPM' for f in fans if f['rpm'] > 0) if fans else 'Датчики оборотов кулера ЦП недоступны')
            update_rows(self.now_store, [[p['name'], p['cpu'], p['machine_cpu'], p['rss_mb'], p['pid'], f'{p["pid"]}:{p["start"]}'] for p in now['processes'][:15]])
            update_rows(self.recent_store, [[p['name'], p['cpu_seconds'], p['peak'], 'Работает' if p['alive'] else 'Завершён', p['pid'], f'{p["pid"]}:{p["start"]}'] for p in s['recent'][:15]])
            lines = s['analysis'][:2]
            if s['recent']:
                p = s['recent'][0]
                lines.append(f'За окно истории больше всего CPU использовал {p["name"]}: {p["cpu_seconds"]:.1f} с.')
            if peak and peak['contributors']:
                names = ', '.join(p['name'] for p in peak['contributors'][:2])
                lines.append(f'В 30 секунд перед пиком температуры активны: {names}.')
            self.explanation.set_text('\n'.join(lines))
            audit = now.get('journal', {})
            if audit.get('accessible'):
                message = ('Журнал ядра за 2 часа:\n' + '\n'.join(audit['events'])) if audit.get('events') else 'В журнале за 2 часа сообщений о критическом нагреве нет.'
                self.explanation.set_text(self.explanation.get_text() + '\n' + message)
        except (OSError, ValueError, KeyError) as e:
            self.status.set_text('Ожидается запись измерений. Проверь psensor-analysis.service.')
        return GLib.SOURCE_CONTINUE

    def on_destroy(self, widget):
        self.get_application().reader.drop_details()
        if self.timer:
            GLib.source_remove(self.timer)
            self.timer = 0

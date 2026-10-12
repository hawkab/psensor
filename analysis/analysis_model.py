# SPDX-License-Identifier: GPL-2.0-or-later
"""Evidence-based, short explanations from the local sampled history."""


def category(name):
    if name.lower().startswith('ldap'): return 'Служба LDAP'
    if name in ('cc1', 'cc1plus', 'gcc', 'g++', 'clang', 'clang++'): return 'Компиляция C/C++'
    if name.startswith(('Gradle', 'Gradle worker')): return 'Gradle'
    if name.startswith(('Эмулятор', 'QEMU')): return 'Android-эмуляторы'
    if name.startswith('Jira'): return 'Jira'
    if name.startswith('ChatGPT'): return 'ChatGPT'
    if name.startswith(('chrome', 'chromium')): return 'Chromium'
    return name


def groups(rows, metric):
    result = {}
    for row in rows:
        name = category(row['name'])
        if name in ('heat-check', 'heat-monitor.py', 'load-window.py', 'render-window.py'): continue
        group = result.setdefault(name, {'name': name, 'value': 0, 'peak': 0, 'rows': []})
        group['value'] += row.get(metric, 0)
        group['peak'] = max(group['peak'], row.get('peak', row.get('cpu', 0)))
        group['rows'].append(row)
    for group in result.values():
        group['process'] = max(group['rows'], key=lambda r: r.get(metric, 0))
    return sorted(result.values(), key=lambda g: g['value'], reverse=True)


def explain(snapshot):
    now = snapshot['current']
    current = groups(now['processes'], 'cpu')
    recent = groups(snapshot['recent'], 'cpu_seconds')
    cpus = snapshot['logical_cpus']
    temp = now['hardware']['temps'].get('Tctl')
    profile = now['hardware'].get('power_profile', now['hardware']['profile'])
    top = current[0] if current else None
    at_peak = groups((snapshot.get('peak') or {}).get('contributors', []), 'cpu_seconds')
    near_peak = bool(at_peak and at_peak[0]['value'] >= 10)
    past = at_peak[0] if near_peak else (recent[0] if recent else None)
    known_busy = past and (past['peak'] / cpus >= 5 or past['peak'] >= 50) and past['value'] >= (10 if near_peak else 30)
    recent_gradle = past if past and past['name'] == 'Gradle' and known_busy else None
    currently_busy = now['cpu'] >= 12 or (top and top['value'] >= 50 and (temp or 0) >= 65)
    if currently_busy and top and top['value'] / cpus < now['cpu'] * .15:
        headline = 'Есть нагрузка, её источник ещё уточняется'
        subtitle = 'Измеренные процессы пока объясняют только часть активности ЦП.'
    elif currently_busy and top:
        headline = {'Gradle': 'Сейчас идёт сборка Gradle', 'Android-эмуляторы': 'ЦП занят Android-эмуляторами'}.get(top['name'], 'Сейчас ЦП нагружает ' + top['name'])
        subtitle = 'Нагрузка на отдельные ядра может объяснять нагрев, даже при небольшой общей загрузке.'
    elif recent_gradle:
        headline = 'Недавно работала сборка Gradle'
        subtitle = 'Сейчас нагрузка небольшая; недавняя сборка могла повлиять на нагрев.'
        past = recent_gradle
    elif known_busy:
        headline = {'Android-эмуляторы': 'Недавно активно работали Android-эмуляторы',
                    'ChatGPT': 'Недавно активно работал ChatGPT',
                    'Компиляция C/C++': 'Недавно компилировался код C/C++',
                    'Служба LDAP': 'Недавно работала служба LDAP'}.get(past['name'], 'Недавняя нагрузка: ' + past['name'])
        subtitle = 'Эта активность совпала с пиком температуры. Сейчас нагрузка небольшая.' if near_peak else 'Сейчас нагрузка небольшая; в истории был всплеск активности.'
    elif profile == 'performance':
        headline = 'Фоновые задачи и режим «Производительность»'
        subtitle = 'Сильной нагрузки сейчас нет. ЦП может сохранять высокие частоты.'
    elif top:
        headline = 'Сейчас работает ' + top['name']
        subtitle = 'Нагрузка небольшая. Явного виновника нагрева пока не видно.'
    else:
        headline = 'Заметной нагрузки сейчас нет'
        subtitle = 'Записываем историю, чтобы увидеть следующие всплески.'
    values = snapshot.get('temperature_range') or [row[2] for row in snapshot.get('series', []) if row[2] is not None]
    ccd = [snapshot['chiplet_peak']] if snapshot.get('chiplet_peak') is not None else [v for row in snapshot.get('series', []) for v in row[3:5] if v is not None]
    temperature = f'Сейчас {temp:.0f} °C' if temp is not None else 'Температура недоступна'
    if values:
        temperature += f' · за 15 мин {min(values):.0f}–{max(values):.0f} °C'
    if ccd:
        temperature += f'\nПик на чиплетах: {max(ccd):.0f} °C'
    inventory = now.get('applications', [])
    emulator_count = sum(category(p['name']) == 'Android-эмуляторы' for p in inventory)
    if not inventory:
        emulator_count = sum(category(p['name']) == 'Android-эмуляторы' for p in now['processes'])
    if top:
        current_text = 'Сейчас основная активность — ' + top['name'] + '.'
        if emulator_count:
            current_text += f' Android-эмуляторов запущено: {emulator_count}.'
    else:
        current_text = 'Активные процессы пока не измерены.'
    recent_text = 'Недавнего сильного всплеска в истории нет.'
    if past:
        if known_busy:
            active = sum(p['cpu'] for p in now['processes'] if category(p['name']) == past['name']) / cpus
            recent_text = past['name'] + (' сейчас почти не нагружает ЦП.' if active < 1 else ' нагружал ЦП и остаётся активным.')
        else:
            recent_text = 'В недавней истории больше всего работал ' + past['name'] + ', без сильного всплеска.'
    return {'headline': headline, 'subtitle': subtitle, 'temperature': temperature,
            'temperature_range': (min(values), max(values)) if values else None,
            'chiplet_peak': max(ccd) if ccd else None,
            'temp': temp, 'cpu': now['cpu'], 'profile': profile,
            'current': top, 'recent': past, 'current_text': current_text,
            'recent_text': recent_text, 'emulator_count': emulator_count,
            'journal': now.get('journal', {'accessible': False}), 'current_groups': current,
            'recent_groups': recent}

from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0, str(ROOT / 'analysis'))
import copy
import sys

from analysis_model import explain

def row(name,cpu=0,seconds=0,peak=0,pid=1):
    return dict(name=name,cpu=cpu,cpu_seconds=seconds,peak=peak,pid=pid,start=1,rss=0,alive=False)
base=dict(logical_cpus=32,current=dict(cpu=2,ts=1000,processes=[row('Эмулятор · test',10)],
    hardware=dict(temps={'Tctl':70},power_profile='performance',profile='performance'),
    applications=[row('Эмулятор · '+str(i),pid=i+1) for i in range(3)],
    journal=dict(accessible=True,events=[])),series=[[1,2,62,55,49],[2,2,76,91,50]],
    recent=[row('ldap(3)',seconds=400,peak=20),row('Gradle',seconds=180,peak=350)],
    peak=dict(contributors=[row('Gradle',seconds=50,peak=350)]))
result=explain(base)
assert result['headline']=='Недавно работала сборка Gradle'
assert 'почти не нагружает' in result['recent_text'] and result['emulator_count']==3
assert result['temperature_range']==(62,76) and result['chiplet_peak']==91
active=copy.deepcopy(base);active['current']['cpu']=15;active['current']['processes']=[row('Gradle',480)]
assert explain(active)['headline']=='Сейчас идёт сборка Gradle'
unknown=copy.deepcopy(base);unknown['current']['cpu']=60;unknown['current']['processes']=[row('service',2)]
assert 'уточняется' in explain(unknown)['headline']
postgres=copy.deepcopy(base);postgres['current']['cpu']=40
postgres['current']['processes']=[row('postgres',600,pid=10),row('postgres',520,pid=11)]
assert '35% всего ЦП' in explain(postgres)['subtitle'] and '40%' in explain(postgres)['subtitle']
idle=copy.deepcopy(base);idle['peak']={};idle['recent']=[]
assert 'Производительность' in explain(idle)['headline']
print('PASS: dominant activity before thermal peak, ended/active Gradle, three emulators, temp/CCD ranges, unattributed spike, performance mode')

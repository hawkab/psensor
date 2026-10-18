# Background cost and on-demand UI

The collector keeps raw history before the window opens. It samples every two
seconds; detailed explanation, graphs, journal queries and process inspection
run only while requested UI is open. Closing all windows exits the UI process.
Details and Inspector are separate modules imported on demand. One shared reader
serves visible windows, and the inspector allows one job at a time and closes
its worker when dismissed.

Tray temperature colors reuse the native sensor samples and cache enabled
sensors and their thresholds when preferences change. Only critical temperature
creates a one-second blink timer. It switches between two small installed PNGs
without touching sensor locks, labels, graphs or the history database. Cooling,
disabling the rule or stale data removes the timer. Unchanged icon paths are not
sent to AppIndicator again, and the tray slot is never hidden during blinking.

## Algorithms

- One bounded, unbuffered `/proc/PID/stat` read per process; integer CPU ticks
  and RSS KiB. PID **and start time** identify a process. Names are stored once
  in a separate table; arbitrary command lines are not persisted.
- Only the first CPU summary line is read. Page size and hwmon paths/labels are
  cached. Power profile uses a persistent D-Bus connection instead of periodic CLI
  processes. No unused per-core frequency reads.
- SQLite WAL, indexed boot/time, batched transactions: ten seconds with UI closed,
  two seconds with UI open. A viewer lease expires if UI dies. SIGUSR1 flushes the
  completed buffer on tray open without extra process sampling. Normal shutdown
  commits outstanding history; disk retention is 24 hours.
- UI reads only new rows, coalesces refreshes within 250 ms and retains a bounded
  15-minute window plus thermal-peak context. Rolling integer sums and monotonic
  peak queues avoid recalculating all process totals. Graph series is created only
  for Details and released when it closes. Legacy history remains readable.
- Native tray strings are freed, label-sensor selection is cached and unchanged
  menu labels are not rewritten. Hidden windows do not rebuild graphs/tables;
  generation counters suppress redraw without a new sample. Mapping a window or
  changing preferences forces a refresh.
- Only enabled sensor rules are cached in the action array. No action timer
  exists when no rules are enabled. Shell commands and notification D-Bus calls
  are asynchronous; GTK does not wait for a busy sensor mutex.
- Kernel journal inspection runs asynchronously only in the UI, at most once
  per minute, with a two-second timeout. Process details and log tails are bounded.

## Measured example

Measurements on a Ryzen 9 7950X, 32 logical CPUs and roughly 1,000 processes,
using process user/system CPU tick differences over 40 seconds. GUI startup
was excluded by a five-second warmup. “One CPU” is 100% of one logical CPU;
whole-system share divides by 32. RSS includes shared libraries and must not be
summed across processes as though it were private memory.

| Mode | One CPU | Whole system | RSS |
|---|---:|---:|---:|
| Collector before optimization | 1.475% | 0.0461% | 30.4 MiB |
| Compact collector, windows closed | 0.625% | 0.0195% | 19.5 MiB |
| Overview UI | 0.150% | 0.0047% | 80.1 MiB |
| Overview + Details + Inspector UI | 0.475% | 0.0148% | 83.3 MiB |

Collector physical writes dropped from 1.38 MiB to 244 KiB in these intervals.
Native Psensor used about 0.15–0.20% of one CPU and 38–39 MiB warmed RSS.
The final installed collector measured 0.675% of one CPU, 20.46 MiB RSS and
392 KiB writes in another 40-second interval including warmup. These are observed
samples, not guaranteed overhead on other hardware/workloads.

## Regression evidence

`python3 tests/analysis/run.py` checks rolling sums/peaks against a full reference
over 700 varied intervals, including window boundaries and reused PIDs; legacy
and compact SQLite history; no duplicate rows/queries; lazy graph arrays and lease
cleanup; batched writes and flush without extra sampling; rule duration,
hysteresis, stale values and zero RPM; asynchronous shell success/error;
standard notifications and clickable URI; lazy module imports, single inspector
job and worker cleanup; application shutdown; installation generation; 100,000
tray label updates without growing heap; and hidden/unchanged native UI behavior.
GUI tests use synthetic history and isolated D-Bus/Xvfb sessions.

Process termination adds no background timer/thread. The control module loads
only when a process menu/action is requested. Tests signal disposable children
only, verify Python/native pidfd identity checks, confirmation/cancellation,
PID reuse in tables, and simulated authorization cancellation. The polkit helper
has no persistent process and is invoked only after an explicit user action.

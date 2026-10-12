# Quest GPU captures

Use the existing game build first. Evidence stays under ignored `work/evidence`;
do not publish captures, device identifiers or account logs. The collector does
not install, launch, stop the game, clear logs or change graphics settings.

Open OVR Metrics Tool once, select Basic stats and enable CSV recording.
Basic includes App GPU Time. Reserve Advanced for a separate diagnostic run:
Meta notes that Advanced adds overhead and can misrepresent normal performance.
Install the actual app from Meta Horizon Store first. Some headsets only include
the system package with version `1.0-stub`: its presence does not mean metrics
recording is available. Probe reports both installation and launch readiness;
baseline refuses the stub. Close the game before installing OVR Metrics Tool.
Include App GPU Time, FPS, stale frames and CPU/GPU utilization. The collector
only retrieves this game's CSV files modified during the capture. Missing CSV
data is reported; hardware-wide counters are not app-exclusive GPU times.

```powershell
python scripts/qa/quest/capture-gpu.py --output work/evidence/gpu --mode probe
python scripts/qa/quest/capture-gpu.py --output work/evidence/gpu --mode baseline --seconds 180
```

For a baseline, detailed profiling must be disabled and the game started in a
fresh process. Warm up, load a skirmish, then record an idle base, camera movement
and a busy scene with the same settings. Do not record video during measurement.

For a separate trace run, fully close the game and launcher first:

```powershell
python scripts/qa/quest/capture-gpu.py --output work/evidence/gpu --mode prepare-trace
```

Start a fresh game, warm it up and reproduce the same scene:

```powershell
python scripts/qa/quest/capture-gpu.py --output work/evidence/gpu --mode trace --seconds 180
python scripts/qa/quest/capture-gpu.py --output work/evidence/gpu --mode restore
```

Trace capture restores the tool's profiling switch on exit; fully close and
restart afterward to discard that process's profiling context. Use Restore if
preparation was interrupted, the connection was lost or capture never started.
Detailed profiling changes GPU timing; compare its overhead with the baseline.
Counters are selected by names from the actual runtime catalog, never fixed IDs.
To start without the OVR Metrics app, use `--mode baseline --without-ovr`.
This explicitly skips the CSV requirement and retrieval. The built-in counters
remain hardware-wide; runtime-performance.log only captures timing tags emitted
by this game's process and may be empty. Missing app GPU time is recorded rather
than inferred from CPU, frame pacing or transfer-only GPU timestamps. A normal
capture does not enable detailed profiling. Short trace runs remain separate.
The first normal game capture successfully emits the runtime `VrApi` App= field
from the game's PID without OVR Metrics. It supplies app GPU time in that run;
do not assume every runtime/process emits it. The manifest records sample count.
Use `--mode trace --without-ovr` after separate preparation for a trace without
CSV collection, then restart after the collector restores profiling.
Short surface traces are separate from native CPU times and transfer GPU times.
The tool does not request per-draw or newer per-stage counters unsupported by the
device's help output. Inspect manifest.json for missing data before conclusions.

Sources: [OVR Metrics](https://developers.meta.com/vr/documentation/native/android/ts-ovrmetricstool/)
and [GPU profiler](https://developers.meta.com/vr/documentation/spatial-sdk/ts-ovrgpuprofiler/).

## Detailed-mode compatibility failure

Local Test 01 on the tested Quest runtime reaches the intro but then loses the
Vulkan device after detailed profiling is enabled; stderr records
`DxvkSubmissionQueue: Command submission failed: VK_ERROR_DEVICE_LOST`, followed
by OpenXR begin-frame failures. The sequence suggests a profiling/driver conflict;
it does not establish the precise driver defect. Preserve private evidence,
restore the owned profiling flag and fully restart the app before judging normal
operation. The maintainer confirms normal startup after restoration. Do not retry
detailed tracing until compatibility has been reviewed.
The collector honors a private `detailed-trace-blocked.json` marker in its output
folder. Restore and ordinary baseline modes remain available. Normal counters
and game-process runtime App= timing are the current measurement path.

For targeted comparisons, keep resolution, AA and camera fixed. Reload the same
saved skirmish where practical; avoid new commands during the capture and change
one existing graphics setting between fresh app processes. Begin with Shadows
On/Off. Compare runtime App= and FPS together, noting GPU clock, power state and
scene changes. A reduction in CPU submission time alone is not a GPU saving.

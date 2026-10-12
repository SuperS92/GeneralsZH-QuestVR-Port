# Quest GPU captures

Use the existing game build first. Evidence stays under ignored `work/evidence`;
do not publish captures, device identifiers or account logs. The collector does
not install, launch, stop the game, clear logs or change graphics settings.

Open OVR Metrics Tool once, select Advanced stats and enable CSV recording.
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
Short surface traces are separate from native CPU times and transfer GPU times.
The tool does not request per-draw or newer per-stage counters unsupported by the
device's help output. Inspect manifest.json for missing data before conclusions.

Sources: [OVR Metrics](https://developers.meta.com/vr/documentation/native/android/ts-ovrmetricstool/)
and [GPU profiler](https://developers.meta.com/vr/documentation/spatial-sdk/ts-ovrgpuprofiler/).

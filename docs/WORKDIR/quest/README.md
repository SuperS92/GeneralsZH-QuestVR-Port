# Quest development

The optional OpenXR layer builds on MYSOREZ v1.4.0. Start with
[architecture](ARCHITECTURE.md), [build instructions](BUILD_RELEASE.md),
[upstream updates](UPSTREAM_SYNC.md), and the [patch ledger](PATCH_LEDGER.json).

[UPSTREAM_PIN.json](UPSTREAM_PIN.json) records the engine and dependency commits.
[OFFLINE_IMPORTS.json](OFFLINE_IMPORTS.json) records the sole permitted offline
Quest donor: v0.44. Later donor networking, timing and numeric experiments are
excluded. Simulation, replay and transport behavior remain owned by upstream.

Private development history, recordings, replays, accounts and signing material
are not part of this source branch. Source checks do not certify runtime behavior.
The public README describes the headset tests and remaining untested modes.

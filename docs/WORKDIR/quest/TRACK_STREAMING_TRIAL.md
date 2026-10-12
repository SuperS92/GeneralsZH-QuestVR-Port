# Vehicle track buffer trial

Local Test 02 retains public Android version 1.4.0.9. Vehicle track streaming is
default off and only applies to Quest world-eye rendering. It is an unverified
performance trial, not a promised GPU saving.

The dedicated track vertex buffer is created with DYNAMIC and WRITEONLY usage.
Each upload rewrites the visible prefix; subsequent indices only reference that
prefix. The trial passes D3DLOCK_DISCARD so DXVK can rename the backing allocation
instead of waiting for an earlier draw to finish. It never uses NOOVERWRITE while
overwriting the prefix. Keep the existing exact-payload second-eye reuse check:
visibility, ordering, positions and fade must all match before skipping upload.
Recorded DXVK draws retain their own backing slices. The buffer is not the shared
sorting ring, and track triangles use the direct DX8 draw path.

Off retains the prior lock flags. Flat sequences, ordinary Android and other
platforms retain their rendering paths. Do not change track update/fading rules,
scene advancement, simulated objects, RNG, compiler math, networking or CRCs.

With Performance metrics enabled, the CPU scopes track_pack, track_lock,
track_copy, track_unlock and track_submit split the existing tracks scope.
Nested values are not additive to the parent. Lock time may be a GPU wait; a
shorter CPU scope alone does not establish a GPU or FPS improvement. Compare
runtime App=, FPS and memory counters in the same scene. No detailed driver
profiling is needed or allowed on the current failed configuration.

Validate Off first, then On after a full game/launcher restart. Check moving and
fading tracks from both eyes, including changed visibility and map transitions.
Compare normal GPU logs in the saved Skirmish scene and the USA mission. Before
public promotion, validate replay checkpoints and a complete PC match with the
trial enabled. Source isolation or compilation cannot certify live crossplay.

Reference: [Microsoft D3DLOCK contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dlock).
The pinned DXVK D3D8 buffer forwards this flag to its D3D9 dynamic-buffer path;
no dependency or translation-layer changes are part of this trial.

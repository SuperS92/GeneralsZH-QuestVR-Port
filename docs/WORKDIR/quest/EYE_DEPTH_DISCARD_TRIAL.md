# Completed-eye depth lifetime trial

Local Test 07 adds **Eye depth buffer (experimental)**, default off. Restart
the game and launcher after changes. Keep scale 1.5, shadows on and AA off;
terrain batching, track streaming and compact HUD on, terrain-view culling off.
Keep the Test 06 fog option fixed in both comparisons. Android version and the
published APK updater remain unchanged.

The original full eye clear already resets color, depth and stencil. Setting
D3D9's resource discard flag globally would not establish a safe lifetime for
intermediate water/shadow passes. A Vulkan layout transition alone spills the
current DXVK render pass and cannot retroactively remove its depth store.

Add a separately identified, Quest-only DXVK interop extension. Preserve the
existing interop vtable, dependency pin and all upstream patches. Only the
explicit stereo-eye End_Render scope can request it. The original sorting flush
and any pillarbox work finish before the wrapper's EndScene hook; the hook runs
before EndScene's possible implicit submission. No duplicate scene traversal,
sorting flush, extra queue submission, synchronization wait or numeric change.

Confirm the actual eye color/depth bindings, frame state, exact D24S8 single
sample extent and the bootstrap-owned depth surface. Unknown interfaces,
resources, failed binds, MSAA, non-eye/UI/campaign passes and Present retain
the original path. A consumed ticket cannot issue a second request. Release
the cached optional interface with graphics teardown. Depth precision, stencil
operations, draws, shadows, water, resolution and color output stay unchanged.

The extension validates the actual D3D9 in-scene binding and surface descriptor,
then queues DXVK's existing discardImageView for depth and stencil on its original
command stream. This can change an eligible active secondary render pass's store
ops to DONT_CARE. Intermediate passes retain their original data. A submitted
or otherwise ineligible pass may receive no store optimization; do not claim a
bandwidth/FPS benefit from successful queuing alone. Log queued/rejected requests
separately from runtime game GPU time and hardware-wide counters.

Host tests exercise the actual scope and one-shot gates, including both eyes,
nested scopes, missing/mismatched resources, Present and expired scope. Native
builds, matching patched DXVK in both profiles, exact APK/locale/privacy checks
and headset A/B still follow. No host pixel-equivalence or new crossplay claim.
Headset checks must include shadows, water/reflections, transparent effects,
moving the camera, both eyes, pause and campaign transitions. A replay and a
complete live PC match remain required before publication. Any future release
must include corresponding dependency sources with this additional patch.

The bandwidth rationale follows [Khronos render-pass attachment guidance](https://docs.vulkan.org/samples/latest/samples/performance/render_passes/README.html).
The implementation is reviewed against the pinned local DXVK source; it does
not copy any later Quest donor implementation.

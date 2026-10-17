# Gameplay passthrough

Both independent room-background controls share the Passthrough card. The new
Behind the battlefield control is marked experimental and defaults to Off. Restart game and launcher after
changing it. Passthrough behind flat screens remains a separate setting. Keep the
Android version and existing APK updater unchanged during local trials.

## Renderer path

The OpenXR room underlay sits below a premultiplied stereo projection and the
original opaque HUD quad. Retain OPAQUE environment blend mode. Menus, campaigns,
controller transforms, simulation and network transport are unchanged.

The former transparent-draw replay path caused severe overhead and exercised an
ownership defect in DXVK D3D8 CreateStateBlock. Passing ref() into a temporary Com
added two references while only one new owner was needed. Move the existing
owner instead. The versioned Quest dependency patch includes the lifetime fix;
do not edit generated dependency sources by hand.

The replacement writes independent coverage to color attachment three during the
original fragment invocation. Both fixed-function and PS1.x shader generation
emit coverage after the original alpha test. The existing material output zero,
including destination alpha used by water, remains intact. Coverage has independent
ONE / ONE_MINUS_SRC_ALPHA blending. Opaque geometry and destination-alpha water
write solid coverage; ordinary transparency writes material alpha. Colorless
shadow volumes, multiplicative shadow fills and additive-only light do not add
opaque coverage. Original stencil/depth tests still govern fragment survival.

Use two persistent R8 targets, one for each eye. Each mask writes one byte per
pixel. Together they occupy half the bytes of the former shared RGBA8 target;
per-fragment mask output is one quarter of its former size. These are byte counts,
not FPS predictions. Clear once per eye and append only to the owned world target.
The D3D A8 attachment retains a logical alpha swizzle even though its storage is
R8. Set its logical write mask to alpha, which DXVK remaps to physical red. A
logical red-only mask suppresses storage writes and makes solid geometry show
the room. Do not change the global A8 mapping or the original world's alpha.

Do not overwrite the original eye's material alpha after rendering. The native
WorldComposite handoff reads original RGB plus the corresponding R8 coverage and
writes the acquired OpenXR image layer in one full-resolution draw. It replaces
the old copy/blit plus alpha-only full-eye pass. It supports the existing UNORM
RGBA/BGRA formats without applying gamma conversion or changing UV orientation.
The original Off copy/blit path remains intact. Read source images only after the
DXVK command-stream flush; restore their declared layouts. Use the existing XR
image fence before reusing destinations, without a new per-frame queue-idle wait.
Drain the shared queue before releasing views, framebuffers, descriptors or masks.

No geometry traversal, draw replay, per-object target switching, ALL-state capture
or final D3D alpha-only draw occurs. Off does not query this bridge or
allocate a mask. Unsupported blend contracts latch opaque fallback until graphics
reattachment. Paired-eye completion is required before showing the room.

## Validation limits

Host fixtures exercise production begin/finalize, state and resource cleanup,
mask reuse, the backend blend policy and COM ownership. They are software models;
a separate host GPU test executes the production SPIR-V emitter and independent
attachment blending with both specialization paths. It compiles the backend's
pure format, component-mask and blend conversion functions and exercises them
with empty, partial and opaque coverage backgrounds. This is an extracted-function
fixture, not the complete DXVK device/pipeline. It does not certify the full
D3D8 game path or the Quest driver. Compile
both native profiles from one source and bind the exact dependency patch hashes.
Then measure real game memory over minutes and GPU/frame cost, including water,
particles, cutout trees, TAA edges, HUD/pointer, both eyes, tilt and transitions.
Passthrough has additional compositor/bandwidth cost; no FPS gain is promised.
The Test20 trial exited normally with no reported crash, but the maintainer
confirmed that its active passthrough path substantially reduced FPS and that Off
restored prior performance. That observation is not a controlled timing benchmark.
The first R8/native-handoff trial reported better apparent FPS but incorrect
world transparency. Its timing cannot be accepted as a correct-rendering gain.
The corrected logical alpha write still needs Quest quality, memory and FPS checks.
A later completed PC match remains a runtime gate before public promotion.

Reference: the reviewed GLES XR implementation generates eye coverage in its
fragment shaders rather than replaying transparent draws. This Vulkan adaptation
preserves the current renderer and legacy material alpha.

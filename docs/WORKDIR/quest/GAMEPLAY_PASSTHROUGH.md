# Gameplay passthrough

The independent graphics switch defaults to Off. Restart game and launcher after
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

Use one persistent, eye-sized RGBA8 target shared sequentially between eyes.
Clear it once per eye, append it only while the owned eye target is bound, detach
before the final alpha-only copy, and retain original RGB/depth/stencil and D3D
state. No geometry traversal, draw replay, render-target switching per transparent
object, or per-object ALL-state capture occurs. Off does not query this bridge or
allocate a mask. Unsupported blend contracts latch opaque fallback until graphics
reattachment. Paired-eye completion is required before showing the room.

## Validation limits

Host fixtures exercise production begin/finalize, state and resource cleanup,
mask reuse, the backend blend policy and COM ownership. They are software models;
a separate host GPU test executes the production SPIR-V emitter and independent
attachment blending with both specialization paths. It does not certify the full
D3D8 game path or the Quest driver. Compile
both native profiles from one source and bind the exact dependency patch hashes.
Then measure real game memory over minutes and GPU/frame cost, including water,
particles, cutout trees, TAA edges, HUD/pointer, both eyes, tilt and transitions.
Passthrough has additional compositor/bandwidth cost; no FPS gain is promised.
A later completed PC match remains a runtime gate before public promotion.

Reference: the reviewed GLES XR implementation generates eye coverage in its
fragment shaders rather than replaying transparent draws. This Vulkan adaptation
preserves the current renderer and legacy material alpha.

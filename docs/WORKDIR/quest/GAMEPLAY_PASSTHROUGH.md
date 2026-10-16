# Gameplay passthrough

The optional graphics switch shows the physical room around the battlefield.
It defaults to Off and is independent of passthrough behind flat screens. Restart
the game and launcher after changing it. The Android version and APK updater stay
unchanged for this local trial.

Reuse the existing OpenXR passthrough feature. Composition order is room underlay,
premultiplied stereo projection, then the existing opaque HUD/menu quad. Retain
the runtime's OPAQUE environment blend mode. Flat campaign sequences continue
through the existing screen path. Eye/controller transforms and simulation do not
change.

Do not use blue color keying or reinterpret the legacy destination alpha used by
water. A private, shared-eye-size RGBA texture accumulates alpha coverage from
extra GPU submissions of eligible transparent draws. Geometry/update traversal is
not repeated. Reuse the same vertices, textures, alpha test and depth test;
disable depth/stencil writes and write only mask alpha. Opaque geometry needs no
replay: at completed-eye time, union the mask with pixels whose depth is below
the original clear value. Only then replace the eye's final alpha, after legacy
water/material consumers and sorted draws, before completed-eye depth discard.

Standard transparency covers black smoke as well as colored particles. Additive
RGB remains additive over the room without an opaque particle rectangle. Treat
destination-alpha water surfaces as solid coverage while keeping their original
RGB draw intact. Include filtered tree fringes and direct water/snow submissions;
ignore offscreen reflection targets. Explicitly save/restore the pinned D3D8
adapter's forwarded D3D9 separate-alpha states in addition to its ALL state block.
The fullscreen alpha union follows D3D8 pixel-center conventions.

Both eye masks must finish before the projection is blended over the room. A
missing capability or unsupported draw contract retains opaque presentation;
coverage failure stays latched until graphics reattachment to prevent repeated
room/background flicker. Capture the background decision once for the stereo
pair. Off leaves original draw traversal, eye color and composition unchanged.

Host tests execute the production D3D8 code against a stateful COM/raster fixture:
coverage accumulation, alpha-only writes, RGB/depth/stencil preservation, state
blocks and explicit separate-alpha restoration, eye isolation, water, additive
light, offscreen targets and failure fallback. This is not GPU/headset evidence.
Headset image quality, TAA silhouettes, distortion effects, water, performance and
lifecycle remain test gates. The extra mask allocation and transparent draws have
a cost; no FPS improvement is claimed. Multiplayer transport and logic source are
unchanged; a later completed PC match remains a runtime gate.

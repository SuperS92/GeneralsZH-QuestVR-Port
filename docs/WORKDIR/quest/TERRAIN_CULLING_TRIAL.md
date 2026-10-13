# Terrain eye-view culling trial

Local Test 05 adds **Terrain view culling (experimental)**, default off.
Restart the entire game and launcher after changes. Keep track streaming and
compact HUD transfer on for the comparison, with scale 1.5, shadows on and AA off.
Actual Android version remains 1.4.0.9 / 10409 and the APK updater is unchanged.

Terrain buffers still contain the whole map. Observe the four final positions
of each original cell upload, including interior peaks and triangle flips.
Each patch must have all 1024 cells initialized; incomplete border/padding,
invalid geometry or unknown groups stay on the original path. Bounds expand
during partial writes and reset when buffers are recreated. Lighting-only writes
do not change positions. No vertex-buffer readback or per-eye vertex scan.

For each actual asymmetric eye camera, reject a group only when its complete
bounding box lies outside one outward W3D frustum plane, with a float precision
guard. Keep partially visible groups. Invalid frusta, reflection/custom/wireframe
passes, software-transformed buffers and nonidentity terrain transforms retain
the original path. The base and additional shroud terrain draws use their own
world transform and camera context. Other terrain overlays are unchanged.

Keep original CPU updates, wrapper state application, draw/triangle statistics
and scene advancement. Suppress only direct native indexed terrain submissions,
with a scope restored after each original wrapper call. No simulation, random,
CRC, networking, numeric/compiler or gameplay visibility change. Metrics report
attempted/culled patches and actual retained submissions per eye and pass.

Host checks exercise the actual bounds/classifier against independent triangle
clipping, with grouped patches, interior peaks, partial rewrites, invalid values,
edge guards and asymmetric rotated eyes. They do not certify native renderer
pixels. Headset checks must cover mountains, map edges, quick turns, zoom, both
eyes, shadows, fog and campaign transitions before accepting the setting.
Compare the same saved scene with Off and On in normal 90-second GPU captures.
No performance gain is assumed until the captures show it. A complete PC match
and replay checks remain required before any public promotion.

## First headset result

The stationary Off/On pair shows no clear gain: mean 50.3 / 50.6 FPS, median 51
in both runs, and App GPU median 17.31 / 17.40 ms. About three percent of patches
are skipped across the eyes. Incomplete border groups retain the original path;
coarse group bounds also retain visible groups. No visible defects were reported.
Keep the trial off. The On hardware-counter worker has a Windows console-input
error at shutdown; runtime/native timing is available across the requested
interval. This is one view and does not establish a general performance result.

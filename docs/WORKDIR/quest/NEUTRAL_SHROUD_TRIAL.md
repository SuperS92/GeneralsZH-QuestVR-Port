# Neutral fog pass trial

Local Test 06 adds **Fog-of-war pass (experimental)**, default off. Fully restart
the game and launcher after a change. Keep the Test 05 terrain-view option off,
track streaming and compact HUD on, scale 1.5, shadows on and AA off. Android
version remains 1.4.0.9 / 10409 and the published APK updater is unchanged.

The original shroud data, visibility queries, interpolation, upload frequency
and scene advancement remain unchanged. Observe successful original RGB565
surface copies into the particular fog texture; unknown texels, failed copies
and reset/released resources cannot qualify a region. Build a bounded reusable
prefix map of uploaded white texels, without a GPU readback or disk writes.
Keep observation and draw scopes local to their render thread.

Only explicit ordinary terrain fog draws qualify. Confirm the actual native
texture/vertex/index bindings, fixed-function shader, multiplicative blending,
disabled depth/stencil writes, disabled fog/alpha test, single texture level,
point/linear filtering, identity world and actual view/texture transforms. A
terrain patch needs complete bounds and a successful original grouped vertex
buffer unlock. Ordinary ungrouped locks keep their inherited behavior and do not
qualify for this trial. Keep terrain batching on during the comparison. Compute
its sampling footprint with float-cancellation and two-texel filter guards.
Retain border/unknown/incomplete or nonwhite regions, including gentle fades.

Skip only qualified all-white patches. Submit the original remaining indices
as consecutive runs in the same order, without rebuilding vertex/index buffers
or repeating scene updates. Keep original wrapper statistics and copy-call
accounting; new metrics separately report skipped patches and actual native runs.
No per-draw heap allocation, new gameplay visibility policy or synchronized data.

CPU oracles pass randomized destination copies, unknown/failed/partial uploads,
20,000 float camera-cancellation footprints and all 2,046 retained index masks
for group sizes one through ten. Default/exact option checks pass. A real D3D9
pixel/state fixture through a host-only D3D8 subset adapter compiles, but device
creation is unavailable in the restricted host: its GPU check remains pending.
Do not report exact hardware pixel equivalence from the CPU checks. The test
adapter is not on native game include paths and is not an additional dependency.

Both native profiles and packaging/source/identity checks remain separate.
Headset review must cover both eyes, known fog boundaries, moving units that
reveal/hide terrain, map edges, shadows and campaign transitions. Compare the
same stationary saved Skirmish Off/On using normal 90-second GPU logs. Review
eligibility/failure counters before attributing a gain. No gain is promised;
additional getters and prefix work can offset saved GPU draws. Replay and a
complete live PC match remain required before any public promotion.

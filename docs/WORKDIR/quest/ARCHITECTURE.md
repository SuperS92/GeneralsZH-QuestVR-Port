# Architecture and implementation boundary

`SAGE_USE_OPENXR` defaults to OFF. The ON target is Android arm64 with SDL3,
Vulkan/DXVK and a pinned OpenXR loader. The regular Android target keeps its
Gradle names and app identity when `-PquestVR` is absent. The Quest flavor uses
`com.generalsx.questvr`, display name `Generals ZH Quest VR`, private storage and
package-derived FileProvider authorities. JNI remains in
`com.generalsx.zerohour`, matching SDL and existing upstream native bindings.

## Platform and storage

`QuestOpenXRBootstrap` owns the OpenXR instance, session, controller actions,
spaces, swapchains and frame lifecycle. Initialization precedes Vulkan device
creation. The v0.44 DXVK bridge exposes the runtime-required Vulkan extensions
and forwards existing D3D interop interfaces; it does not replace the renderer.
Copies use the device/queue selected for XR, barriers, completion fences and
acquire/wait/release ordering from the permitted snapshot. Single-sample stereo
is the default. Historical MSAA code is inert and must stay disabled.

The normal mobile paused branch polls XR lifecycle without rendering or stepping
simulation. Each active engine loop brackets exactly one existing upstream
update with begin/end, including exception cleanup. Sleep/wake and tracking
reconnect still require device evidence; this structure alone cannot certify
them.

`QuestImportActivity` requests read access to SAF trees, copies them into an
app-private import directory, checks archive headers/table bounds and selects
validated paths. Zero Hour and Generals remain separate roots. No original
player files are deleted or written. Native user data is private
`UserData/ZeroHour`. Quest rejects a missing requested 60 Hz engine and disables
phone engine overrides/custom drivers; it retains upstream authentication,
signed data-pack checks and protocol security checks.

## Presentation and input

New modules live in `GeneralsMD/Code/Main/QuestVR` and
`GeneralsMD/Code/GameEngineDevice/Source/QuestVR`; shared typed interfaces and pure
math live in `Core/GameEngineDevice/Include/VRDevice`. No duplicate GameLogic,
GameNetwork, GameClient or full GameEngine implementation is imported.

`QuestVRPresentation` prepares the ordinary upstream views, terrain, water,
shadows and particles once, then draws two eye-specific scenes using runtime
pose, IPD and asymmetric frusta. It skips the redundant flat Android world draw.
The scene `On_Frame_Update` runs for the first successful eye only. Distinct
visibility/shadow/particle draws remain per eye. The frame-update flag is reset
after the stereo call. This is a potential deterministic-side-effect boundary:
bone transforms, animations, LOD and visibility can affect legacy logic, so CRC
comparisons with rendering enabled/disabled remain required. No broad donor
bone/LOD cache or timing macro is imported.

`QuestVRInput` maps v0.44 controller gestures to upstream input and command
generation. `QuestVRPlacement` supplies a world-position override to the current
placement translator, which still checks placement legality, costs and creates
the current command messages. It does not directly construct gameplay objects.
The override clears when placement starts/cancels. Synthetic wrist-pointer
coordinates are clamped away from screen edges and neutralized on exit.
Right-stick height arbitration is independent of left-stick panning.

Only the snow draw entry point is suppressed on Quest, before both point-sprite
and quad paths. Snow update and RNG consumption remain upstream. Other particle
effects remain enabled. Current upstream audio uses FFmpeg; the donor dr_libs
reserve/growth optimization is therefore not transplanted.

## Clocks and update cost

Online comparison uses current upstream `SAGE_HIGH_FPS_SIM=ON` (60 Hz). OpenXR
predicted display time, render FPS, headset refresh and simulation ticks are
distinct clocks. The input adapter uses current upstream FramePacer. It never
steps logic once per eye or reinstates donor render-unlock/timebase macros.
Compiler contraction/aliasing/overflow flags, libm bridges, enum values, CRC,
replay layout and name-key allocation are preserved.

Existing diagnostics report eye CPU costs and frame FPS/version. Mean costs do
not establish performance: device acceptance must capture p50/p95/p99, spikes,
GPU timing, sustained thermal behavior and tracking stalls. No 60/72 FPS claim
is made. Campaign movies and scripted camera scenes use a flat presentation; gameplay
returns to stereo. Left-handed options and complete PCVR settings parity remain
outside the tested feature set.

## Front-end screens and navigation

One default-on Quest preference fixes front-end screens in the room and uses
the original camera for the main-menu scene and its 3D intro. The left menu
button recenters the screen; matches return to stereo. Retired trial preferences
are ignored. Online opens the existing GeneralsOnline settings activity from
the Quest navigation, retaining the previous launcher page on return.

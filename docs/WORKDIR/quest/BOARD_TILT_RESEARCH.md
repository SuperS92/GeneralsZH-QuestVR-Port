# Board tilt feasibility

Reviewed 13 October 2026. Research only; no tilt control is implemented.

The shared `QuestVRWorldTransform` already supplies scale, offset and yaw to
`QuestVRPresentation.cpp` and `QuestVRInput.cpp`. Extend this presentation-space
mapping with pitch rather than rotating terrain, units or simulation coordinates.
Both eye cameras and controller ray origins/directions must use the same mapping.
Keep selection, placement previews and generated game commands in original world
coordinates. Existing two-hand zoom measures a horizontal span; adding a tilt
gesture there needs deliberate handling rather than reusing that scalar directly.

A first prototype can use an angle control and reset, with a deliberate pivot
so the battlefield stays within reach. Check stick panning, zoom, follow-camera,
ray intersections and both eyes at large inclinations and near the board edges.
The current upright camera basis and yaw-only head direction need review.

Comfortable lying play also needs screen placement/recentering: OpenXR game-space
recenter and the fixed front-end screen currently take only head yaw. Preserve
the wrist HUD separately; test keyboard, radial menus, pause and transitions.
This makes a complete comfort mode a moderate change, not just one camera angle.

Expect little additional rotation cost, but a changed viewpoint can expose more
geometry. Measure it. Keep scene advancement, simulation, RNG, timing, CRC and
transport unchanged. Camera-driven rendering side effects still require review,
replay checks and a complete PC match before claiming multiplayer compatibility.
Start with the angle prototype; gesture controls and lying-mode recenter can follow.

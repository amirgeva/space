# space

A space simulation 3D game in C++20 and OpenGL 4.3 core.

## Dependencies

You need vcpkg with `VCPKG_ROOT` set. The dependencies (glfw3, glew, glm, stb) are listed in
`vcpkg.json`, and vcpkg installs them automatically the first time you configure.

## Build

```
cmake --preset windows
cmake --build --preset windows-debug      # or windows-release
build\windows\Debug\space.exe
```

You can also open the folder in Visual Studio or VS Code (CMake Tools), which detect `CMakePresets.json`.
The build copies `assets/` and the vcpkg DLLs next to the executable.

## Viewer

`space` generates a ship and shows it. The same seed always generates the same ship.

```
space [--seed N] [--type freighter|station|fighter] [--model file.obj]
      [--screenshot out.png] [--yaw radians] [--pitch radians] [--zoom factor] [--focus -1..1]
```

- Left-drag orbits the camera and the mouse wheel zooms.
- N generates a new random ship, `[` / `]` step to the previous / next seed, and T switches the ship type.
- Space toggles auto-rotate and Esc quits.
- To fly through the scene, use the Up / Down arrows to pitch, Left / Right to roll, and Q / A to fly forward /
  backward. Movement starts slow and speeds up while the key is held. Flight starts from the current view. While flying, left-drag looks around and the wheel moves you
  forward or back. R or Space returns to the orbit view, as does loading another ship.
- `--model` views any .obj file (e.g. `assets/models/cube.obj`) instead of generating a ship.
- `--screenshot` renders one frame to a PNG and exits.

## Ship generator

`shipgen` exports generated ships as .obj/.mtl. It copies the textures into a `textures/`
folder next to the output, so the result opens directly in Blender and similar tools.

```
shipgen [--type freighter|station|fighter] [--seed N] [--count K] [--out ship.obj] [--textures dir]
```

**Logistics freighter / port** is the first ship type. It is built along +Z from stern to bow:

- **Engine block:** a cluster of 1-4 main nozzles plus a ring of small thrusters, an optional
  top cowling, RCS pods, optional radiator panels and frame belts.
- **Neck:** connects the engine block to the hull and carries pipe runs.
- **Main hull:**
  - eight-sided hull with armor belts and greebles (small surface details)
  - side panels with porthole rows, raised trace lines and livery trim
  - docking ports and underside bays
  - a tiered bridge tower with lit window bands, spires, a sensor dome and antenna masts
- **Cargo section:** one of three layouts:
  - coaxial drums with external trusses
  - container racks around a lattice spine
  - rings of domed tanks
- **Bow:** a tapered prow or a docking pod, with antenna spars and nav lights.

Each seed also picks a livery: the hull tint, an accent color and the cargo colors.

**Wheel station** (`--type station`) spins around Z. It is 270-410 units across, so the hub has room
for a hangar that fighters (up to 20 x 26) fly into.

- **Center hub (port of entry):** a hollow many-sided prism with RCS pods, porthole rows, greebles and
  collars where the spokes attach. Ships fly right up to it, so its plating uses high-resolution
  textures (`hub_plating.png`, 128 pixels per unit).
  - **Hangar:** a 4-, 6- or 8-sided interior. Each inner face is a deck (`hangar_deck.png`) 40-46 wide,
    with room for a row of 34 x 34 landing pads (`kPadSize` in `WheelStation.cpp`). The decks are left
    clear for the pads. Girders with light strips run along the corners, and control room windows on
    the back wall look out over each deck. The hangar surfaces glow faintly, standing in for floodlights,
    since the renderer has no shadows or lamps.
  - **Entrance:** a slot in the front wall, wide enough for a fighter with its wings level. It has a
    striped frame, a lit tunnel, rows of lamps, approach light masts and airlock ports for larger ships.
    A force field effect flickers across the slot.
- **Rear:** a service module with radiator fins, and a truss mast with solar array pairs and a dish.
- **Outer ring (living quarters):** one or two rings, either box-section (faceted, greebled) or torus.
  Bulkheads divide it into modules. It has window rows on the outer and side walls, and greenhouse
  glazing facing the hub.
- **Spokes:** transit tubes between hub and ring. They come as a single tube, twin tubes, or a tube
  inside a truss, with bulkhead rings, elevator cars and airlock blocks at the ring.

**Fighter** (`--type fighter`) is a single-seat craft, built along +Z from tail to nose:

- **Fuselage:** a faceted body with an engine section, a mid section and a tapered nose with a
  sensor probe, sometimes in the accent color. Small greebles sit on the top and belly, with
  panel-line relief and optional trim on the sides.
- **Canopy:** dark glass with a sloped windscreen and a frame, and a dorsal fairing behind it.
- **Engines:** one or two nozzles in the tail with a belly intake, or two nacelles at the wing roots.
- **Wings:** swept, delta or forward-swept, with dihedral or anhedral and optional accent tips.
  Some have tail planes as well.
- **Fins:** a single fin or twin canted fins, sometimes with ventral fins below.
- **Weapons:** cannons on the leading edges, missiles on pylons under the wings, and optional
  wingtip missile rails.

## Effects

Effects are objects that draw a visual effect, attached to a model. A generator says where it wants one
by adding an `EffectDesc` (type, placement, size) to `ModelData::effects`. The viewer turns each into an
`Effect` through `EffectFactory` and attaches it to the `Model`, which updates the effects every frame and
draws them after the opaque geometry, each with its own shader and blending.

- **ForceField:** a quad showing a different random window of a tiling blue noise texture
  (`textures/effects/forcefield.png`) every frame, so it flutters. It is blended additively, visible
  from both sides, and fades out towards its edges.

To add an effect type, add a value to `EffectType`, write an `Effect` subclass in `src/effects/` and
create it in `EffectFactory::create`.

## Layout

```
src/
  main.cpp              viewer: orbit and fly cameras, regeneration, screenshots
  tools/shipgen.cpp     command line ship exporter
  core/Window           GLFW window, GL context, input
  core/OrbitCamera      orbit camera
  core/FlyCamera        free-flying camera (pitch, roll, yaw, move)
  effects/              Effect base class, ForceField, EffectFactory
  geometry/             (no OpenGL dependency)
    ModelData           CPU-side model: vertices, indices, materials
    MeshBuilder         procedural primitives: extrusions, revolves, boxes, cylinders, domes, beams
    ObjLoader/ObjWriter Wavefront .obj/.mtl import and export
  procgen/
    Rng                 deterministic RNG (same output on every platform)
    Greebles            surface detail: plates, blocks, vents, pipes, knobs, traces
    ShipMaterials       material palette and liveries
    ShipGenerator       entry point: generateShip(type, seed)
    ShipParts           shared parts: nozzles, docking ports, trusses, dishes, RCS pods
    ships/              one generator per ship type
  render/               Mesh, Texture, TextureCache, Material, Model, Shader
assets/
  shaders/              GLSL shaders (Phong, emissive, ACES tone mapping; force field)
  models/               .obj/.mtl models
  textures/             crate texture; ship/ holds the ship textures, effects/ the effect textures
tools/                  texture generators (python tools/gen_ship_textures.py [names...])
```

To add a ship type, add a value to `ShipType`, write a generator in `procgen/ships/`
and dispatch to it in `generateShip`.

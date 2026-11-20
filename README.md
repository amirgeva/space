# space

A space simulation 3D game in C++20 and OpenGL 4.3 core.

## Dependencies

You need vcpkg in classic mode with `VCPKG_ROOT` set. Install the dependencies with:

```
vcpkg install glfw3 glew glm stb --triplet x64-windows
```

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
space [--seed N] [--type freighter|station] [--model file.obj]
      [--screenshot out.png] [--yaw radians] [--pitch radians] [--zoom factor] [--focus -1..1]
```

- Left-drag orbits the camera and the mouse wheel zooms.
- N generates a new random ship, `[` / `]` step to the previous / next seed, and T switches the ship type.
- Space toggles auto-rotate and Esc quits.
- `--model` views any .obj file (e.g. `assets/models/cube.obj`) instead of generating a ship.
- `--screenshot` renders one frame to a PNG and exits.

## Ship generator

`shipgen` exports generated ships as .obj/.mtl. It copies the textures into a `textures/`
folder next to the output, so the result opens directly in Blender and similar tools.

```
shipgen [--type freighter|station] [--seed N] [--count K] [--out ship.obj] [--textures dir]
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

**Wheel station** (`--type station`) spins around Z:

- **Center hub (port of entry):** a many-sided prism covered in greebles, with RCS pods, porthole rows,
  and collars where the spokes attach. At the front is a docking collar with:
  - a recessed, lit docking bay with a landing-light ring
  - radial docking ports
  - approach light masts
- **Rear:** a service module with radiator fins, and a truss mast with solar array pairs and a dish.
- **Outer ring (living quarters):** one or two rings, either box-section (faceted, greebled) or torus.
  Bulkheads divide it into modules. It has window rows on the outer and side walls, and greenhouse
  glazing facing the hub.
- **Spokes:** transit tubes between hub and ring. They come as a single tube, twin tubes, or a tube
  inside a truss, with bulkhead rings, elevator cars and airlock blocks at the ring.

## Layout

```
src/
  main.cpp              viewer: orbit camera, regeneration, screenshots
  tools/shipgen.cpp     command line ship exporter
  core/Window           GLFW window, GL context, input
  core/OrbitCamera      orbit camera
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
  shaders/              GLSL shaders (Phong, emissive, ACES tone mapping)
  models/               .obj/.mtl models
  textures/             crate texture; ship/ holds the ship textures
tools/                  texture generators (python tools/gen_ship_textures.py)
```

To add a ship type, add a value to `ShipType`, write a generator in `procgen/ships/`
and dispatch to it in `generateShip`.

# Box3D on the web (WebAssembly + three.js)

A browser demo that compiles the Box3D engine to WebAssembly with Emscripten,
exposes its C API to JavaScript via [embind](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html),
and drives an instanced three.js scene — 1,050 rigid bodies (cubes + spheres)
raining into a pit and piling up. Inspired by the three.js
[`physics_rapier_instancing`](https://threejs.org/examples/?q=phys#physics_rapier_instancing)
example, but the simulation is Box3D.

![screenshot](https://box2d.org/images/logo.svg)

## The engine is built unmodified

Nothing under `src/` or `include/` is changed. Box3D's C17 core compiles to
`wasm32` as-is, and the existing `src/CMakeLists.txt` already selects the wasm
SIMD path (`-msimd128 -msse2`) under Emscripten. Everything web-specific lives in
this `web/` directory:

| file | purpose |
| --- | --- |
| `src/box3d_bindings.cpp` | embind layer: marshals Box3D's by-value structs, ids, enums, and functions to JS. |
| `CMakeLists.txt` | consumes the engine as a subproject (library only) and links the bindings into one ES6 WASM module. |
| `build.sh` | thin wrapper around `emcmake cmake` + `cmake --build`. |
| `config.js` | scene + simulation constants shared by both threads. |
| `physics.worker.js` | Web Worker: owns the WASM module + world, runs the fixed-timestep loop, streams transform snapshots. |
| `index.html` / `main.js` | the three.js render thread — **all scene and simulation logic is authored in JavaScript** against the exposed API. |

## Build

Activate the Emscripten SDK, then build:

```bash
source /path/to/emsdk/emsdk_env.sh
cd web
./build.sh            # → web/box3d.js + web/box3d.wasm
```

The build is single-threaded (`workerCount = 1`), so it needs no `-pthread` and no
COOP/COEP response headers — it serves from any static host.

## Run

```bash
cd web
python3 -m http.server 8099
# open http://localhost:8099/index.html
```

`three.js` is loaded from a CDN via an import map (see `index.html`); no npm
install required. Controls: drag to orbit, scroll to zoom, click the pile to
detonate an explosion, press **R** to reset.

## How JS drives Box3D

`box3d_bindings.cpp` exposes the simulation surface — world/body/shape lifecycle
and tuning, the math and definition structs, ids, enums, explosions, and math
helpers. The demo builds the whole scene in `main.js`:

```js
const B = await createBox3D();

const wd = B.DefaultWorldDef();
wd.gravity = { x: 0, y: -20, z: 0 };
const world = B.CreateWorld(wd);

const bd = B.DefaultBodyDef();
bd.type = B.BodyType.dynamic;
bd.position = { x: 0, y: 10, z: 0 };
const body = B.CreateBody(world, bd);

const sd = B.DefaultShapeDef();
sd.density = 1.0;
B.CreateBoxShape(body, sd, 0.5, 0.5, 0.5);

B.World_Step(world, 1 / 60, 4);
const xf = B.Body_GetTransform(body);   // { p:{x,y,z}, q:{v:{x,y,z}, s} }
```

### Threading model

Physics runs off the main thread in a Web Worker (`physics.worker.js`), so a slow
solve step never stalls rendering or input:

```
physics.worker.js  ──(transform snapshot, transferred)──▶  main.js
   fixed 60 Hz                                              render @ display rate
   Box3D world                                              interpolate + draw
        ▲                                                        │
        └──────────── { explode, reset } ◀───────────────────────┘
```

The worker steps at a fixed 60 Hz using a real-time accumulator and, after each
step, snapshots every body transform into a fresh `Float32Array` (transferred to
the main thread zero-copy). Reading 1,050 transforms one embind call at a time
would dominate the tick, so the worker keeps its body ids in a `B.BodyIdVector`
and calls `B.WriteTransforms(vec, ptr, stride)` once — the loop stays inside WASM
and fills the buffer, read back through `Module.HEAPF32`.

### Interpolation

The render thread keeps the two most recent snapshots and, each animation frame,
computes `alpha = (now − snapshotArrival) / (1000/60)` clamped to `[0, 1]`. It
interpolates every body — `Vector3.lerpVectors` for position,
`Quaternion.slerpQuaternions` for rotation — from the previous state toward the
current one. Rendering effectively trails physics by one step (~16 ms), which buys
perfectly smooth motion at any display rate (e.g. 144 Hz) regardless of the 60 Hz
physics cadence. On reset the previous snapshot is dropped so the scene snaps to
the rebuilt world instead of interpolating across it.

### Notes / limitations

- 64-bit fields (collision filter bits, user material ids, explosion mask bits)
  are kept off the JS surface — embind does not marshal them without BigInt, and a
  zeroed filter would silently disable collisions. The shape/explosion thunks
  restore the engine defaults, so JS callers get correct filtering automatically.
  Add BigInt-based accessors if custom filtering is needed.
- The binding covers the core simulation API; extending it to joints, queries,
  sensors, meshes, or the recording API is a matter of adding more `function(...)`
  and `value_object<...>` lines.

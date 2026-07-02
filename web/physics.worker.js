// Physics thread. Owns the Box3D WASM module and world, runs a real-time
// fixed-timestep loop, and posts each new body-transform snapshot to the render
// thread. Nothing here touches the DOM or three.js.
import createBox3D from './box3d.js';
import * as C from './config.js';

const B = await createBox3D();

const V = (x, y, z) => ({ x, y, z });

// Deterministic PRNG so the pile is reproducible across resets.
let seed = 0x1234567;
function rnd() {
	seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0;
	return (seed & 0xffffff) / 0xffffff;
}
const rr = (a, b) => a + (b - a) * rnd();

let world = null;
let bodies = null;                                 // B.BodyIdVector
const bufPtr = B.AllocFloats(C.TOTAL * C.STRIDE);  // scratch heap buffer

function staticBox(cx, cy, cz, hx, hy, hz) {
	const bd = B.DefaultBodyDef();
	bd.type = B.BodyType.static;
	bd.position = V(cx, cy, cz);
	const body = B.CreateBody(world, bd);
	const sd = B.DefaultShapeDef();
	sd.baseMaterial.friction = 0.6;
	B.CreateBoxShape(body, sd, hx, hy, hz);
}

function randomQuat() {
	let ax = V(rr(-1, 1), rr(-1, 1), rr(-1, 1));
	const len = B.Length(ax);
	if (len < 1e-4) return { v: V(0, 0, 0), s: 1 };
	ax = V(ax.x / len, ax.y / len, ax.z / len);
	return B.QuatFromAxisAngle(ax, rr(0, 2 * Math.PI));
}

function buildWorld() {
	if (world) {
		B.DestroyWorld(world);
		bodies.delete();
	}
	seed = 0x1234567;

	const wd = B.DefaultWorldDef();
	wd.gravity = V(0, C.GRAVITY_Y, 0);
	wd.workerCount = 1;
	wd.enableSleep = true;
	world = B.CreateWorld(wd);

	const t = 0.5;
	staticBox(0, -t, 0, C.EXTENT, t, C.EXTENT);
	staticBox(C.EXTENT + t, C.EXTENT, 0, t, C.EXTENT, C.EXTENT);
	staticBox(-C.EXTENT - t, C.EXTENT, 0, t, C.EXTENT, C.EXTENT);
	staticBox(0, C.EXTENT, C.EXTENT + t, C.EXTENT, C.EXTENT, t);
	staticBox(0, C.EXTENT, -C.EXTENT - t, C.EXTENT, C.EXTENT, t);

	bodies = new B.BodyIdVector();
	const spawnHalf = C.EXTENT - 1.2;
	let y = 2;
	for (let i = 0; i < C.TOTAL; i++) {
		const isBox = i < C.BOX_COUNT;
		const bd = B.DefaultBodyDef();
		bd.type = B.BodyType.dynamic;
		bd.position = V(rr(-spawnHalf, spawnHalf), y, rr(-spawnHalf, spawnHalf));
		bd.rotation = randomQuat();
		bd.angularVelocity = V(rr(-2, 2), rr(-2, 2), rr(-2, 2));
		const body = B.CreateBody(world, bd);

		const sd = B.DefaultShapeDef();
		sd.density = 1.0;
		sd.baseMaterial.friction = 0.6;
		sd.baseMaterial.restitution = 0.0;
		if (isBox) {
			B.CreateBoxShape(body, sd, C.BOX_HALF, C.BOX_HALF, C.BOX_HALF);
		} else {
			sd.baseMaterial.rollingResistance = 0.3;
			B.CreateSphereShape(body, sd, { center: V(0, 0, 0), radius: C.SPHERE_R });
		}
		bodies.push_back(body);
		if ((i % 24) === 23) y += 1.6;
	}
}

// Snapshot the current world into a fresh buffer and hand it to the render
// thread (transferred, zero-copy). `reset` tells the render thread to snap
// rather than interpolate from the previous (now stale) world.
function postSnapshot(stepMs, reset) {
	B.WriteTransforms(bodies, bufPtr, C.STRIDE);
	const n = C.TOTAL * C.STRIDE;
	const out = new Float32Array(n);
	const base = bufPtr >> 2;
	out.set(B.HEAPF32.subarray(base, base + n));
	postMessage(
		{ type: 'state', buf: out.buffer, reset, stepMs, awake: B.World_GetAwakeBodyCount(world) },
		[out.buffer]
	);
}

buildWorld();
let resetFlag = true; // first snapshot after (re)build is a snap, not an interp

// Real-time accumulator loop. A short self-scheduled delay gives the accumulator
// fine resolution so steps land close to the fixed 60 Hz cadence.
let last = performance.now();
let acc = 0;
let stepMs = 0;

function loop() {
	const now = performance.now();
	acc += (now - last) / 1000;
	last = now;

	let steps = 0;
	const t0 = performance.now();
	while (acc >= C.FIXED_DT && steps < C.MAX_STEPS) {
		B.World_Step(world, C.FIXED_DT, C.SUB_STEPS);
		acc -= C.FIXED_DT;
		steps++;
	}
	if (acc > C.FIXED_DT) acc = 0; // shed backlog we couldn't keep up with
	if (steps > 0) stepMs = (performance.now() - t0) / steps;

	if (steps > 0 || resetFlag) {
		postSnapshot(stepMs, resetFlag);
		resetFlag = false;
	}
	setTimeout(loop, 4);
}
loop();

onmessage = (e) => {
	const m = e.data;
	if (!world) return;
	if (m.type === 'explode') {
		const ed = B.DefaultExplosionDef();
		ed.position = V(m.x, m.y, m.z);
		ed.radius = m.radius;
		ed.falloff = m.falloff;
		ed.impulsePerArea = m.impulse;
		B.World_Explode(world, ed);
	} else if (m.type === 'reset') {
		buildWorld();
		resetFlag = true;
	}
};

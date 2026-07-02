import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import * as C from './config.js';

// Render thread. No physics here — a Web Worker owns the Box3D simulation and
// streams body-transform snapshots; this thread renders at display rate and
// interpolates each body between the two most recent physics states.
const $ = (id) => document.getElementById(id);

const worker = new Worker(new URL('./physics.worker.js', import.meta.url), { type: 'module' });

// Latest two physics snapshots. We render `prev -> cur` over one physics step's
// worth of wall time, so cur is reached just as the next snapshot arrives.
let prevBuf = null;
let curBuf = null;
let lastMsgTime = 0;
let stepMs = 0;
let awake = 0;

worker.onmessage = (e) => {
	const m = e.data;
	if (m.type !== 'state') return;
	const arr = new Float32Array(m.buf);
	prevBuf = m.reset ? null : curBuf; // snap on reset instead of interpolating from a stale world
	curBuf = arr;
	lastMsgTime = performance.now();
	stepMs = m.stepMs;
	awake = m.awake;
};

$('s-bodies').textContent = C.TOTAL.toLocaleString();

// ---------------------------------------------------------------------------
// three.js setup
// ---------------------------------------------------------------------------
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.1;
$('app').appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0b0e14);
scene.fog = new THREE.Fog(0x0b0e14, 40, 90);

const camera = new THREE.PerspectiveCamera(55, innerWidth / innerHeight, 0.1, 500);
camera.position.set(C.EXTENT * 1.7, C.EXTENT * 1.6, C.EXTENT * 2.1);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.target.set(0, C.EXTENT * 0.35, 0);
controls.maxPolarAngle = Math.PI * 0.495;
controls.minDistance = 8;
controls.maxDistance = 120;

scene.add(new THREE.HemisphereLight(0x9fb4ff, 0x30241a, 0.55));
const sun = new THREE.DirectionalLight(0xfff2d8, 2.4);
sun.position.set(18, 30, 14);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
const sc = sun.shadow.camera;
sc.near = 1; sc.far = 90;
sc.left = -C.EXTENT * 2; sc.right = C.EXTENT * 2;
sc.top = C.EXTENT * 2; sc.bottom = -C.EXTENT * 2;
sun.shadow.bias = -0.0002;
scene.add(sun);

const floor = new THREE.Mesh(
	new THREE.BoxGeometry(C.EXTENT * 2, 1, C.EXTENT * 2),
	new THREE.MeshStandardMaterial({ color: 0x1b2130, roughness: 0.95 })
);
floor.position.y = -0.5;
floor.receiveShadow = true;
scene.add(floor);

const cage = new THREE.LineSegments(
	new THREE.EdgesGeometry(new THREE.BoxGeometry(C.EXTENT * 2, C.EXTENT, C.EXTENT * 2)),
	new THREE.LineBasicMaterial({ color: 0x3a4a63, transparent: true, opacity: 0.5 })
);
cage.position.y = C.EXTENT * 0.5;
scene.add(cage);

const material = new THREE.MeshStandardMaterial({ roughness: 0.45, metalness: 0.1 });
const boxMesh = new THREE.InstancedMesh(new THREE.BoxGeometry(C.BOX_HALF * 2, C.BOX_HALF * 2, C.BOX_HALF * 2), material, C.BOX_COUNT);
const sphereMesh = new THREE.InstancedMesh(new THREE.SphereGeometry(C.SPHERE_R, 18, 12), material, C.SPHERE_COUNT);
for (const m of [boxMesh, sphereMesh]) {
	m.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
	m.castShadow = true;
	m.receiveShadow = true;
	m.frustumCulled = false;
	scene.add(m);
}

const color = new THREE.Color();
for (let i = 0; i < C.BOX_COUNT; i++) { color.setHSL(0.05 + 0.12 * Math.random(), 0.7, 0.55); boxMesh.setColorAt(i, color); }
for (let i = 0; i < C.SPHERE_COUNT; i++) { color.setHSL(0.55 + 0.15 * Math.random(), 0.65, 0.6); sphereMesh.setColorAt(i, color); }
boxMesh.instanceColor.needsUpdate = true;
sphereMesh.instanceColor.needsUpdate = true;

// ---------------------------------------------------------------------------
// Interpolated instance update
// ---------------------------------------------------------------------------
const dummy = new THREE.Object3D();
const pPrev = new THREE.Vector3();
const pCur = new THREE.Vector3();
const qPrev = new THREE.Quaternion();
const qCur = new THREE.Quaternion();

function syncInstances(alpha) {
	if (!curBuf) return;
	const S = C.STRIDE;
	const interp = prevBuf !== null;
	for (let i = 0; i < C.TOTAL; i++) {
		const o = i * S;
		if (interp) {
			pPrev.set(prevBuf[o], prevBuf[o + 1], prevBuf[o + 2]);
			pCur.set(curBuf[o], curBuf[o + 1], curBuf[o + 2]);
			qPrev.set(prevBuf[o + 3], prevBuf[o + 4], prevBuf[o + 5], prevBuf[o + 6]);
			qCur.set(curBuf[o + 3], curBuf[o + 4], curBuf[o + 5], curBuf[o + 6]);
			dummy.position.lerpVectors(pPrev, pCur, alpha);
			dummy.quaternion.slerpQuaternions(qPrev, qCur, alpha);
		} else {
			dummy.position.set(curBuf[o], curBuf[o + 1], curBuf[o + 2]);
			dummy.quaternion.set(curBuf[o + 3], curBuf[o + 4], curBuf[o + 5], curBuf[o + 6]);
		}
		dummy.updateMatrix();
		if (i < C.BOX_COUNT) boxMesh.setMatrixAt(i, dummy.matrix);
		else sphereMesh.setMatrixAt(i - C.BOX_COUNT, dummy.matrix);
	}
	boxMesh.instanceMatrix.needsUpdate = true;
	sphereMesh.instanceMatrix.needsUpdate = true;
}

const DT_MS = C.FIXED_DT * 1000;
let last = performance.now();
let fpsEMA = 60;
let frame = 0;
let revealed = false;

function tick(now) {
	requestAnimationFrame(tick);
	const wall = (now - last) / 1000;
	last = now;
	fpsEMA += ((1 / Math.max(wall, 1e-3)) - fpsEMA) * 0.08;

	// Fraction through the current physics step, based on wall time since the
	// latest snapshot arrived. Clamped so a late snapshot holds at `cur`.
	const alpha = prevBuf !== null ? Math.min((now - lastMsgTime) / DT_MS, 1) : 1;

	syncInstances(alpha);
	controls.update();
	renderer.render(scene, camera);

	if (!revealed && curBuf) { $('overlay').classList.add('hidden'); revealed = true; }

	if ((frame++ & 15) === 0) {
		$('s-awake').textContent = awake.toLocaleString();
		$('s-step').textContent = stepMs.toFixed(2) + ' ms';
		$('s-fps').textContent = fpsEMA.toFixed(0);
	}
}

// ---------------------------------------------------------------------------
// Interaction (raycast on the render thread, physics call sent to the worker)
// ---------------------------------------------------------------------------
const raycaster = new THREE.Raycaster();
const ndc = new THREE.Vector2();

renderer.domElement.addEventListener('pointerdown', (e) => {
	if (e.button !== 0) return;
	ndc.x = (e.clientX / innerWidth) * 2 - 1;
	ndc.y = -(e.clientY / innerHeight) * 2 + 1;
	raycaster.setFromCamera(ndc, camera);
	const hits = raycaster.intersectObjects([boxMesh, sphereMesh, floor], false);
	if (hits.length) {
		const p = hits[0].point;
		worker.postMessage({ type: 'explode', x: p.x, y: p.y, z: p.z, radius: 6.0, falloff: 3.0, impulse: 40.0 });
	}
});

addEventListener('keydown', (e) => {
	if (e.key === 'r' || e.key === 'R') worker.postMessage({ type: 'reset' });
});

addEventListener('resize', () => {
	camera.aspect = innerWidth / innerHeight;
	camera.updateProjectionMatrix();
	renderer.setSize(innerWidth, innerHeight);
});

requestAnimationFrame(tick);

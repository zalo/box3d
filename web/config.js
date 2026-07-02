// Scene + simulation constants shared by the render thread (main.js) and the
// physics thread (physics.worker.js).
export const BOX_COUNT = 700;
export const SPHERE_COUNT = 350;
export const TOTAL = BOX_COUNT + SPHERE_COUNT;

export const EXTENT = 10;      // half-width of the containing pit, meters
export const BOX_HALF = 0.5;
export const SPHERE_R = 0.5;

export const FIXED_DT = 1 / 30; // physics timestep, seconds (30 Hz)
export const SUB_STEPS = 8;     // 1/30/8 == 1/240 s: same integration granularity as 60 Hz / 4
export const MAX_STEPS = 4;     // per worker tick, before shedding backlog

export const GRAVITY_Y = -20;

// Floats per body in the transform buffer: px,py,pz, qx,qy,qz,qw, awake.
export const STRIDE = 8;

// SPDX-License-Identifier: MIT
//
// box3d_bindings.cpp — Emscripten/embind bindings that expose the Box3D C API to
// JavaScript. The engine is compiled unmodified; this file only marshals its
// public types and functions across the JS boundary so the whole simulation can
// be driven from JS (see main.js for the demo built on top of it).
//
// Box3D passes small structs (vectors, quaternions, ids, definitions) by value.
// embind's value_object marshals those to/from plain JS objects. The few API
// functions that take pointers to definitions are wrapped in by-value thunks so
// JS never has to manage heap structs by hand.

#include <emscripten/bind.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "box3d/box3d.h"
#include "box3d/collision.h"
#include "box3d/math_functions.h"
#include "box3d/types.h"

using namespace emscripten;

// ---------------------------------------------------------------------------
// Thunks: pointer-taking constructors become by-value calls.
// ---------------------------------------------------------------------------
static b3WorldId CreateWorld( b3WorldDef def ) { return b3CreateWorld( &def ); }
static b3BodyId CreateBody( b3WorldId w, b3BodyDef def ) { return b3CreateBody( w, &def ); }

// 64-bit fields (collision filter bits, material and mask ids) are kept off the JS
// boundary — embind does not marshal them cleanly without BigInt, and a zeroed
// filter would disable all collisions. The shape thunks restore the engine's
// default filter so JS callers get correct filtering for free.
static b3ShapeId CreateSphereShape( b3BodyId b, b3ShapeDef def, b3Sphere s )
{
	def.filter = b3DefaultFilter();
	return b3CreateSphereShape( b, &def, &s );
}
static b3ShapeId CreateCapsuleShape( b3BodyId b, b3ShapeDef def, b3Capsule c )
{
	def.filter = b3DefaultFilter();
	return b3CreateCapsuleShape( b, &def, &c );
}

// b3BoxHull carries variable-length data off its tail, so it cannot be a
// value_object. Build it here and hand the embedded hull to the C API.
static b3ShapeId CreateBoxShape( b3BodyId b, b3ShapeDef def, float hx, float hy, float hz )
{
	def.filter = b3DefaultFilter();
	b3BoxHull hull = b3MakeBoxHull( hx, hy, hz );
	return b3CreateHullShape( b, &def, &hull.base );
}

static void WorldExplode( b3WorldId w, b3ExplosionDef d )
{
	d.maskBits = b3DefaultExplosionDef().maskBits; // affect all shapes
	b3World_Explode( w, &d );
}

// ---------------------------------------------------------------------------
// Inline math helpers are not linkable symbols, so re-expose the ones JS wants.
// ---------------------------------------------------------------------------
static b3Quat QuatFromAxisAngle( b3Vec3 axis, float radians ) { return b3MakeQuatFromAxisAngle( axis, radians ); }
static b3Quat MulQuat( b3Quat a, b3Quat b ) { return b3MulQuat( a, b ); }
static b3Quat NormalizeQuat( b3Quat q ) { return b3NormalizeQuat( q ); }
static b3Vec3 RotateVector( b3Quat q, b3Vec3 v ) { return b3RotateVector( q, v ); }
static b3Vec3 AddV( b3Vec3 a, b3Vec3 b ) { return b3Add( a, b ); }
static b3Vec3 SubV( b3Vec3 a, b3Vec3 b ) { return b3Sub( a, b ); }
static b3Vec3 MulSV( float s, b3Vec3 a ) { return b3MulSV( s, a ); }
static float Length( b3Vec3 v ) { return b3Length( v ); }

// ---------------------------------------------------------------------------
// Batch transform readback. JS keeps its body ids in a BodyIdVector and calls
// this once per frame; the loop stays inside wasm and writes a flat float buffer
// that JS reads via Module.HEAPF32 — one boundary crossing instead of thousands.
// Layout per body: [px,py,pz, qx,qy,qz,qw] and (if stride > 7) awake flag.
// ---------------------------------------------------------------------------
static uintptr_t AllocFloats( int count ) { return (uintptr_t)std::malloc( (size_t)count * sizeof( float ) ); }
static void FreeBuffer( uintptr_t ptr ) { std::free( (void*)ptr ); }

static void WriteTransforms( const std::vector<b3BodyId>& bodies, uintptr_t outPtr, int stride )
{
	float* out = (float*)outPtr;
	for ( size_t i = 0; i < bodies.size(); ++i )
	{
		b3Transform xf = b3Body_GetTransform( bodies[i] );
		float* o = out + i * (size_t)stride;
		o[0] = xf.p.x;
		o[1] = xf.p.y;
		o[2] = xf.p.z;
		o[3] = xf.q.v.x;
		o[4] = xf.q.v.y;
		o[5] = xf.q.v.z;
		o[6] = xf.q.s;
		if ( stride > 7 )
		{
			o[7] = b3Body_IsAwake( bodies[i] ) ? 1.0f : 0.0f;
		}
	}
}

EMSCRIPTEN_BINDINGS( box3d )
{
	// --- math ---
	value_object<b3Vec3>( "Vec3" )
		.field( "x", &b3Vec3::x )
		.field( "y", &b3Vec3::y )
		.field( "z", &b3Vec3::z );

	value_object<b3Quat>( "Quat" )
		.field( "v", &b3Quat::v )
		.field( "s", &b3Quat::s );

	value_object<b3Transform>( "Transform" )
		.field( "p", &b3Transform::p )
		.field( "q", &b3Transform::q );

	value_object<b3AABB>( "AABB" )
		.field( "lowerBound", &b3AABB::lowerBound )
		.field( "upperBound", &b3AABB::upperBound );

	// --- ids ---
	value_object<b3WorldId>( "WorldId" )
		.field( "index1", &b3WorldId::index1 )
		.field( "generation", &b3WorldId::generation );

	value_object<b3BodyId>( "BodyId" )
		.field( "index1", &b3BodyId::index1 )
		.field( "world0", &b3BodyId::world0 )
		.field( "generation", &b3BodyId::generation );

	value_object<b3ShapeId>( "ShapeId" )
		.field( "index1", &b3ShapeId::index1 )
		.field( "world0", &b3ShapeId::world0 )
		.field( "generation", &b3ShapeId::generation );

	register_vector<b3BodyId>( "BodyIdVector" );

	// --- enums ---
	enum_<b3BodyType>( "BodyType" )
		.value( "static", b3_staticBody )
		.value( "kinematic", b3_kinematicBody )
		.value( "dynamic", b3_dynamicBody );

	enum_<b3ShapeType>( "ShapeType" )
		.value( "capsule", b3_capsuleShape )
		.value( "compound", b3_compoundShape )
		.value( "height", b3_heightShape )
		.value( "hull", b3_hullShape )
		.value( "mesh", b3_meshShape )
		.value( "sphere", b3_sphereShape );

	// --- config / definition structs ---
	// Note: 64-bit fields (b3Filter bits, userMaterialId, explosion maskBits) are
	// intentionally omitted from the JS surface; see the shape/explosion thunks.
	value_object<b3SurfaceMaterial>( "SurfaceMaterial" )
		.field( "friction", &b3SurfaceMaterial::friction )
		.field( "restitution", &b3SurfaceMaterial::restitution )
		.field( "rollingResistance", &b3SurfaceMaterial::rollingResistance )
		.field( "tangentVelocity", &b3SurfaceMaterial::tangentVelocity )
		.field( "customColor", &b3SurfaceMaterial::customColor );

	value_object<b3MotionLocks>( "MotionLocks" )
		.field( "linearX", &b3MotionLocks::linearX )
		.field( "linearY", &b3MotionLocks::linearY )
		.field( "linearZ", &b3MotionLocks::linearZ )
		.field( "angularX", &b3MotionLocks::angularX )
		.field( "angularY", &b3MotionLocks::angularY )
		.field( "angularZ", &b3MotionLocks::angularZ );

	value_object<b3WorldDef>( "WorldDef" )
		.field( "gravity", &b3WorldDef::gravity )
		.field( "restitutionThreshold", &b3WorldDef::restitutionThreshold )
		.field( "hitEventThreshold", &b3WorldDef::hitEventThreshold )
		.field( "contactHertz", &b3WorldDef::contactHertz )
		.field( "contactDampingRatio", &b3WorldDef::contactDampingRatio )
		.field( "contactSpeed", &b3WorldDef::contactSpeed )
		.field( "maximumLinearSpeed", &b3WorldDef::maximumLinearSpeed )
		.field( "enableSleep", &b3WorldDef::enableSleep )
		.field( "enableContinuous", &b3WorldDef::enableContinuous )
		.field( "workerCount", &b3WorldDef::workerCount )
		.field( "internalValue", &b3WorldDef::internalValue );

	value_object<b3BodyDef>( "BodyDef" )
		.field( "type", &b3BodyDef::type )
		.field( "position", &b3BodyDef::position )
		.field( "rotation", &b3BodyDef::rotation )
		.field( "linearVelocity", &b3BodyDef::linearVelocity )
		.field( "angularVelocity", &b3BodyDef::angularVelocity )
		.field( "linearDamping", &b3BodyDef::linearDamping )
		.field( "angularDamping", &b3BodyDef::angularDamping )
		.field( "gravityScale", &b3BodyDef::gravityScale )
		.field( "sleepThreshold", &b3BodyDef::sleepThreshold )
		.field( "motionLocks", &b3BodyDef::motionLocks )
		.field( "enableSleep", &b3BodyDef::enableSleep )
		.field( "isAwake", &b3BodyDef::isAwake )
		.field( "isBullet", &b3BodyDef::isBullet )
		.field( "isEnabled", &b3BodyDef::isEnabled )
		.field( "allowFastRotation", &b3BodyDef::allowFastRotation )
		.field( "enableContactRecycling", &b3BodyDef::enableContactRecycling )
		.field( "internalValue", &b3BodyDef::internalValue );

	value_object<b3ShapeDef>( "ShapeDef" )
		.field( "baseMaterial", &b3ShapeDef::baseMaterial )
		.field( "density", &b3ShapeDef::density )
		.field( "explosionScale", &b3ShapeDef::explosionScale )
		.field( "enableCustomFiltering", &b3ShapeDef::enableCustomFiltering )
		.field( "isSensor", &b3ShapeDef::isSensor )
		.field( "enableSensorEvents", &b3ShapeDef::enableSensorEvents )
		.field( "enableContactEvents", &b3ShapeDef::enableContactEvents )
		.field( "enableHitEvents", &b3ShapeDef::enableHitEvents )
		.field( "enablePreSolveEvents", &b3ShapeDef::enablePreSolveEvents )
		.field( "invokeContactCreation", &b3ShapeDef::invokeContactCreation )
		.field( "updateBodyMass", &b3ShapeDef::updateBodyMass )
		.field( "internalValue", &b3ShapeDef::internalValue );

	value_object<b3Sphere>( "Sphere" )
		.field( "center", &b3Sphere::center )
		.field( "radius", &b3Sphere::radius );

	value_object<b3Capsule>( "Capsule" )
		.field( "center1", &b3Capsule::center1 )
		.field( "center2", &b3Capsule::center2 )
		.field( "radius", &b3Capsule::radius );

	value_object<b3ExplosionDef>( "ExplosionDef" )
		.field( "position", &b3ExplosionDef::position )
		.field( "radius", &b3ExplosionDef::radius )
		.field( "falloff", &b3ExplosionDef::falloff )
		.field( "impulsePerArea", &b3ExplosionDef::impulsePerArea );

	// --- defaults ---
	function( "DefaultWorldDef", &b3DefaultWorldDef );
	function( "DefaultBodyDef", &b3DefaultBodyDef );
	function( "DefaultShapeDef", &b3DefaultShapeDef );
	function( "DefaultSurfaceMaterial", &b3DefaultSurfaceMaterial );
	function( "DefaultExplosionDef", &b3DefaultExplosionDef );

	// --- world ---
	function( "CreateWorld", &CreateWorld );
	function( "DestroyWorld", &b3DestroyWorld );
	function( "World_IsValid", &b3World_IsValid );
	function( "World_Step", &b3World_Step );
	function( "World_SetGravity", &b3World_SetGravity );
	function( "World_GetGravity", &b3World_GetGravity );
	function( "World_Explode", &WorldExplode );
	function( "World_GetAwakeBodyCount", &b3World_GetAwakeBodyCount );
	function( "World_EnableSleeping", &b3World_EnableSleeping );
	function( "World_EnableContinuous", &b3World_EnableContinuous );
	function( "World_GetBounds", &b3World_GetBounds );
	function( "World_SetContactTuning", &b3World_SetContactTuning );

	// --- body ---
	function( "CreateBody", &CreateBody );
	function( "DestroyBody", &b3DestroyBody );
	function( "Body_IsValid", &b3Body_IsValid );
	function( "Body_GetType", &b3Body_GetType );
	function( "Body_SetType", &b3Body_SetType );
	function( "Body_GetPosition", &b3Body_GetPosition );
	function( "Body_GetRotation", &b3Body_GetRotation );
	function( "Body_GetTransform", &b3Body_GetTransform );
	function( "Body_SetTransform", &b3Body_SetTransform );
	function( "Body_GetLinearVelocity", &b3Body_GetLinearVelocity );
	function( "Body_SetLinearVelocity", &b3Body_SetLinearVelocity );
	function( "Body_GetAngularVelocity", &b3Body_GetAngularVelocity );
	function( "Body_SetAngularVelocity", &b3Body_SetAngularVelocity );
	function( "Body_ApplyForce", &b3Body_ApplyForce );
	function( "Body_ApplyForceToCenter", &b3Body_ApplyForceToCenter );
	function( "Body_ApplyTorque", &b3Body_ApplyTorque );
	function( "Body_ApplyLinearImpulse", &b3Body_ApplyLinearImpulse );
	function( "Body_ApplyLinearImpulseToCenter", &b3Body_ApplyLinearImpulseToCenter );
	function( "Body_ApplyAngularImpulse", &b3Body_ApplyAngularImpulse );
	function( "Body_GetMass", &b3Body_GetMass );
	function( "Body_IsAwake", &b3Body_IsAwake );
	function( "Body_SetAwake", &b3Body_SetAwake );
	function( "Body_EnableSleep", &b3Body_EnableSleep );
	function( "Body_SetBullet", &b3Body_SetBullet );
	function( "Body_GetShapeCount", &b3Body_GetShapeCount );

	// --- shape ---
	function( "CreateSphereShape", &CreateSphereShape );
	function( "CreateCapsuleShape", &CreateCapsuleShape );
	function( "CreateBoxShape", &CreateBoxShape );
	function( "DestroyShape", &b3DestroyShape );
	function( "Shape_IsValid", &b3Shape_IsValid );
	function( "Shape_GetType", &b3Shape_GetType );
	function( "Shape_GetBody", &b3Shape_GetBody );
	function( "Shape_SetFriction", &b3Shape_SetFriction );
	function( "Shape_GetFriction", &b3Shape_GetFriction );
	function( "Shape_SetRestitution", &b3Shape_SetRestitution );
	function( "Shape_GetRestitution", &b3Shape_GetRestitution );
	function( "Shape_SetDensity", &b3Shape_SetDensity );
	function( "Shape_GetDensity", &b3Shape_GetDensity );

	// --- math helpers ---
	function( "QuatFromAxisAngle", &QuatFromAxisAngle );
	function( "MulQuat", &MulQuat );
	function( "NormalizeQuat", &NormalizeQuat );
	function( "RotateVector", &RotateVector );
	function( "Add", &AddV );
	function( "Sub", &SubV );
	function( "MulSV", &MulSV );
	function( "Length", &Length );

	// --- batch readback utilities ---
	function( "AllocFloats", &AllocFloats );
	function( "FreeBuffer", &FreeBuffer );
	function( "WriteTransforms", &WriteTransforms );
}

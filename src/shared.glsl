#line 2
#define f32v2 vec2
#define f32v3 vec3
#define f32v4 vec4

#ifndef __cplusplus
#define f32 float
#endif


// -- prototyping
#define skResolutionX 640
#define skResolutionY 360

// -- shipping
// #define skResolutionX 1280
// #define skResolutionY 720

// -----------------------------------------------------------------------------
// -- shared data structures
// -----------------------------------------------------------------------------

struct GBuffer {
	f32v3 ori;
	f32v3 nor;
	f32 hitDist;
	f32 materialId;
	f32v3 wi;

#ifdef __cplusplus
	// padding just to allocate enough data. these aren't going to hit bandwidth
	f32v4 pad0; f32v4 pad1;
#endif
};

struct Material {
	f32v3 albedo;
	f32 alpha;
	f32 diffuse;
	f32 transmittive;
	f32 fresnel;
};

#define skLightsMax 128
// TODO v this needs to be written directly into a storage buffer by init!
#define skLightsInScene 1

// for miss, hit sky. this is always just the number of lights + 1
#define skLightIndexSky (skLightsInSceneInclSky-1)
#define skLightIndexNone (-1)
#define LIGHT_IDX(fl) int(fl - 100.0)

#define skLightSkyEmission (vec3(0.88, 0.86, 0.63) * 1.05)

// sky is a special light
#define skLightsInSceneInclSky (skLightsInScene+1)

struct Light {
	f32v3 ori;
	f32v3 nor;
	f32v2 halfExtent;
	f32v3 emission;
};

#ifndef __cplusplus

// -----------------------------------------------------------------------------
// -- global state
// -----------------------------------------------------------------------------

layout(binding = 0) uniform sampler2DArray samplerStbnScalar;
layout(binding = 1) uniform sampler2DArray samplerStbnVec2;
uniform float uKnobR;
uniform int iFrame;
uniform float uSlots[64];

// -----------------------------------------------------------------------------
// -- macro tuning
// -----------------------------------------------------------------------------

#define skPropagationIterations 2
#define skSamplesPerPixel 1
#define skConverge 1
#define skAnimate 0

#define skResolution ivec2(skResolutionX, skResolutionY)

// -----------------------------------------------------------------------------
// -- math utility
// -----------------------------------------------------------------------------

#define PI   3.141592653589793
#define IPI  0.318309886183791
#define IPI2 0.159154943091895
#define TAU  6.283185307179586
#define ITAU 0.159154943091895

#define SQR_EXPAND(X) ((X)*(X))
f32 sqr(const f32 x) { return x*x; }

#define f32m33 mat3
#define f32m33 mat3

// -----------------------------------------------------------------------------
// -- camera utility
// -----------------------------------------------------------------------------

struct Ray {
	f32v3 ori;
	f32v3 dir;
};

f32m33 fnLookAt(f32v3 n, f32v3 up) {
	const f32v3 ww = normalize(n);
	const f32v3 uu = normalize(cross(up, ww));
	const f32v3 vv = cross(ww, uu);
	return mat3(ww, uu, vv);
}

Ray fnLookAtRay(f32v2 uv, f32v3 origin, f32v3 target, f32 fov) {
	const f32v3 up = f32v3(0, 1, 0);
	f32m33 LA = fnLookAt(normalize(target - origin), up);
	LA = mat3(LA[2], LA[1], LA[0]);
	return Ray(origin, normalize(LA * f32v3(uv.y, uv.x, fov)));
}

bool fnWorldToScreen(f32v3 P, f32v3 ori, f32v3 target, float fov, out vec2 outUv) {
	f32v3 ww = normalize(target - ori);
	f32v3 uu = normalize(cross(f32v3(0,1,0), ww));
	f32v3 vv = cross(ww, uu);
	f32v3 v  = P - ori;
	f32 z = dot(v, ww);
	if (z <= 1e-4) {
		return false;
	}
	outUv = vec2(dot(v, uu), dot(v, vv)) * fov / z;
	return true;
}

void fnCameraFromSlots(int frame, out vec3 ori, out vec3 tgt, out float fov) {
#if !skAnimate
	frame = 0;
	float wrapX = float(uSlots[0])*TAU;
	float wrapY = float(uSlots[1]);
	float wrapZ = float(uSlots[2]);
	tgt.x = -0.7;
	tgt.y = 0.4;
	tgt.z = -0.2;
#else
	float wrapX = float(frame*0.02);
	float wrapY = 0.8f;
	float wrapZ = 3.0;
	tgt = (
		vec3(-2.0, 0.0f, 0.0f)
	);
#endif
	ori = vec3(cos(wrapX), wrapY, sin(wrapX)) * wrapZ * 3.0;
	fov = 2.0f;
}

// -----------------------------------------------------------------------------
// -- sdf utilities
// -----------------------------------------------------------------------------
// TODO some of these need fn prefix

float lengthn ( in f32v3 p, in float n ) {
	return pow(pow(p.x, n) + pow(p.y, n) + pow(p.z, n), (1.0/n));
}

float lengthn ( in f32v2 p, in float n ) {
	return pow(pow(p.x, n) + pow(p.y, n), (1.0/n));
}

void Union ( inout f32v2 t, float d, in float ID ) {
	if ( t.x > d ) t = f32v2(d, ID);
}
void Union ( inout f32v2 t, vec2 did ) {
	if ( t.x > did.x ) t = did;
}

float vmax ( f32v2 v ) { return max(v.x, v.y); }
float vmax ( f32v3 v ) { return max(max(v.x, v.y), v.z); }
float vmax ( f32v4 v ) { return max(max(v.x, v.y), max(v.z, v.w)); }
float vmin ( f32v2 v ) { return min(v.x, v.y); }
float vmin ( f32v3 v ) { return min(min(v.x, v.y), v.z); }
float vmin ( f32v4 v ) { return min(min(v.x, v.y), min(v.z, v.w)); }

float sgn ( float x) { return (x<0.0) ? -1.0 : 1.0; }
f32v2 sgn ( f32v2 v ) { return f32v2(sgn(v.x), sgn(v.y)); }
f32v3 sgn ( f32v3 v ) { return f32v3(sgn(v.x), sgn(v.y), sgn(v.z)); }

float sdSphere ( in f32v3 O, in float R ) { return length(O) - R; }
float sdShell( in float D, in float R ) { return abs(D) - R*0.5; }
float sdPlane( in f32v3 O, in f32v3 N, in float D ) {
	return dot(O, N) + D;
}

float sdCapsule(vec3 p, vec3 a, vec3 b, float r) {
	vec3 pa = p - a, ba = b - a;
	float h = clamp( dot(pa,ba)/dot(ba,ba), 0.0, 1.0 );
	return length( pa - ba*h ) - r;
}

float sdHexPrism( vec3 p, vec2 h ) {
	const vec3 k = vec3(-0.8660254, 0.5, 0.57735);
	p = abs(p);
	p.xy -= 2.0*min(dot(k.xy, p.xy), 0.0)*k.xy;
	vec2 d = vec2(
			 length(p.xy-vec2(clamp(p.x,-k.z*h.x,k.z*h.x), h.x))*sign(p.y-h.x),
			 p.z-h.y );
	return min(max(d.x,d.y),0.0) + length(max(d,0.0));
}

float sdBox ( f32v3 O, f32v3 b ) {
	f32v3 d = abs(O) - b;
	return length(max(d, f32v3(0.0))) + vmax(min(d, f32v3(0.0)));
}
float sdCheap2DBox ( f32v2 O, f32v2 b ) { return vmax(abs(O) - b); }
float sdRoundBox(vec3 p, vec3 b, float r) {
	vec3 q = abs(p) - b + r;
	return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r;
}

float sdHexagonCircumcircle(vec3 p, vec2 h) {
	vec3 q = abs(p);
	return max(q.y - h.y, max(q.x*sqrt(3.0)*0.5 + q.z*0.5, q.z) - h.x);
}

// Cone with correct distances to tip and base circle. Y is up,
// 0 is in the middle of the base.
float sdCone(vec3 p, float radius, float height) {
	vec2 q = vec2(lengthn(p.xz, 8.0), p.y);
	vec2 tip = q - vec2(0.0, height);
	vec2 mantleDir = normalize(vec2(height, radius));
	float mantle = dot(tip, mantleDir);
	float d = max(mantle, -q.y);
	float projected = dot(tip, vec2(mantleDir.y, -mantleDir.x));

	// distance to tip
	if ((q.y > height) && (projected < 0.0)) {
		d = max(d, length(tip));
	}

	// distance to base ring
	if ((q.x > radius) && (projected > length(vec2(height, radius)))) {
		d = max(d, length(q - vec2(radius, 0.0)));
	}
	return d;
}

float sdCylinder ( in f32v3 O, in float r, in float height ) {
	float d = length(O.xz) - r;
	d = max(d, abs(O.y) - height);
	return d;
}

float opSmoothSubtraction( float d1, float d2, float k ) {
		float h = clamp( 0.5 - 0.5*(d2+d1)/k, 0.0, 1.0 );
		return mix( d2, -d1, h ) + k*h*(1.0-h);
}

void opRotate(inout f32v2 p, in float a ) {
	p = cos(a)*p + sin(a)*vec2(p.y, -p.x);
}

float opUnionChamfer ( in float a, in float b, in float r ) {
	return min(min(a, b), (a - r + b)*sqrt(0.5));
}

vec2 opBlendPolynomial(float a, float b, float k) {
	// iquilez smin
	float h = 1.0 - min( abs(a-b)/(4.0*k), 1.0 );
	float w = h*h;
	float m = w*0.5;
	float s = w*k;
	return (a<b) ? vec2(a-s,m) : vec2(b-s,1.0-m);
}

vec2 opBlendCubic( float a, float b, float k ) {
	// iquilez smin
	float h = 1.0 - min( abs(a-b)/(6.0*k), 1.0 );
	float w = h*h*h;
	float m = w*0.5;
	float s = w*k; 
	return (a<b) ? vec2(a-s,m) : vec2(b-s,1.0-m);
}

// repeat around the origin by a fixed angle.
float opModPolar ( inout f32v2 p, float repetitions ) {
	float angle = 2.0*PI/repetitions,
				a = atan(p.y, p.x) + angle/2.0,
				r = length(p),
				c = floor(a/angle);
	a = mod(a, angle) - angle/2.0;
	p = f32v2(cos(a), sin(a))*r;
	// For an odd number of repetitions, fix cell index of the cell in -x dir,
	// (cell index would be -5 and 5 in two halves of the cell)
	if ( abs(c) >= (repetitions/2.0) ) c = abs(c);
	return c;
}

// Repeat the domain only in positive direction. Everything in the negative
// half-space is unchanged.
float opModSingle1(inout float p, in float size) {
	float halfsize = size*0.5;
	float c = floor((p + halfsize)/size);
	if ( p >= 0.0 )
		p = mod(p + halfsize, size) - halfsize;
	return c;
}


// mirror every second cell so they match at boundaries
float opModMirror1 ( inout float p, float size ) {
	float halfsize = size*0.5;
	float c = floor((p + halfsize)/size);
	p = mod(p + halfsize,size) - halfsize;
	p *= mod(c, 2.0)*2.0 - 1.0;
	return c;
}

// mirror at an axis-align plane which is at a dist from origin
float opMirror ( inout float p, float dist ) {
	float s = sgn(p);
	p = abs(p) - dist;
	return s;
}

// reflect space at a plane
float opReflect ( inout f32v3 p, f32v3 plane_normal, float offset ) {
	float t = dot(p, plane_normal) + offset;
	if ( t < 0.0 )
	p = p - (2.0*t)*plane_normal;
	return sgn(t);
}

// -----------------------------------------------------------------------------
// -- random utilities
// -----------------------------------------------------------------------------

#define skRandomSine 1
#define skRandomBlueNoise 2
#define skRandom skRandomBlueNoise

// global dimension counter
// 1/phi, golden ratio, low-discrepancy increment
const f32 R1 = 0.61803398875;
const f32v2 R2 = f32v2(0.754876662466, 0.56984029099);
const f32v3 R3 = (
	f32v3(
		0.8191725133961644, // 1/phi3
		0.6710436067037892, // 1/phi3^2
		0.5497004779019703  // 1/phi3^3
	)
);
int gSampleDimension = 1;

float fnSampleSeed(ivec2 px, int iteration=0) {
#if skRandom == skRandomSine
	return (
		fract(
			sin(
				  float(px.x)*3.12931
				+ float(px.y)*7.23145
				+ float(iFrame)*1.61803398875
				+ float(iteration)*0.61803398875
			) * 43758.5453123
		)
	);
#elif skRandom == skRandomBlueNoise
	const ivec3 size = textureSize(samplerStbnScalar, 0);
	const ivec3 c = (
		ivec3(
			px % size.xy,
			(iFrame * skSamplesPerPixel + iteration + 16) % size.z
		)
	);
	return texelFetch(samplerStbnScalar, c, 0).r;
#endif
}

f32v2 fnSampleSeed2(ivec2 px, int iteration=0, int frame=-1) {
#if skRandom == skRandomSine
	return vec2(fnSampleSeed(px, iteration)) + (
		vec2(1.0, 1.3)*float(iteration)*0.61803398875
	);
#else
	ivec3 size = textureSize(samplerStbnVec2, 0);
	if (frame == -1) { frame = iFrame; }
	const ivec3 c = (
		ivec3(
			px % size.xy,
			(frame * skSamplesPerPixel + iteration + 16) % size.z
		)
	);
	return texelFetch(samplerStbnVec2, c, 0).rg;
#endif
}


float fnSampleUniform(inout float seed) {
#if skRandom == skRandomSine
	return fract(sin(seed += 0.1)*43758.5453123);
#else
	return fract(sin(seed += 0.1)*43758.5453123);
#endif
}

vec2 fnSampleUniform2(inout f32v2 seed) {
#if skRandom == skRandomSine
	return (
		  fract(sin(vec2(seed.r+=0.1,seed.g+=0.1))
		* vec2(43758.5453123,22578.1459123))
	);
#else
	seed = vec2(
		fract(sin(dot(seed, vec2(127.1, 311.7))) * 43758.5453),
		fract(sin(dot(seed, vec2(269.5, 183.3))) * 22151.0)
	);
	return seed;
#endif
}

// -----------------------------------------------------------------------------
// -- samplers
// -----------------------------------------------------------------------------

void Calculate_XY ( in f32v3 N, inout f32v3 binormal, inout f32v3 bitangent){
  binormal = abs(N.y) < 0.99f ? f32v3(0.0, 1.0, 0.0) : f32v3(1.0, 0.0, 0.0);
  binormal = normalize(cross(N, binormal));
  bitangent = cross(binormal, N);
}

vec3 Reorient_Hemisphere ( vec3 wo, vec3 N ) {
  f32v3 binormal, bitangent;
  Calculate_XY(N, binormal, bitangent);
  return bitangent*wo.x + binormal*wo.y + wo.z*N;
}

float PDF_Cosine_Hemisphere ( f32v3 wi, f32v3 N ) {
  return abs(dot(wi, N)) * IPI;
}

f32v3 To_Cartesian_T ( float theta, float phi ) {
  return f32v3(cos(phi)*sin(theta), sin(phi)*sin(theta), cos(theta));
}
f32v3 To_Cartesian ( float cos_theta, float phi ) {
  float sin_theta = sqrt(max(0.0, 1.0 - cos_theta*cos_theta));
  return f32v3(cos(phi)*sin_theta, sin(phi)*sin_theta, cos_theta);
}

vec3 fnSampleHemisphereCos(
	f32v3 N,
	out float pdf, inout f32v2 seed2
) {
  vec2 u = fnSampleUniform2(seed2);
  f32v3 wo = Reorient_Hemisphere(
                normalize(To_Cartesian(sqrt(u.y), TAU*u.x)), N);
  pdf = PDF_Cosine_Hemisphere(wo, N);
  return wo;
}

float PDF_Cone ( float lobe ) {
  if ( lobe < 0.001 ) return 1.0;
  return (TAU*sqr(sin(0.5*lobe)));
}

f32v3 Sample_Uniform_Cone ( float lobe, out float pdf, inout f32v2 seed2 ) {
  f32v2 u = fnSampleUniform2(seed2);
  float phi = TAU*u.x,
        cos_theta = 1.0 - u.y*(1.0 - cos(lobe));
  pdf = PDF_Cone(lobe);
  return To_Cartesian(cos_theta, phi);
}

f32v2 Normal_Sampler ( in sampler2D s, in f32v2 uv ) {
  f32v2 eps = f32v2(0.003, 0.0);
  return f32v2(length(texture(s, uv+eps.xy)) - length(texture(s, uv-eps.xy)),
                length(texture(s, uv+eps.yx)) - length(texture(s, uv-eps.yx)));
}

// -----------------------------------------------------------------------------
// -- raymarch utilities
// -----------------------------------------------------------------------------

f32v2 fnSceneMap(f32v3 o);

#define skSceneMarchIterations 128
#define skSceneMarchMaxDist 128.0f
#define skSceneMarchThreshold 0.0001f
f32v2 fnSceneMarch(
	Ray ray,
	const int maxIterations = skSceneMarchIterations,
	const float maxDist = skSceneMarchMaxDist,
	const float threshold = skSceneMarchThreshold
) {
	f32 dist = 0.0;
	f32v2 cur;
	for (int i = 0; i < maxIterations; i++) {
		cur = fnSceneMap(ray.ori + ray.dir*dist);
		if (cur.x < threshold || cur.x > maxDist) break;
		dist += cur.x;
	}
	if (dist > maxDist || dist < 0.0f) {
		return f32v2(-1.0f, -1.0f);
	}
	return f32v2(dist, cur.y);
}

f32v3 fnSceneNormal(
	f32v3 p
) {
	f32v2 e = f32v2(1.0, -1.0)*0.001;
	return normalize(
		e.xyy*fnSceneMap(p + e.xyy).x +
		e.yxy*fnSceneMap(p + e.yxy).x +
		e.yyx*fnSceneMap(p + e.yyx).x +
		e.xxx*fnSceneMap(p + e.xxx).x
	);
}

// -----------------------------------------------------------------------------
// -- SDF scene
// -----------------------------------------------------------------------------

uniform float iTime;
uniform float iMillis;

f32v2 fnSceneMapLights(f32v3 o);

#define FnSceneMapStub \
	f32v2 fnSceneMapLights(f32v3 o) { return f32v2(0.0f); }

// for now just a sphere
f32v2 fnSceneMap(f32v3 o) {
	f32v2 t = f32v2(1e9, -1.0);
	float T = iTime;
// #if !skAnimate
	T = 0.0; // disable animation for convergence mode
// #endif
	o.x += 2.0f;

	// ground plane
	Union(t, sdPlane(o, f32v3(0,1,0), 0.0), 0.0);

	// put a box at origin just to mark origin
	Union(t, sdBox(o - f32v3(0.0,0.0,0), f32v3(0.1,4.5,0.1)), 1.0);

	// -- castle tower
	f32v3 co = o;
	float castleSpacing = 2.0;
	co.x = mod(co.x + castleSpacing*0.5, castleSpacing) - castleSpacing*0.5;
	co.z = mod(co.z + castleSpacing*0.5, castleSpacing) - castleSpacing*0.5;
	Union(t, sdCylinder(co, 0.4, 0.8), 1.0);
	Union(t, sdBox(co - f32v3(0,0.9,0), f32v3(0.42,0.1,0.42)), 1.0);
	for (int i = 0; i < 4; i++) {
		float a = float(i) * PI * 0.5;
		Union(t, sdBox(co - f32v3(cos(a)*0.32, 1.1, sin(a)*0.32), f32v3(0.08,0.1,0.08)), 1.0);
	}

	// door
	float door = sdBox(co - f32v3(0.0, 0.25, -0.38), f32v3(0.12,0.25,0.05));
	t.x = opSmoothSubtraction(door, t.x, 0.02);

	// -- orbiting moon rock around tower
	float moonA = T * 1.3;
	f32v3 moonP = o - f32v3(cos(moonA)*1.1, 0.6 + sin(T*0.7)*0.15, sin(moonA)*1.1);
	Union(t, sdRoundBox(moonP, f32v3(0.09,0.07,0.08), 0.03), 1.0);

	// -- chest, bobbing open
	float lidAngle = 0.3 + 0.28*sin(T*1.1);
	f32v3 chestBase = f32v3(0.7, 0.1, 0.3);
	Union(t, sdRoundBox(o - chestBase, f32v3(0.18,0.1,0.12), 0.02), 2.0);
	// rotate lid around its back edge
	f32v3 lidP = o - (chestBase + f32v3(0.0, 0.12, -0.1));
	opRotate(lidP.yz, -lidAngle);
	lidP += f32v3(0.0, 0.0, -0.02);
	Union(t, sdRoundBox(lidP, f32v3(0.18,0.025,0.13), 0.02), 2.0);
	// gold coins spilling out when lid open
	for (int i = 0; i < 5; i++) {
		float fi = float(i);
		float coinT = T*0.9 + fi*1.2;
		float spill = smoothstep(0.0, 1.0, (sin(T*1.1)*0.5+0.5)); // tied to lid open
		f32v3 coinP = o - (chestBase + f32v3(
			sin(fi*2.3)*0.15*spill,
			0.22 + fi*0.04*spill,
			cos(fi*1.7)*0.08*spill
		));
		Union(t, sdCylinder(coinP, 0.04, 0.01), 5.0);
	}

	// -- campfire
	f32v3 fp = f32v3(-0.7, 0.0, 0.2);
	Union(t, sdCapsule(o-fp, f32v3(-0.12,0.04,0.0), f32v3(0.12,0.04,0.0), 0.04), 3.0);
	Union(t, sdCapsule(o-fp, f32v3(0.0,0.04,-0.12), f32v3(0.0,0.04,0.12), 0.04), 3.0);
	// fire flicker — emissive
	float flicker = 0.06 + 0.02*sin(T*13.7) + 0.015*sin(T*7.3 + 1.0);
	Union(t, sdSphere(o - (fp + f32v3(0.0, 0.1 + flicker, 0.0)), flicker + 0.05), 4.0);

	// -- floating magic runes orbiting the fire (thin torus-like rings via shell)
	for (int i = 0; i < 3; i++) {
		float fi = float(i);
		float ra = T*0.8 + fi*TAU/3.0;
		float ry = sin(T*0.5 + fi*1.1) * 0.15;
		f32v3 rp = o - (fp + f32v3(cos(ra)*0.25, 0.15 + ry, sin(ra)*0.25));
		opRotate(rp.xz, ra);
		opRotate(rp.xy, T*1.5 + fi);
		Union(t, sdShell(sdBox(rp, f32v3(0.06, 0.001, 0.06)), 0.008), 6.0);
	}

	// -- knight pacing back and forth
	float pace = sin(T*0.8);
	f32v3 kOri = f32v3(1.3 + pace*0.4, 0.0, -0.3);
	// body
	Union(t, sdCylinder(o - (kOri + f32v3(0,0.35,0)), 0.1, 0.2), 1.0);
	// head
	Union(t, sdSphere(o - (kOri + f32v3(0,0.65,0)), 0.1), 1.0);
	// visor slit subtraction
	float visor = sdBox(o - (kOri + f32v3(0,0.66,-0.08)), f32v3(0.07,0.015,0.04));
	t.x = opSmoothSubtraction(visor, t.x, 0.005);
	// arms swinging
	float swing = sin(T*1.6)*0.18;
	Union(t, sdCapsule(o-kOri, f32v3(-0.13,0.5,0.0), f32v3(-0.18,0.28, swing), 0.045), 1.0);
	Union(t, sdCapsule(o-kOri, f32v3( 0.13,0.5,0.0), f32v3( 0.18,0.28,-swing), 0.045), 1.0);
	// legs
	float lstep = sin(T*1.6)*0.12;
	Union(t, sdCapsule(o-kOri, f32v3(-0.06,0.15,0.0), f32v3(-0.07,-0.1, lstep), 0.05), 1.0);
	Union(t, sdCapsule(o-kOri, f32v3( 0.06,0.15,0.0), f32v3( 0.07,-0.1,-lstep), 0.05), 1.0);
	// sword
	f32v3 swordBase = kOri + f32v3(0.22, 0.35, swing*0.5);
	Union(t, sdCapsule(o, swordBase, swordBase + f32v3(0.04, 0.45, 0.0), 0.015), 7.0);

	const f32v2 lightmap = fnSceneMapLights(o);
	Union(t, lightmap);

	return t;
}

// -----------------------------------------------------------------------------
// -- debug with knobs
// -----------------------------------------------------------------------------

f32v3 knob3(int offset) {
	return f32v3(uSlots[offset], uSlots[offset+1], uSlots[offset+2]);
}

#define NOR3(X) (X*2.0 - vec3(1.0))

#endif

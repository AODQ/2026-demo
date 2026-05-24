#ifdef PLATFORM_LINUX
#define iMouse u_mouse
#define iResolution u_resolution
#define iTime u_time
uniform vec2 u_mouse;
uniform vec2 u_resolution;
uniform float u_time;
#endif
//------------------------------------------------------------------------------
//-------- SETTINGS (FOR UTILITY LIBRARY) --------------------------------------

// more paths = higher accuracy, better visuals, more effects, etc.
// Probably need at least 5 for translucent materials.
#define ITERS 2
// Uncomment below to have light move along with scene (you have to undefine
// collect in Buf c)

//------------------------------------------------------------------------------
//-------- UTILITY LIBRARY -----------------------------------------------------
//------------------------------------------------------------------------------
#define float4 vec4
#define float3 vec3
#define float2 vec2
#define f4 vec4
#define f3 vec3
#define f2 vec2

float sqr ( in float t ) { return t*t; }

// DINPUT(iMouse.w) .. set w/ iMouse.w using plugin .. DINPUT(constant)
#define DINPUT(w) (w/288.0)

#define PI   3.141592653589793
#define IPI  0.318309886183791
#define IPI2 0.159154943091895
#define TAU  6.283185307179586
#define ITAU 0.159154943091895

#define SQR(X) ((X)*(X))

#define MOUSEX (-1.0f + (iMouse.x/iResolution.x)*2.0f)
#define MOUSEY (iMouse.y/iResolution.y)
// #define MOUSEX (iMouse.x/iResolution.x)
// #define MOUSEY (iMouse.y/iResolution.y)

struct Ray { float3 ori, dir; };

float2 Map ( float3 o );

float2 March ( in Ray ray ) {
  float dist = 0.0;
  float2 cur;
  for ( int i = 0; i != 256; ++ i ) {
    cur = Map(ray.ori + ray.dir*dist);
    if ( cur.x <= 0.0005 || dist > 256.0 ) break;
    dist += cur.x;
  }
  if ( dist > 256.0 || dist < 0.0 ) return float2(-1.0);
  return float2(dist, cur.y);
}

float3 Normal ( float3 p ) {
  float2 e = float2(1.0, -1.0)*0.001;
  return normalize(
                   e.xyy*Map(p + e.xyy).x +
                   e.yyx*Map(p + e.yyx).x +
                   e.yxy*Map(p + e.yxy).x +
                   e.xxx*Map(p + e.xxx).x);
}

float lengthn ( in float3 p, in float n ) {
  return pow(pow(p.x, n) + pow(p.y, n) + pow(p.z, n), (1.0/n));
}

float lengthn ( in float2 p, in float n ) {
  return pow(pow(p.x, n) + pow(p.y, n), (1.0/n));
}

void Union ( inout float2 t, float d, in float ID ) {
  if ( t.x > d ) t = float2(d, ID);
}
void Union ( inout float2 t, vec2 did ) {
  if ( t.x > did.x ) t = did;
}

//------------------------------------------------------------------------------
//-------- HG SDF --------------------------------------------------------------
//------------------------------------------------------------------------------

float vmax ( float2 v ) { return max(v.x, v.y); }
float vmax ( float3 v ) { return max(max(v.x, v.y), v.z); }
float vmax ( float4 v ) { return max(max(v.x, v.y), max(v.z, v.w)); }
float vmin ( float2 v ) { return min(v.x, v.y); }
float vmin ( float3 v ) { return min(min(v.x, v.y), v.z); }
float vmin ( float4 v ) { return min(min(v.x, v.y), min(v.z, v.w)); }

float  sgn ( float x  ) { return (x<0.0) ? -1.0 : 1.0; }
float2 sgn ( float2 v ) { return float2(sgn(v.x), sgn(v.y)); }
float3 sgn ( float3 v ) { return float3(sgn(v.x), sgn(v.y), sgn(v.z)); }

float sdSphere ( in float3 O, in float R ) { return length(O) - R; }
float sdShell  ( in float  D, in float R ) { return abs(D) - R*0.5; }
float sdPlane  ( in float3 O, in float3 N, in float D ) {
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

float sdBox ( float3 O, float3 b ) {
  float3 d = abs(O) - b;
  return length(max(d, float3(0.0))) + vmax(min(d, float3(0.0)));
}
float sdCheap2DBox ( float2 O, float2 b ) { return vmax(abs(O) - b); }
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

float sdCylinder ( in float3 O, in float r, in float height ) {
  float d = length(O.xz) - r;
  d = max(d, abs(O.y) - height);
  return d;
}

float opSmoothSubtraction( float d1, float d2, float k ) {
    float h = clamp( 0.5 - 0.5*(d2+d1)/k, 0.0, 1.0 );
    return mix( d2, -d1, h ) + k*h*(1.0-h);
}

void opRotate(inout float2 p, in float a ) {
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
float opModPolar ( inout float2 p, float repetitions ) {
  float angle = 2.0*PI/repetitions,
        a = atan(p.y, p.x) + angle/2.0,
        r = length(p),
        c = floor(a/angle);
  a = mod(a, angle) - angle/2.0;
  p = float2(cos(a), sin(a))*r;
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
float opReflect ( inout float3 p, float3 plane_normal, float offset ) {
  float t = dot(p, plane_normal) + offset;
  if ( t < 0.0 )
    p = p - (2.0*t)*plane_normal;
  return sgn(t);
}

//------------------------------------------------------------------------------
//-------- RANDOM --------------------------------------------------------------
//------------------------------------------------------------------------------

float Sample_Uniform(inout float seed) {
    return fract(sin(seed += 0.1)*43758.5453123);
}

vec2 Sample_Uniform2(inout float seed) {
    return fract(sin(vec2(seed+=0.1,seed+=0.1))*
                vec2(43758.5453123,22578.1459123));
}

vec3 Sample_Uniform3(inout float seed) {
    return fract(sin(vec3(seed+=0.1,seed+=0.1,seed+=0.1))*
                 vec3(43758.5453123,22578.1459123,842582.632592));
}

// -----------------------------------------------------------------------------
// ------- SAMPLERS ------------------------------------------------------------
// -----------------------------------------------------------------------------

void Calculate_XY ( in float3 N, inout float3 binormal, inout float3 bitangent){
  binormal = vec3(1.0, 0.0, 0.0) ;
  binormal = normalize(cross(N, binormal));
  bitangent = cross(binormal, N);
}

vec3 Reorient_Hemisphere ( vec3 wo, vec3 N ) {
  float3 binormal, bitangent;
  Calculate_XY(N, binormal, bitangent);
  return bitangent*wo.x + binormal*wo.y + wo.z*N;
}

float PDF_Cosine_Hemisphere ( float3 wi, float3 N ) {
  return abs(dot(wi, N)) * IPI;
}

float3 To_Cartesian_T ( float theta, float phi ) {
  return float3(cos(phi)*sin(theta), sin(phi)*sin(theta), cos(theta));
}
float3 To_Cartesian ( float cos_theta, float phi ) {
  float sin_theta = sqrt(max(0.0, 1.0 - cos_theta));
  return float3(cos(phi)*sin_theta, sin(phi)*sin_theta, cos_theta);
}

vec3 Sample_Cos_Hemisphere ( float3 wi, float3 N, out float pdf,
                             inout float seed ) {
  vec2 u = Sample_Uniform2(seed);
  float3 wo = Reorient_Hemisphere(
                normalize(To_Cartesian(sqrt(u.y), TAU*u.x)), N);
  pdf = PDF_Cosine_Hemisphere(wo, N);
  return wo;
}

float PDF_Cone ( float lobe ) {
  if ( lobe < 0.001 ) return 1.0;
  return (TAU*SQR(sin(0.5*lobe)));
}

float3 Sample_Uniform_Cone ( float lobe, out float pdf, inout float seed ) {
  float2 u = Sample_Uniform2(seed);
  float phi = TAU*u.x,
        cos_theta = 1.0 - u.y*(1.0 - cos(lobe));
  pdf = PDF_Cone(lobe);
  return To_Cartesian(cos_theta, phi);
}

float2 Normal_Sampler ( in sampler2D s, in float2 uv ) {
  float2 eps = float2(0.003, 0.0);
  return float2(length(texture(s, uv+eps.xy)) - length(texture(s, uv-eps.xy)),
                length(texture(s, uv+eps.yx)) - length(texture(s, uv-eps.yx)));
}

// -- camera --

mat3 Look_At ( in float3 N ) {
  float3 ww = normalize(N),
         uu = normalize(cross(float3(0.0, 1.0, 0.0), ww)),
         vv = normalize(cross(ww, uu));
  return mat3(ww, uu, vv);
}

float3 RCamera_Origin ( in float2 uv );
Ray Look_At ( float2 uv, in float seed ) {
  //----camera origin
  #if 0
    float3 ori = RCamera_Origin(vec2(iTime, iTime*0.5)*0.5);
  #else
    float3 ori = RCamera_Origin(vec2(MOUSEX, MOUSEY)*64.0-1.0);
  #endif
  //----etc
  float3 center = float3(0.0, 0.0, 0.0);
  float3 up     = float3(0.0, 1.0, 0.0);
  // TODO I'm pretty sure I recall this is wrong the last time I looked at it
  //   but this was years ago so idk
  mat3 LA = Look_At(normalize(center-ori));
  LA = mat3(LA[2], LA[1], LA[0]);
  uv += (Sample_Uniform2(seed) - 0.5)*2.0*(1.0/iResolution.xy);
  return Ray(ori, normalize((LA)*float3(uv.y, uv.x, 9.5)));
}

mat3 Rotate_X ( in float gamma ) {
  return mat3(1.0, 0.0      , 0.0        ,
              0.0, cos(gamma), -sin(gamma) ,
              0.0, sin(gamma),  cos(gamma));
}

mat3 Rotate_Y ( in float beta ) {
  return mat3(cos(beta) , 0.0 , sin(beta),
              0.0      , 1.0 , 0.0     ,
              -sin(beta), 0.0 , cos(beta));
}

mat3 Rotate_Z ( in float alpha ) {
  return mat3(cos(alpha), -sin(alpha), 0.0 ,
              sin(alpha), cos(alpha) , 0.0 ,
              0.0      , 0.0       , 1.0);
}
#define GTIME (MOUSEX+MOUSEY*0.5)

//------------------------------------------------------------------------------
//-------- LIGHTS/MATERIALS ----------------------------------------------------
//------------------------------------------------------------------------------

float3 RCamera_Origin ( in float2 uv ) {
  return vec3(MOUSEX*64.0f, 0.0f, 128.0f);
}

struct Light {
  float3 ori, N, emi;
  float2 radius;
};

#define LIGHTS_LEN 2
#define LIGHT_IDX(fl) int(fl - 100.0)
Light lights[LIGHTS_LEN];

void Construct_Light ( int idx, float3 ori, float3 N, float3 emi,
                       float2 radius ) {
  lights[idx] = Light(ori, N, emi, radius);
}

void Initialize_Lights ( ) {
  float3 O; float3 P;
  float time = GTIME*2.5;

  // ambient sky light

  O = vec3(0.0f, 164.0f, 5.0f);
  P = normalize(vec3(0.0f, 1.0f, 0.1f));
  Construct_Light(0, O, P, float3(2.5), float2(1000.5, 1000.5));

  // door light behind occluder

  O = vec3(0.0f, -128.0f, 180.0f);
  P = normalize(vec3(0.0f, 0.0f, 1.0f));
  Construct_Light(1, O, P, float3(0.9f, 0.85f, 0.92f), float2(30.0f, 128.0f));

  // O.xz *= -1.0;
  // // P.xz = P.zx;
  // P.xz *= -1.0;
  // P.x += cos(time)*0.2;
  // P.y = sin(time)*0.2;
  // float tint = 0.5 + sin(time*2.0)*0.5;
  // Construct_Light(1, O, normalize(P),
  //                 tint*float3(10.0, 1.0, 10.0+sin(time*5.0)*5.0),
  //                 float2(0.5, 1.5));
}

float3 Sample_Emitter ( int I, float3 O, inout float seed ) {
  float3 lorig = normalize((Sample_Uniform3(seed)-0.5)*2.0);
  lorig *= float3(0.01, lights[I].radius);
  lorig = inverse(Look_At(lights[I].N))*lorig;
  lorig += lights[I].ori;
  return normalize(lorig - O);
}

float REmit_PDF ( int I, float3 On, float3 wo, float dist ) {
	const Light l = lights[I];
	const f32 area = 4.0f * l.halfExtent.x * l.halfExtent.y;
	const f32 dotNorWo = abs(dot(l.nor, wo));
	return (dist * dist) / (area * max(dotNorWo, 0.001f));
}

bool Valid_Emitter ( int I, in float3 wo ) {
  if ( I < 0 ) return false;
  return dot(lights[I].N, wo) > 0.0;
}

struct Material {
  float3 colour;
  float alpha, diffuse, transmittive, fresnel;
};

// RMTAG
Material RMaterial ( float3 O, float idx ) {
  if ( idx == 100.0 )
    return Material(float3(lights[0].emi), 1.0, 1.0, 0.0, 1.5);

  if (idx == 1.0f) // helmet
    return Material(vec3(0.3f, 0.1f, 0.1f), 1.0f, 0.2f, 0.0f, 1.5f);
  if (idx == 2.0f) // tubing
    return Material(vec3(0.1f, 0.1f, 0.1f), 1.0f, 0.2f, 0.0f, 1.5f);
  if (idx == 3.0f) // visor
    return Material(vec3(0.4f, 0.9f, 0.6f), 1.0f, 0.7f, 0.0f, 1.5f);
  return Material(float3(1.0), 0.0, 0.0, 0.0, 0.0);
}

//------------------------------------------------------------------------------
//-------- SHADER --------------------------------------------------------------
//------------------------------------------------------------------------------

float supershape_r(float phi, float n1, float n2, float n3, float a, float b, float m)
{
    float sqrt_term1 = pow(abs(1.0 / a * cos(m / 4.0 * phi)), n2);
    float sqrt_term2 = pow(abs(1.0 / b * sin(m / 4.0 * phi)), n3);
    float radius = pow(sqrt_term1 + sqrt_term2, -1.0 / n1);

    return radius;
}

float r1(float phi)
{
    return supershape_r(phi, 1.0, 1.0, 1.0, 1.0, 1.0, 6.0);
}

float r2(float phi)
{
     return supershape_r(phi, 1.0, 1.0, 1.0, 1.0, 1.0, 3.0);
}


int Illumination ( inout float3 O, float3 N, float3 wi, inout float3 bsdf_wo,
                   Material mat, inout float3 radiance,
                   inout float3 direct_radiance, inout float seed ) {
  int return_enum = 0;
  float bsdf_pdf, emit_pdf;
  float3 prev_radiance = radiance;

  // ---- indirect radiance ----
  bsdf_wo = BSDF_Sample(N, wi, O, mat, bsdf_pdf, seed);
  float2 bsdf_res = March(Ray(O+bsdf_wo*0.1, bsdf_wo));
  radiance *= (BSDF_F(N, wi, bsdf_wo, mat)*abs(dot(N, wi)))/bsdf_pdf;
  int lidx = -1;

  if ( bsdf_res.x < 0.0 ) return -1;
  if ( Valid_Emitter(LIGHT_IDX(bsdf_res.y), bsdf_wo) ) {
    lidx = LIGHT_IDX(bsdf_res.y);
    float3 Lo = O + bsdf_wo*bsdf_res.x;
    emit_pdf = REmit_PDF(lidx, N, bsdf_wo, bsdf_res.x);
    float3 dr = lights[lidx].emi * BSDF_F(N, wi, bsdf_wo, mat);
    dr *= prev_radiance * float3(bsdf_pdf/(bsdf_pdf+emit_pdf));
    direct_radiance += clamp(dr, float3(0.0), float3(1.0));
    return 2;
  }

  // ---- direct radiance ----
  if ( lidx == -1 ) lidx = int(Sample_Uniform(seed)*float(LIGHTS_LEN));
  float3 emit_wo = Sample_Emitter(lidx, O, seed);
  float2 emit_res = March(Ray(O+emit_wo*0.01, emit_wo));
  bsdf_pdf = BSDF_PDF(N, wi, emit_wo, mat);
  if ( lidx == LIGHT_IDX(emit_res.y) && bsdf_pdf > 0.0 &&
       Valid_Emitter(lidx, emit_wo) ) {
    float3 Lo = O + emit_wo*emit_res.x;
    emit_pdf = REmit_PDF(lidx, N, emit_wo, emit_res.x);
    float3 dr = lights[lidx].emi * BSDF_F(N, wi, emit_wo, mat);
    dr *= prev_radiance * emit_pdf/(bsdf_pdf+emit_pdf);
    direct_radiance += clamp(dr, float3(0.0), float3(1.0));
    if ( return_enum != 2 ) {
      return_enum = 1;
    }
  }

  // prepare origin
  O = O + bsdf_wo * bsdf_res.x;
  return return_enum;
}

int Propagate ( inout float3 radiance, inout float3 accum_rad,
                inout Ray eye, inout float seed, int step ) {
  float2 res = March(eye);

  //-- accumulate radiance --
  if ( res.x < 0.0 ) return 0;
  //-- gather material and reflection information --
  float3 O  = eye.ori + eye.dir*res.x,
         N  = Normal(O),
         wi = eye.dir,
         wo;// to be calculated by direct light contribution
  if ( res.y >= 100.0 ) {
    int I = LIGHT_IDX(res.y);
    if ( step == 0 && Valid_Emitter(I, wi) )
      accum_rad = lights[I].emi;
    return 2;
  }
  Material mat = RMaterial(O, res.y);
  int return_enum = Illumination(O, N, wi, wo, mat, radiance, accum_rad,
                                 seed);
  //-- prepare for next propagation --
  eye.ori = O+wo*0.01;
  eye.dir = wo;

  return return_enum;
}


vec3 Render(vec2 uv, float seed, Ray eye) {
  vec4 fragColor = float4(0.0);
  float3 radiance  = float3(1.0),
         accum_rad = float3(0.0);

  Initialize_Lights();

  // DEBUG BELOW
  // {
  //   float2 res = March(eye);
  //   float3 ori = eye.ori + eye.dir*res.x;
  //   if (res.x < 0.0) return abs(eye.dir)*0.1;
  //   Material mat = RMaterial(ori, res.y);
  //   // return mat.colour * abs(Normal(ori));
  //   // return vec4(abs(Normal(ori)), 1.0);
  // }

  int hit = 0;
  for ( int i = 0; i != ITERS; ++ i ) {
    int res = Propagate(radiance, accum_rad, eye, seed, i);
    if ( res == 1 ) {
      hit = 1;
    }
    if ( res == 2 ) {
      hit = 1;
      break;
    }
    if ( res == -1 ) break;
  }

  fragColor.xyz = accum_rad;
  // fragColor.w = hit==1 ? 1.0 : 0.0;

  return fragColor.rgb;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
  float2 uv = -1.0 + 2.0*fragCoord.xy/iResolution.xy;
  uv.x *= iResolution.x/iResolution.y;

  float seed = fract(sin(dot(uv, vec2(12.9898*iTime, iTime*78.233))) * 43758.5453);

  Ray eye = Look_At(uv, seed);
  fragColor = vec4(Render(uv, seed, eye), 1.0f);
}

#ifdef PLATFORM_LINUX
out vec4 fragColor;
void main(void) {
  mainImage(fragColor, gl_FragCoord.xy);
}
#endif

// -----------------------------------------------------------------------------
// -- SDF SCENE COMPOSITION ----------------------------------------------------
// -----------------------------------------------------------------------------

#define Material float2
#define MaterialNew() Material(999.0f)
#define MaterialOrigin(x) x.x
#define MaterialID(x) x.y

float MapHelmetEar(in vec3 O) {
  vec3 o = O;
  // just ever so slight tilt downwards
  opRotate(o.yx, PI/2.0f + PI*0.04f);
  float ra = 2.2f;
  float rb = 3.8f;
  float h = 0.8f;
  vec2 d = vec2(length(o.xz) - 2.0f*ra + rb, abs(o.y) - h);
  return min(max(d.x, d.y), 0.0f) + length(max(d, 0.0f)) - rb;
}

float MapCheekCutout(in vec3 O) {
  vec3 o = O;
  o.y *= 0.7f;

  opRotate(o.zy, PI/2.0);
  opRotate(o.xz, -0.25);

  {
    vec2 h = vec2(10.0, 10.0);
    vec3 q = abs(o);
    float da = o.z < 0.0f ? -0.1f : 0.8f;
    float za = 0.5f;
    // hexagon circumcircle
    return max(q.y - h.y, max(q.x*sqrt(3.0)*za + q.z*da, q.z) - h.x);
  }
}

float MapChin(in vec3 O) {
  opRotate(O.zy, PI*0.5f);
  O.y *= 0.8f;
  return sdHexPrism(O, vec2(3.5f, 5.5f));
}

float MapTubeConnector(in vec3 O) {
  O.x -= 11.5f;
  O.y -= 2.8f;
  O.z += 0.5f;
  O.x += smoothstep(0.0f, 4.0f, O.x);
  O.y += -smoothstep(0.0f, 4.0f, O.x);
  float zy = 2.5f;
  zy -= smoothstep(0.0f, 8.0f, O.x)*4.0f;
  opRotate(O.zy, PI*0.7f);
  opRotate(O.zx, PI*1.3f);
  return sdHexPrism(O, vec2(2.0f, zy));
}

float MapHelmetTube(in vec3 O) {
  // simple tube
  vec3 base = vec3(6.0f, -6.0f, 1.0f);
  vec3 start = base + vec3(-2.0f, -1.4f, 3.0f);
  vec3 end = base + vec3(3.0f, 2.0f, 3.0f);
  float tube;
  {
    vec3 endcp = end;
    vec3 startcp = start;
    endcp.y += O.x * 0.1f;
    endcp.z -= log(O.x);
    startcp.x += O.x * 0.12f;
    tube = sdCapsule(O, startcp, endcp, 1.2f);
  }
  // endpoint
  // O.y -= start;
  O -= start;
  opRotate(O.xy, 2.0f);
  tube = min(tube, sdCylinder(O, 1.4f, 1.8f));
  return tube;
}

float MapHelmetVisorCutoutRot(in vec3 O, in vec2 P, float rot, vec2 size) {
  opRotate(O.xy, rot);
  return (
    sdRoundBox(
      O - vec3(P.x, P.y, 0.0f),
      vec3(size.x, size.y, 2.0f),
      0.5f
    )
  );
}

float MapHelmetVisorCutout(in vec3 O) {
  vec3 o = O;
  float cutout = 9999.9f;
  #define Cut(x, y, z, sx, sy) \
    MapHelmetVisorCutoutRot(O, vec2(x, y), z, vec2(sx, sy))
  cutout = opUnionChamfer(Cut(-3.3f, 0.0f, 2.3f, 2.0f, 8.0f), cutout, 0.5f);
  cutout = opUnionChamfer(Cut(1.5f, 0.0f, 1.5f, 2.0f, 8.0f), cutout, 0.5f);
  cutout = opUnionChamfer(Cut(-0.1f, -0.4f, -9.4f, 2.0f, 4.0f), cutout, 0.5f);
  cutout = opUnionChamfer(Cut(0.4f, -2.0f, -0.0f, 1.0f, 5.0f), cutout, 0.0f);
  cutout = opUnionChamfer(Cut(0.0f, 0.0f, 0.0f, 3.0f, 3.5f), cutout, 1.0f);

  #undef Cut

  return cutout;
}

float2 MapHelmet(in vec3 O) {
  Material m = MaterialNew();

  // this just does the right side of helm then mirrors it
  O.x = abs(O.x);

  float helmet = sdSphere(O, 12.0);

  // create base helmet
  float cutoutR = MapCheekCutout(O - vec3(15.0f, -10.0f, 0.0));
  {
    helmet = opSmoothSubtraction(cutoutR, helmet, 0.3f);
    helmet = opBlendPolynomial(helmet, MapChin(O-vec3(0,-6.0f,1.5)), 0.5f).x;
    helmet = opBlendPolynomial(helmet, MapTubeConnector(O-vec3(-0.0f,-5.4f,1.5)), 0.5f).x;

    float rightEar = MapHelmetEar(O-vec3(9.0, 2.0, 0.0));
    helmet = opBlendPolynomial(helmet, rightEar, 0.3f).x;

    // now have to do cut outs of the visor
    helmet = (
      opSmoothSubtraction(
        MapHelmetVisorCutout(O+vec3(0.0f, -2.0f, -10.0f)),
        helmet,
        0.5f
      )
    );
  }

  // now combine all the different materials
  Union(m, helmet, 1.0f);
  Union(m, MapHelmetTube(O + vec3(0.0f, 1.2f, 0.0f)), 2.0f);

  // the visor
  {
    float visor = sdSphere(O, 11.3f);
    visor = opSmoothSubtraction(cutoutR, visor, 0.3f);
    Union(m, visor, 3.0f);
  }

  return m;
}

//------------------------------------------------------------------------------

float2 Map(in vec3 O) {
  float3 o=O, ori=O;
  float2 res = float2(999.0);

  // map the 'area'
  Union(res, dot(O - vec3(0.0f, -35.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f)), 1.0);

  vec2 helm = MapHelmet(o);
  Union(res, helm.x, helm.y);


  // -----------light----------------------------------------------------------
  for ( int i = 0; i != LIGHTS_LEN; ++ i ) {
    O = inverse(Look_At(lights[i].N))*(ori-lights[i].ori);
    Union(res, sdBox(O, float3(0.01, lights[i].radius)), 100.0+float(i));
  }
  // --------------------------------------------------------------------------

  return res;
}

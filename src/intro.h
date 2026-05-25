// intro.h — compute-pipeline engine for a single-window OpenGL demo.
//
// The host (main.cpp) owns the data: shader filenames/sources, the resource
// list, the pipeline order, and the input map. This engine owns the
// mechanics: window + GL 4.3 context, the GL loader, scheduling, resource
// creation/resize, hot-reload, knob/control input, and present.

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;

struct vec2 { f32 x, y; };
struct vec3 { f32 x, y, z; };
struct vec4 { f32 x, y, z, w; };

#define INTRO_NONE (-1)
#define INTRO_MAX_READS 8
#define INTRO_MAX_BUFFERS 8

#define INTRO_SLOTS 64

typedef i32 IntroImage;
typedef i32 IntroBuffer;
typedef i32 IntroControl;

typedef enum {
	// dispatched once after init (and after each resize)
	IntroSchedule_Startup,
	// dispatched every frame
	IntroSchedule_EveryFrame,
	// every `interval` frames; output must be a buffered image
	IntroSchedule_Buffered,
} IntroSchedule;

typedef enum {
	// work-groups cover the render size
	IntroDispatch_Image,
	// work-groups taken from gx, gy, gz
	IntroDispatch_Explicit,
} IntroDispatch;

typedef struct {
	char const * title;
	i32 width;
	i32 height;
	// group size for IMAGE dispatch (0 -> 8)
	i32 localSize;
	bool vsync;
	// honored only in dev builds
	bool hotReload;
	// where <name>.comp live (0 -> "shaders")
	char const * shaderDir;
} IntroConfig;

typedef struct {
	// ping-pong; required for self-feedback passes. Format is RGBA16F.
	bool doubleBuffered;
} IntroImageDesc;

typedef struct {
	// SSBO binding point referenced by the shader
	i32 binding;
	size_t bytes;
} IntroBufferDesc;

typedef struct {
	// disk stem -> <shaderDir>/<name>.comp
	char const * name;
	// fallback source, shipped in the size build
	char const * embedded;
	IntroSchedule sched;
	// IntroSchedule_Buffered only (frames between dispatches)
	i32 interval;
	IntroDispatch dispatch;
	// IntroDispatch_Explicit only (work-group counts)
	i32 gx;
	i32 gy;
	i32 gz;
	// 0 -> config localSize
	i32 localX;
	i32 localY;
	// output image, or INTRO_NONE
	IntroImage write;
	// bound to sampler units 0..n-1
	IntroImage reads[INTRO_MAX_READS];
	i32 readCount;

	IntroBuffer buffers[INTRO_MAX_BUFFERS];
	i32 bufferCount;
} IntroPassDesc;

// Lifecycle. intro_init returns false if no display/context is available.
bool intro_init(IntroConfig const * cfg);
void intro_run(void);
void intro_shutdown(void);

// Registration: call after intro_init and before intro_run.
IntroImage intro_add_image(IntroImageDesc const * desc);
IntroBuffer intro_add_buffer(IntroBufferDesc const * desc);
void intro_add_pass(IntroPassDesc const * desc);
void intro_set_present(IntroImage img);

// Seed a storage buffer from the host (alternative to a startup pass).
void intro_buffer_upload(IntroBuffer buf, void const * data, size_t bytes);

// Load a PNG from disk as a read-only texture. Returns an IntroImage that can
// be used in IntroPassDesc reads[]. Uses REPEAT wrap (good for tiling noise).
// Requires stb_image.h to be present in src/.
IntroImage intro_load_texture_png(char const * path);

IntroImage intro_load_texture_array_png(char const * path, i32 layerCount);

// Float controls exposed to every shader as `uniform float <name>`.
IntroControl intro_add_control(char const * name, f32 def, f32 lo, f32 hi);
void intro_bind_key(i32 vk, IntroControl ctrl, f32 delta);
void intro_bind_reset(i32 vk, IntroControl ctrl);

void intro_bind_select(i32 vk, i32 slot);
void intro_bind_slot_key(i32 vk, f32 delta);
f32 intro_slot_value(i32 slot);
f32 intro_control_value(IntroControl ctrl);

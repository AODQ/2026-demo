// main.cpp — driver. Owns the shader sources, the resource list, the pipeline
// order, and the input map. All mechanics live in the intro engine.

#include "intro.h"
#include "embedded_shaders.h"

#include <cstdio>
#include <windows.h>  // VK_F13.. for the key map

// ---------------------------------------------------------------------------

// shared buffer
#include "shared.glsl"

// controls
#include <windows.h>
#include <GL/gl.h>

// ---------------------------------------------------------------------------

static void setup_controls() {
	f32 const STEP = 0.02f;

	// always-on encoders 1 and 2
	IntroControl const kR = intro_add_control("uKnobR", 0.5f, 0.0f, 1.0f);
	intro_bind_key('H', kR, -STEP);  // enc2 CCW
	intro_bind_key('J', kR, +STEP);  // enc2 CW

	// third encoder edits whichever slot is selected
	intro_bind_slot_key('B', -STEP); // enc3 CCW
	intro_bind_slot_key('N', +STEP); // enc3 CW

	// slot selectors. layer * 16 + position. QMK sends different keycodes
	// per layer (letters / numbers / F1-12 / F13-24), so each maps to a
	// distinct slot index.
	// --- layer 1 (letters) -> slots 0..11
	intro_bind_select('Q', 0);  intro_bind_select('W', 1);
	intro_bind_select('E', 2);  intro_bind_select('R', 3);
	intro_bind_select('A', 4);  intro_bind_select('S', 5);
	intro_bind_select('D', 6);  intro_bind_select('F', 7);
	intro_bind_select('Z', 8);  intro_bind_select('X', 9);
	intro_bind_select('C', 10); intro_bind_select('V', 11);
	// --- layer 2 (number row) -> slots 16..27
	intro_bind_select('1', 16); intro_bind_select('2', 17);
	intro_bind_select('3', 18); intro_bind_select('4', 19);
	intro_bind_select('5', 20); intro_bind_select('6', 21);
	intro_bind_select('7', 22); intro_bind_select('8', 23);
	intro_bind_select('9', 24); intro_bind_select('0', 25);
	intro_bind_select(VK_OEM_MINUS, 26); intro_bind_select(VK_OEM_PLUS, 27);
	// --- layer 3 (F1-F12) -> slots 32..43
	intro_bind_select(VK_F1, 32);  intro_bind_select(VK_F2, 33);
	intro_bind_select(VK_F3, 34);  intro_bind_select(VK_F4, 35);
	intro_bind_select(VK_F5, 36);  intro_bind_select(VK_F6, 37);
	intro_bind_select(VK_F7, 38);  intro_bind_select(VK_F8, 39);
	intro_bind_select(VK_F9, 40);  intro_bind_select(VK_F10, 41);
	intro_bind_select(VK_F11, 42); intro_bind_select(VK_F12, 43);
	// --- layer 4 (F13-F24) -> slots 48..59
	intro_bind_select(VK_F13, 48); intro_bind_select(VK_F14, 49);
	intro_bind_select(VK_F15, 50); intro_bind_select(VK_F16, 51);
	intro_bind_select(VK_F17, 52); intro_bind_select(VK_F18, 53);
	intro_bind_select(VK_F19, 54); intro_bind_select(VK_F20, 55);
	intro_bind_select(VK_F21, 56); intro_bind_select(VK_F22, 57);
	intro_bind_select(VK_F23, 58); intro_bind_select(VK_F24, 59);
}

// ---------------------------------------------------------------------------

i32 main(i32, char * *) {
	IntroConfig config {
		.title = "intro",
		.width = skResolutionX,
		.height = skResolutionY,
		.localSize = 8,
		.vsync = true,
		.hotReload = true,
		.shaderDir = "shaders",
	};
	if (!intro_init(&config)) {
		return 1;
	}

	setup_controls();

	// -- create images

	IntroImageDesc const imgDescPalette {
		.doubleBuffered = false,
	};
	IntroImageDesc const imgDescHistory {
		.doubleBuffered = false,
	};
	IntroImageDesc const imgDescPresent {
		.doubleBuffered = false,
	};

	IntroImage const imgScene = intro_add_image(&imgDescPresent);
	IntroImage const imgHistory = intro_add_image(&imgDescHistory);
	IntroImage const imgPresent = intro_add_image(&imgDescPresent);

	IntroImage const imgBlueNoise = (
		intro_load_texture_png("shaders/bluenoise.png")
	);

	IntroImage const imgStbnScalar = (
		intro_load_texture_array_png(
			"shaders/textures/stbn_scalar_2Dx1Dx1D_128x128x64x1",
			64
		)
	);
	IntroImage const imgStbnVec2 = (
		intro_load_texture_array_png(
			"shaders/textures/stbn_vec2_2Dx1D_128x128x64",
			64
		)
	);

	printf("loaded images: scene=%d history=%d present=%d blueNoise=%d stbnScalar=%d stbnVec2=%d\n",
		   imgScene, imgHistory, imgPresent, imgBlueNoise,
		   imgStbnScalar, imgStbnVec2);

	// -- create buffers

	IntroBufferDesc const bufDescPalette {
		.bytes = 256 * sizeof(float) * 4,
	};

	IntroBufferDesc const bufDescMaterials {
		.bytes = sizeof(Material) * 256,
	};
	IntroBufferDesc const bufDescLights {
		.bytes = sizeof(Light) * 256,
	};

	IntroBufferDesc const bufDescGbuffer {
		.bytes = sizeof(GBuffer) * skResolutionX * skResolutionY,
	};
	IntroBufferDesc const bufDescRadiance {
		.bytes = sizeof(vec4) * skResolutionX * skResolutionY,
	};

	IntroBuffer const bufPalette = intro_add_buffer(&bufDescPalette);
	IntroBuffer const bufGbuffer = intro_add_buffer(&bufDescGbuffer);
	IntroBuffer const bufRadiance = intro_add_buffer(&bufDescRadiance);
	IntroBuffer const bufMaterials = intro_add_buffer(&bufDescMaterials);
	IntroBuffer const bufLights = intro_add_buffer(&bufDescLights);

	// -- create passes
	// initial -> [gbuffer | propagate bounces | temporal accumulate | postproc]
	IntroPassDesc const passInit {
		.name = "initial",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_Startup,
		.dispatch = IntroDispatch_Explicit,
		.gx = 1, .gy = 1, .gz = 1,
		.write = INTRO_NONE,
		.reads = { imgStbnScalar, imgStbnVec2 },
		.readCount = 2,
		.buffers = { bufPalette, bufMaterials, bufLights, },
		.bufferCount = 3,
	};
	IntroPassDesc const passGbuffer {
		.name = "gbuffer",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_EveryFrame,
		.dispatch = IntroDispatch_Image,
		.localX = 8, .localY = 8,
		.write = INTRO_NONE,
		.reads = { imgStbnScalar, imgStbnVec2 },
		.readCount = 2,
		.buffers = { bufGbuffer, bufLights, },
		.bufferCount = 2,
	};
	IntroPassDesc const passPropagate {
		.name = "propagate",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_EveryFrame,
		.dispatch = IntroDispatch_Image,
		.localX = 8, .localY = 8,
		.write = INTRO_NONE,
		.reads = { imgStbnScalar, imgStbnVec2 },
		.readCount = 2,
		.buffers = { bufGbuffer, bufRadiance, bufMaterials, bufLights, },
		.bufferCount = 4,
	};
	IntroPassDesc const passAccumulate {
		.name = "accumulate",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_EveryFrame,
		.dispatch = IntroDispatch_Image,
		.write = imgHistory,
		.reads = { imgStbnScalar, imgStbnVec2, imgHistory },
		.readCount = 3,
		.buffers = { bufRadiance },
		.bufferCount = 1,
	};
	IntroPassDesc const passDescPost {
		.name = "post",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_EveryFrame,
		.dispatch = IntroDispatch_Image,
		.write = imgPresent,
		.reads = { imgStbnScalar, imgStbnVec2, imgHistory },
		.readCount = 3,
	};

	intro_add_pass(&passInit);
	intro_add_pass(&passGbuffer);
	intro_add_pass(&passPropagate);
	intro_add_pass(&passAccumulate);
	intro_add_pass(&passDescPost);

	intro_set_present(imgPresent);
	intro_run();
	intro_shutdown();
	return 0;
}

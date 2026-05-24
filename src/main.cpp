// main.cpp — driver. Owns the shader sources, the resource list, the pipeline
// order, and the input map. All mechanics live in the intro engine.

#include "intro.h"
#include "embedded_shaders.h"

#include <windows.h>  // VK_F13.. for the key map

// ---------------------------------------------------------------------------

// shared buffer
#include "shared.glsl"

// ---------------------------------------------------------------------------

static void setup_controls() {
	f32 const STEP = 0.02f;
	IntroControl const k0 = intro_add_control("uKnob0", 0.5f, 0.0f, 1.0f);
	IntroControl const k1 = intro_add_control("uKnob1", 0.5f, 0.0f, 1.0f);
	IntroControl const k2 = intro_add_control("uKnob2", 0.5f, 0.0f, 1.0f);
	intro_bind_key(VK_F1, k0, -STEP);
	intro_bind_key(VK_F2, k0, +STEP);
	intro_bind_reset(VK_F3, k0);
	intro_bind_key(VK_F4, k1, -STEP);
	intro_bind_key(VK_F5, k1, +STEP);
	intro_bind_reset(VK_F6, k1);
	intro_bind_key(VK_F7, k2, -STEP);
	intro_bind_key(VK_F8, k2, +STEP);
	intro_bind_reset(VK_F9, k2);
}

// ---------------------------------------------------------------------------

i32 main(i32, char * *) {
	IntroConfig config {
		.title = "intro",
		.width = 1280,
		.height = 720,
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
		.doubleBuffered = true,
	};
	IntroImageDesc const imgDescPresent {
		.doubleBuffered = false,
	};

	IntroImage const imgScene = intro_add_image(&imgDescPresent);
	IntroImage const imgHistory = intro_add_image(&imgDescHistory);
	IntroImage const imgPresent = intro_add_image(&imgDescPresent);

	// -- create buffers

	IntroBufferDesc const bufDescPalette {
		.bytes = 256 * sizeof(float) * 4,
	};

	// 
	IntroBufferDesc const bufDescGbuffer {
		.bytes = sizeof(GBuffer) * 1280 * 720,
	};
	IntroBufferDesc const bufDescRadiance {
		.bytes = sizeof(vec4) * 1280 * 720,
	};

	IntroBuffer const bufPalette = intro_add_buffer(&bufDescPalette);
	IntroBuffer const bufGbuffer = intro_add_buffer(&bufDescGbuffer);
	IntroBuffer const bufRadiance = intro_add_buffer(&bufDescRadiance);

	// -- create passes
	// initial -> [gbuffer | propagate bounces | temporal accumulate | postproc]
	IntroPassDesc const passInit {
		.name = "initial",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_Startup,
		.dispatch = IntroDispatch_Explicit,
		.gx = 1, .gy = 1, .gz = 1,
		.write = INTRO_NONE,
		.readCount = 0,
		.buffers = { bufPalette },
		.bufferCount = 1,
	};
	IntroPassDesc const passGbuffer {
		.name = "gbuffer",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_EveryFrame,
		.dispatch = IntroDispatch_Explicit,
		.gx = 1280 / 8, .gy = 720 / 8, .gz = 1,
		.write = INTRO_NONE,
		.readCount = 0,
		.buffers = { bufGbuffer },
		.bufferCount = 1,
	};
	IntroPassDesc const passPropagate {
		.name = "propagate",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_EveryFrame,
		.dispatch = IntroDispatch_Explicit,
		.gx = 1280 / 8, .gy = 720 / 8, .gz = 1,
		.write = INTRO_NONE,
		.readCount = 0,
		.buffers = { bufGbuffer, bufRadiance },
		.bufferCount = 2,
	};
	IntroPassDesc const passAccumulate {
		.name = "accumulate",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_EveryFrame,
		.dispatch = IntroDispatch_Image,
		.write = imgHistory,
		.reads = { imgHistory },
		.readCount = 1,
		.buffers = { bufRadiance },
		.bufferCount = 1,
	};
	IntroPassDesc const passDescPost {
		.name = "post",
		.embedded = nullptr, // TODO embed
		.sched = IntroSchedule_EveryFrame,
		.dispatch = IntroDispatch_Image,
		.write = imgPresent,
		.reads = { imgHistory },
		.readCount = 1,
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

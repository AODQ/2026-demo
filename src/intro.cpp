// intro.cpp — engine implementation. See intro.h for the public API.
//
// Win32 + WGL + OpenGL 4.3 core. All state lives in the single static engine
// instance `E`; there is only ever one window. NOT compiled in-sandbox; the
// spots most worth review are the WGL context creation, the GL loader, and
// the memory barriers in run_pass / present.

#include "intro.h"

#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef INTRO_SIZE
#include <sys/stat.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif


// ---------------------------------------------------------------------------
// GL / WGL tokens absent from mingw's 1.1-era <GL/gl.h>.
// ---------------------------------------------------------------------------
#define GL_COMPUTE_SHADER 0x91B9
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_WRITE_ONLY 0x88B9
// read write
#define GL_READ_WRITE 0x88BA
#define GL_RGBA8   0x8058
#define GL_RGBA16F 0x881A
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GL_FRAMEBUFFER_BARRIER_BIT 0x00000400

#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_ACCELERATION_ARB 0x2003
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_PIXEL_TYPE_ARB 0x2013
#define WGL_COLOR_BITS_ARB 0x2014
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_FULL_ACCELERATION_ARB 0x2027
#define WGL_TYPE_RGBA_ARB 0x202B
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

#ifndef GL_VERSION_1_5
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#endif


// ---------------------------------------------------------------------------
// Modern GL entry points. APIENTRY (__stdcall) is mandatory or calls crash.
// ---------------------------------------------------------------------------
typedef GLuint(APIENTRY* PFN_CreateShader)(GLenum);
typedef void(APIENTRY* PFN_ShaderSource)(
	GLuint, GLsizei, const char* const*, const GLint*);
typedef void(APIENTRY* PFN_CompileShader)(GLuint);
typedef void(APIENTRY* PFN_GetShaderiv)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFN_GetShaderInfoLog)(
	GLuint, GLsizei, GLsizei*, char*);
typedef GLuint(APIENTRY* PFN_CreateProgram)(void);
typedef void(APIENTRY* PFN_AttachShader)(GLuint, GLuint);
typedef void(APIENTRY* PFN_LinkProgram)(GLuint);
typedef void(APIENTRY* PFN_GetProgramiv)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFN_GetProgramInfoLog)(
	GLuint, GLsizei, GLsizei*, char*);
typedef void(APIENTRY* PFN_DeleteShader)(GLuint);
typedef void(APIENTRY* PFN_DeleteProgram)(GLuint);
typedef void(APIENTRY* PFN_UseProgram)(GLuint);
typedef GLint(APIENTRY* PFN_GetUniformLocation)(GLuint, const char*);
typedef void(APIENTRY* PFN_Uniform1f)(GLint, GLfloat);
typedef void(APIENTRY* PFN_Uniform1fv)(GLint, GLsizei, const GLfloat*);
typedef void(APIENTRY* PFN_Uniform2f)(GLint, GLfloat, GLfloat);
typedef void(APIENTRY* PFN_Uniform1i)(GLint, GLint);
typedef void(APIENTRY* PFN_DispatchCompute)(GLuint, GLuint, GLuint);
typedef void(APIENTRY* PFN_MemoryBarrier)(GLbitfield);
typedef void(APIENTRY* PFN_BindImageTexture)(
	GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
typedef void(APIENTRY* PFN_ActiveTexture)(GLenum);
typedef void(APIENTRY* PFN_GenBuffers)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_BindBuffer)(GLenum, GLuint);
typedef void(APIENTRY* PFN_BufferData)(
	GLenum, GLsizeiptr, const void*, GLenum);
typedef void(APIENTRY* PFN_BufferSubData)(
	GLenum, GLintptr, GLsizeiptr, const void*);
typedef void(APIENTRY* PFN_BindBufferBase)(GLenum, GLuint, GLuint);
typedef void(APIENTRY* PFN_GenFramebuffers)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_BindFramebuffer)(GLenum, GLuint);
typedef void(APIENTRY* PFN_FramebufferTexture2D)(
	GLenum, GLenum, GLenum, GLuint, GLint);
typedef void(APIENTRY* PFN_BlitFramebuffer)(
	GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield,
	GLenum);

static PFN_CreateShader glCreateShader;
static PFN_ShaderSource glShaderSource;
static PFN_CompileShader glCompileShader;
static PFN_GetShaderiv glGetShaderiv;
static PFN_GetShaderInfoLog glGetShaderInfoLog;
static PFN_CreateProgram glCreateProgram;
static PFN_AttachShader glAttachShader;
static PFN_LinkProgram glLinkProgram;
static PFN_GetProgramiv glGetProgramiv;
static PFN_GetProgramInfoLog glGetProgramInfoLog;
static PFN_DeleteShader glDeleteShader;
static PFN_DeleteProgram glDeleteProgram;
static PFN_UseProgram glUseProgram;
static PFN_GetUniformLocation glGetUniformLocation;
static PFN_Uniform1f glUniform1f;
static PFN_Uniform1fv glUniform1fv;
static PFN_Uniform2f glUniform2f;
static PFN_Uniform1i glUniform1i;
static PFN_DispatchCompute glDispatchCompute;
static PFN_MemoryBarrier glMemoryBarrier;
static PFN_BindImageTexture glBindImageTexture;
static PFN_ActiveTexture glActiveTexture;
static PFN_GenBuffers glGenBuffers;
static PFN_BindBuffer glBindBuffer;
static PFN_BufferData glBufferData;
static PFN_BufferSubData glBufferSubData;
static PFN_BindBufferBase glBindBufferBase;
static PFN_GenFramebuffers glGenFramebuffers;
static PFN_BindFramebuffer glBindFramebuffer;
static PFN_FramebufferTexture2D glFramebufferTexture2D;
static PFN_BlitFramebuffer glBlitFramebuffer;

typedef BOOL(WINAPI* PFN_wglChoosePixelFormatARB)(
	HDC, const int*, const FLOAT*, UINT, int*, UINT*);
typedef HGLRC(WINAPI* PFN_wglCreateContextAttribsARB)(
	HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFN_wglSwapIntervalEXT)(int);

static PFN_wglChoosePixelFormatARB wglChoosePixelFormatARB;
static PFN_wglCreateContextAttribsARB wglCreateContextAttribsARB;
static PFN_wglSwapIntervalEXT wglSwapIntervalEXT;

#define LOAD_GL(name) \
	gl##name = (PFN_##name)(void*)wglGetProcAddress("gl" #name); \
	if (!gl##name) { \
		LOG("GL load failed: gl%s\n", #name); \
		return false; \
	}


static bool load_gl() {
	LOAD_GL(CreateShader);
	LOAD_GL(ShaderSource);
	LOAD_GL(CompileShader);
	LOAD_GL(GetShaderiv);
	LOAD_GL(GetShaderInfoLog);
	LOAD_GL(CreateProgram);
	LOAD_GL(AttachShader);
	LOAD_GL(LinkProgram);
	LOAD_GL(GetProgramiv);
	LOAD_GL(GetProgramInfoLog);
	LOAD_GL(DeleteShader);
	LOAD_GL(DeleteProgram);
	LOAD_GL(UseProgram);
	LOAD_GL(GetUniformLocation);
	LOAD_GL(Uniform1f);
	LOAD_GL(Uniform2f);
	LOAD_GL(Uniform1i);
	LOAD_GL(Uniform1fv);
	LOAD_GL(DispatchCompute);
	LOAD_GL(MemoryBarrier);
	LOAD_GL(BindImageTexture);
	LOAD_GL(ActiveTexture);
	LOAD_GL(GenBuffers);
	LOAD_GL(BindBuffer);
	LOAD_GL(BufferData);
	LOAD_GL(BufferSubData);
	LOAD_GL(BindBufferBase);
	LOAD_GL(GenFramebuffers);
	LOAD_GL(BindFramebuffer);
	LOAD_GL(FramebufferTexture2D);
	LOAD_GL(BlitFramebuffer);
	return true;
}


// ---------------------------------------------------------------------------
// Internal state.
// ---------------------------------------------------------------------------
#define MAX_IMAGES 16
#define MAX_BUFFERS 16
#define MAX_PASSES 32
#define MAX_CONTROLS 16
#define MAX_KEYS 64

typedef struct {
	bool dbl;
	bool externalSize; // if true, skip on resize / clear
	GLuint tex[2];
	// index of the front (readable) buffer
	int cur;
} Img;

typedef struct {
	GLuint ssbo;
	size_t bytes;
} Buf;

typedef struct {
	char name[64];
	// host-owned static string
	const char* embedded;
	char file[288];
	IntroSchedule sched;
	int interval;
	IntroDispatch dispatch;
	int gx, gy, gz;
	int lx, ly;
	int write;
	int reads[INTRO_MAX_READS];
	int readCount;
	int buffers[INTRO_MAX_BUFFERS];
	int bufferCount;
	GLuint program;
	GLint uTime, uFrame, uSlots;
	GLint ctrlLoc[MAX_CONTROLS];
	long long mtime;
} Pass;

typedef struct {
	char name[64];
	float value, def, lo, hi;
} Control;

typedef struct {
	int vk;
	int ctrl;
	float delta;
	bool reset;
	int selectSlot;
	bool slotDelta;
} KeyBind;

static struct {
	HWND win;
	HDC dc;
	HGLRC rc;
	int w, h, localSize;
	int renderResolutionWidth;
	int renderResolutionHeight;
	bool vsync, hotReload, running, resized;
	char shaderDir[256];
	GLuint fbo;
	Img img[MAX_IMAGES];
	int imgCount;
	Buf buf[MAX_BUFFERS];
	int bufCount;
	Pass pass[MAX_PASSES];
	int passCount;
	Control ctrl[MAX_CONTROLS];
	int ctrlCount;
	KeyBind key[MAX_KEYS];
	int keyCount;
	int present;
	int frame;
	float time;
	LARGE_INTEGER freq, t0;

	float slot[INTRO_SLOTS];
	int selectedSlot;

	long long sharedMtime;
} E;


// ---------------------------------------------------------------------------
// Small helpers.
// ---------------------------------------------------------------------------
static GLuint img_front(const Img& im) { return im.tex[im.cur]; }


static GLuint img_back(const Img& im) {
	return im.dbl ? im.tex[1 - im.cur] : im.tex[0];
}


static void copy_str(char* dst, const char* src, int n) {
	int i = 0;
	if (src) {
		for (; src[i] && i < n - 1; i++) dst[i] = src[i];
	}
	dst[i] = 0;
}


static GLuint compile_compute(const char* src) {
	GLuint s = glCreateShader(GL_COMPUTE_SHADER);
	glShaderSource(s, 1, &src, 0);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetShaderInfoLog(s, sizeof(log), 0, log);
		LOG("compute compile error:\n%s\n", log);
		LOG("^^ shader failed: %s.comp\n", E.pass[E.passCount].name);
		glDeleteShader(s);
		return 0;
	}
	GLuint p = glCreateProgram();
	glAttachShader(p, s);
	glLinkProgram(p);
	glDeleteShader(s);
	GLint lok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &lok);
	if (!lok) {
		char log[4096];
		glGetProgramInfoLog(p, sizeof(log), 0, log);
		LOG("program link error:\n%s\n", log);
		glDeleteProgram(p);
		return 0;
	}
	return p;
}


static void adopt(Pass* p, GLuint prog) {
	if (p->program) glDeleteProgram(p->program);
	p->program = prog;
	p->uTime = glGetUniformLocation(prog, "iTime");
	p->uFrame = glGetUniformLocation(prog, "iFrame");
	p->uSlots = glGetUniformLocation(prog, "uSlots");
	for (int c = 0; c < E.ctrlCount; c++)
		p->ctrlLoc[c] = glGetUniformLocation(prog, E.ctrl[c].name);
}


#ifndef INTRO_SIZE
static char* read_file(const char* path, long long* mtimeOut) {
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	if (mtimeOut) *mtimeOut = (long long)st.st_mtime;
	FILE* f = fopen(path, "rb");
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* buf = (char*)malloc(len + 1);
	fread(buf, 1, len, f);
	buf[len] = 0;
	fclose(f);
	return buf;
}


static bool reload_from_disk(Pass* p, bool force) {
	struct stat st;
	if (stat(p->file, &st) != 0) return false;
	long long m = (long long)st.st_mtime;
	if (!force && m == p->mtime) return false;
	char* src = read_file(p->file, &p->mtime);
	if (!src) return false;

	// insert src/shared.glsl so assume it can be grabbed from ../src/shared.glsl
	// just malloc a huge buffe to slam shit in
	const char * version = (
		"#version 460 core\n"
		"layout(local_size_x = 8, local_size_y = 8) in;\n"
		"#line 0\n"
	);
	size_t const versionStrlen = strlen(version);
	char * const header = read_file("../src/shared.glsl", 0);
	size_t const headerStrlen = strlen(header);
	size_t const srcStrlen = strlen(src);
	size_t const srcoutStrlen = versionStrlen + headerStrlen + srcStrlen;
	char * const srcout = (char *)malloc(srcoutStrlen + 1);
	{
		// insert at top
		memcpy(srcout, version, strlen(version));
		memcpy(srcout + versionStrlen, header, strlen(header));
		memcpy(srcout + versionStrlen + headerStrlen, src, strlen(src));
		srcout[srcoutStrlen] = '\0';
	}

	GLuint prog = compile_compute(srcout);
	free(src);
	free(srcout);
	// keep the last good program
	if (!prog) return false;
	adopt(p, prog);
	static int counter = 0;
	++counter;
	LOG("   [%d] reloaded %s\n", counter, p->file);
	return true;
}


static void dump_default(const Pass* p) {
	struct stat st;
	// never clobber edits
	if (stat(p->file, &st) == 0) return;
	if (!p->embedded) return;
	FILE* f = fopen(p->file, "wb");
	if (f) {
		fputs(p->embedded, f);
		fclose(f);
		LOG("wrote %s\n", p->file);
	}
}
#endif


static void make_image_storage(Img* im) {
	if (im->externalSize) return;
	int n = im->dbl ? 2 : 1;
	for (int k = 0; k < n; k++) {
		if (!im->tex[k]) glGenTextures(1, &im->tex[k]);
		glBindTexture(GL_TEXTURE_2D, im->tex[k]);
		glTexImage2D(
			GL_TEXTURE_2D,
			0, GL_RGBA16F,
			E.renderResolutionWidth, E.renderResolutionHeight,
			0, GL_RGBA,
			GL_FLOAT, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	im->cur = 0;
}


static void clear_image(const Img* im) {
	if (im->externalSize) return;
	glBindFramebuffer(GL_FRAMEBUFFER, E.fbo);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	int n = im->dbl ? 2 : 1;
	for (int k = 0; k < n; k++) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
							   GL_TEXTURE_2D, im->tex[k], 0);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


static void run_pass(Pass* p) {
	glUseProgram(p->program);
	if (p->uTime >= 0) glUniform1f(p->uTime, E.time);
	if (p->uFrame >= 0) glUniform1i(p->uFrame, E.frame);
	if (p->uSlots >= 0) glUniform1fv(p->uSlots, INTRO_SLOTS, E.slot);
	for (int c = 0; c < E.ctrlCount; c++)
		if (p->ctrlLoc[c] >= 0) glUniform1f(p->ctrlLoc[c], E.ctrl[c].value);

	for (int i = 0; i < p->readCount; i++) {
		const Img& im = E.img[p->reads[i]];
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, img_front(im));
	}
	if (p->write >= 0) {
		glBindImageTexture(0, img_back(E.img[p->write]), 0, GL_FALSE, 0,
						   GL_READ_WRITE, GL_RGBA16F);
	}

	for (int i = 0; i < p->bufferCount; i++) {
		const Buf& b = E.buf[p->buffers[i]];
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, b.ssbo);
	}

	if (p->dispatch == IntroDispatch_Image) {
		int lx = p->lx > 0 ? p->lx : E.localSize;
		int ly = p->ly > 0 ? p->ly : E.localSize;
		glDispatchCompute((GLuint)((E.renderResolutionWidth + lx - 1) / lx),
						  (GLuint)((E.renderResolutionHeight + ly - 1) / ly), 1);
	} else {
		glDispatchCompute((GLuint)p->gx, (GLuint)p->gy, (GLuint)p->gz);
	}

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
					GL_TEXTURE_FETCH_BARRIER_BIT |
					GL_SHADER_STORAGE_BARRIER_BIT);

	if (p->write >= 0 && E.img[p->write].dbl) E.img[p->write].cur ^= 1;
}


static void present() {
	glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);
	GLuint tex = img_front(E.img[E.present]);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, E.fbo);
	glFramebufferTexture2D(
		GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0
	);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBlitFramebuffer(
		0, 0, E.renderResolutionWidth, E.renderResolutionHeight,
		0, 0, E.w,  E.h,
		GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


static void on_key(int vk) {
	for (int i = 0; i < E.keyCount; i++) {
		const KeyBind& b = E.key[i];
		if (b.vk != vk) continue;
		// slot selection
		if (b.selectSlot >= 0) {
			E.selectedSlot = b.selectSlot;
			return;
		}
		printf("slot %d -> %.3f\n", E.selectedSlot, E.slot[E.selectedSlot]);
		// nudge the currently-selected slot
		if (b.slotDelta) {
			int s = E.selectedSlot;
			float v = E.slot[s] + b.delta;
			E.slot[s] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
			printf("slot %d -> %.3f\n", s, E.slot[s]);
			return;
		}
		// normal control
		Control& c = E.ctrl[b.ctrl];
		if (b.reset) {
			c.value = c.def;
		} else {
			float v = c.value + b.delta;
			c.value = v < c.lo ? c.lo : (v > c.hi ? c.hi : v);
		}
		return;
	}
}


static void run_startup_passes() {
	for (int i = 0; i < E.passCount; i++)
		if (E.pass[i].sched == IntroSchedule_Startup) run_pass(&E.pass[i]);
}


// ---------------------------------------------------------------------------
// Window + context.
// ---------------------------------------------------------------------------
static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_SIZE:
			E.w = LOWORD(lp);
			E.h = HIWORD(lp);
			if (E.w < 1) E.w = 1;
			if (E.h < 1) E.h = 1;
			E.resized = true;
			return 0;
		case WM_KEYDOWN:
			// if (wp == VK_ESCAPE) E.running = false; else
			{ on_key((int)wp); 

				// for now reset
				E.frame = 0;
				E.time = 0.0f;
				run_startup_passes();
			}

			return 0;
		case WM_CLOSE:
		case WM_DESTROY:
			E.running = false;
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProc(h, msg, wp, lp);
}


// Burn a throwaway window+context to fetch the ARB entry points.
static bool load_wgl(HINSTANCE inst) {
	WNDCLASSA wc = {};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = inst;
	wc.lpszClassName = "dummygl";
	RegisterClassA(&wc);
	HWND dh = CreateWindowA("dummygl", "", 0, 0, 0, 1, 1, 0, 0, inst, 0);
	if (!dh) {
		LOG("dummy window failed: no display driver\n");
		return false;
	}
	HDC dc = GetDC(dh);
	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	int pf = ChoosePixelFormat(dc, &pfd);
	SetPixelFormat(dc, pf, &pfd);
	HGLRC rc = wglCreateContext(dc);
	if (!rc) {
		LOG("wglCreateContext failed\n");
		return false;
	}
	wglMakeCurrent(dc, rc);

	wglChoosePixelFormatARB = (PFN_wglChoosePixelFormatARB)(void*)
		wglGetProcAddress("wglChoosePixelFormatARB");
	wglCreateContextAttribsARB = (PFN_wglCreateContextAttribsARB)(void*)
		wglGetProcAddress("wglCreateContextAttribsARB");
	wglSwapIntervalEXT = (PFN_wglSwapIntervalEXT)(void*)
		wglGetProcAddress("wglSwapIntervalEXT");

	wglMakeCurrent(0, 0);
	wglDeleteContext(rc);
	ReleaseDC(dh, dc);
	DestroyWindow(dh);

	if (!wglChoosePixelFormatARB || !wglCreateContextAttribsARB) {
		LOG("WGL ARB extensions missing\n");
		return false;
	}
	return true;
}


bool intro_init(const IntroConfig* cfg) {
	E.w = cfg->width > 0 ? cfg->width : 800;
	E.h = cfg->height > 0 ? cfg->height : 600;
	E.renderResolutionWidth = cfg->width;
	E.renderResolutionHeight = cfg->height;
	E.localSize = cfg->localSize > 0 ? cfg->localSize : 8;
	E.vsync = cfg->vsync;
	E.hotReload = cfg->hotReload;
	copy_str(E.shaderDir, cfg->shaderDir ? cfg->shaderDir : "shaders",
			 sizeof(E.shaderDir));
	E.present = INTRO_NONE;
	E.running = true;
	E.resized = false;

	HINSTANCE inst = GetModuleHandle(0);
	if (!load_wgl(inst)) return false;

	WNDCLASSA wc = {};
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.lpszClassName = "introgl";
	RegisterClassA(&wc);

	RECT r = { 0, 0, E.w, E.h };
	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	E.win = CreateWindowA(
		"introgl", cfg->title ? cfg->title : "intro",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
		r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
	if (!E.win) {
		LOG("window creation failed\n");
		return false;
	}
	E.dc = GetDC(E.win);

	const int pfAttribs[] = {
		WGL_DRAW_TO_WINDOW_ARB, 1,
		WGL_SUPPORT_OPENGL_ARB, 1,
		WGL_DOUBLE_BUFFER_ARB, 1,
		WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
		WGL_COLOR_BITS_ARB, 32,
		WGL_DEPTH_BITS_ARB, 24,
		WGL_STENCIL_BITS_ARB, 8,
		WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
		0,
	};
	int pf;
	UINT n;
	wglChoosePixelFormatARB(E.dc, pfAttribs, 0, 1, &pf, &n);
	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize = sizeof(pfd);
	SetPixelFormat(E.dc, pf, &pfd);

	const int ctxAttribs[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
		WGL_CONTEXT_MINOR_VERSION_ARB, 3,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		0,
	};
	E.rc = wglCreateContextAttribsARB(E.dc, 0, ctxAttribs);
	if (!E.rc) {
		LOG("4.3 core context creation failed\n");
		return false;
	}
	wglMakeCurrent(E.dc, E.rc);

	if (!load_gl()) return false;
	if (wglSwapIntervalEXT) wglSwapIntervalEXT(E.vsync ? 1 : 0);
	glGenFramebuffers(1, &E.fbo);
	return true;
}


IntroImage intro_add_image(const IntroImageDesc* desc) {
	if (E.imgCount >= MAX_IMAGES) {
		LOG("too many images\n");
		return INTRO_NONE;
	}
	Img* im = &E.img[E.imgCount];
	im->dbl = desc->doubleBuffered;
	im->externalSize = false;
	im->tex[0] = im->tex[1] = 0;
	im->cur = 0;
	make_image_storage(im);
	clear_image(im);
	return E.imgCount++;
}


IntroBuffer intro_add_buffer(const IntroBufferDesc* desc) {
	if (E.bufCount >= MAX_BUFFERS) {
		LOG("too many buffers\n");
		return INTRO_NONE;
	}
	Buf* b = &E.buf[E.bufCount];
	b->bytes = desc->bytes;
	glGenBuffers(1, &b->ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, b->ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)b->bytes, 0,
				 GL_DYNAMIC_DRAW);
	return E.bufCount++;
}


void intro_buffer_upload(IntroBuffer buf, const void* data, size_t bytes) {
	if (buf < 0 || buf >= E.bufCount) return;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, E.buf[buf].ssbo);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)bytes, data);
}


#ifndef INTRO_SIZE
IntroImage intro_load_texture_png(const char* path) {
	if (E.imgCount >= MAX_IMAGES) {
		LOG("too many images\n");
		return INTRO_NONE;
	}
	int w, h;
	unsigned char* pixels = stbi_load(path, &w, &h, NULL, 4);
	if (!pixels) {
		LOG("failed to load texture: %s\n", path);
		return INTRO_NONE;
	}
	Img* im = &E.img[E.imgCount];
	im->dbl = false;
	im->externalSize = true;
	im->cur = 0;
	im->tex[0] = im->tex[1] = 0;
	glGenTextures(1, &im->tex[0]);
	glBindTexture(GL_TEXTURE_2D, im->tex[0]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	stbi_image_free(pixels);
	LOG("loaded texture: %s (%dx%d)\n", path, w, h);
	return E.imgCount++;
}
#else
IntroImage intro_load_texture_png(const char* path) {
	(void)path;
	return INTRO_NONE;
}
#endif


void intro_add_pass(const IntroPassDesc* desc) {
	if (E.passCount >= MAX_PASSES) {
		LOG("too many passes\n");
		return;
	}
	Pass* p = &E.pass[E.passCount];
	*p = (Pass){};
	copy_str(p->name, desc->name, sizeof(p->name));
	p->embedded = desc->embedded;
	snprintf(p->file, sizeof(p->file), "%s/%s.comp", E.shaderDir, p->name);
	p->sched = desc->sched;
	p->interval = desc->interval;
	p->dispatch = desc->dispatch;
	p->gx = desc->gx;
	p->gy = desc->gy;
	p->gz = desc->gz;
	p->lx = desc->localX;
	p->ly = desc->localY;
	p->write = desc->write;
	p->readCount = desc->readCount;
	p->bufferCount = desc->bufferCount;
	for (int i = 0; i < desc->readCount; i++) p->reads[i] = desc->reads[i];
	for (int i = 0; i < desc->bufferCount; i++) p->buffers[i] = desc->buffers[i];

	bool fromDisk = false;
#ifndef INTRO_SIZE
	if (E.hotReload) {
		CreateDirectoryA(E.shaderDir, 0);
		dump_default(p);
		fromDisk = reload_from_disk(p, true);
		// reset time/frame so that shader changes are visible immediately
		E.frame = 0;
		E.time = 0.0f;
	}
#endif
	if (!fromDisk) {
		GLuint prog = compile_compute(p->embedded);
		if (!prog) {
			LOG("pass '%s' failed to compile\n", p->name);
			return;
		}
		adopt(p, prog);
	}
	E.passCount++;
}


void intro_set_present(IntroImage img) { E.present = img; }


IntroControl intro_add_control(const char* name, float def, float lo,
							   float hi) {
	if (E.ctrlCount >= MAX_CONTROLS) {
		LOG("too many controls\n");
		return INTRO_NONE;
	}
	int idx = E.ctrlCount++;
	Control* c = &E.ctrl[idx];
	copy_str(c->name, name, sizeof(c->name));
	c->def = c->value = def;
	c->lo = lo;
	c->hi = hi;
	// Resolve this control's location in any already-compiled programs.
	for (int i = 0; i < E.passCount; i++)
		E.pass[i].ctrlLoc[idx] =
			glGetUniformLocation(E.pass[i].program, c->name);
	return idx;
}


void intro_bind_key(int vk, IntroControl ctrl, float delta) {
	if (E.keyCount >= MAX_KEYS || ctrl < 0) return;
	E.key[E.keyCount++] = (KeyBind){ vk, ctrl, delta, false, -1, false };
}

void intro_bind_reset(int vk, IntroControl ctrl) {
	if (E.keyCount >= MAX_KEYS || ctrl < 0) return;
	E.key[E.keyCount++] = (KeyBind){ vk, ctrl, 0.0f, true, -1, false };
}

void intro_bind_select(int vk, int slot) {
	if (E.keyCount >= MAX_KEYS || slot < 0 || slot >= INTRO_SLOTS)
		return;
	E.key[E.keyCount++] = (KeyBind){ vk, -1, 0.0f, false, slot, false };
}

void intro_bind_slot_key(int vk, float delta) {
	if (E.keyCount >= MAX_KEYS) return;
	E.key[E.keyCount++] = (KeyBind){ vk, -1, delta, false, -1, true };
}

float intro_slot_value(int slot) {
	if (slot < 0 || slot >= INTRO_SLOTS) return 0.0f;
	return E.slot[slot];
}


void intro_run(void) {
	QueryPerformanceFrequency(&E.freq);
	QueryPerformanceCounter(&E.t0);
	run_startup_passes();

	while (E.running) {
		MSG msg;
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// don't need to with internal fixed resolution
		// if (E.resized) {
		// 	for (int i = 0; i < E.imgCount; i++) {
		// 		make_image_storage(&E.img[i]);
		// 		clear_image(&E.img[i]);
		// 	}
		// 	// re-seed anything size-dependent
		// 	run_startup_passes();
		// 	E.resized = false;
		// }

#ifndef INTRO_SIZE
		if (E.hotReload && (E.frame & 15) == 0) {
			for (int i = 0; i < E.passCount; i++) {
				bool changed = reload_from_disk(&E.pass[i], false);
				if (changed && E.pass[i].sched == IntroSchedule_Startup) {
					run_pass(&E.pass[i]);
				}
				if (changed) {
					E.frame = 0;
					E.time = 0;
				}
			}
		}

		// also check for shared.glsl change

		{
			struct stat st;
			if (stat("../src/shared.glsl", &st) == 0) {
				long long m = (long long)st.st_mtime;
				if (m != E.sharedMtime) {
					E.sharedMtime = m;
					for (int i = 0; i < E.passCount; i++) {
						reload_from_disk(&E.pass[i], true);
						if (E.pass[i].sched == IntroSchedule_Startup)
							run_pass(&E.pass[i]);
					}
				}
			}
		}
#endif

		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		E.time = (float)(now.QuadPart - E.t0.QuadPart) /
				 (float)E.freq.QuadPart;

		for (int i = 0; i < E.passCount; i++) {
			Pass* p = &E.pass[i];
			if (p->sched == IntroSchedule_EveryFrame) {
				run_pass(p);
			} else if (p->sched == IntroSchedule_Buffered) {
				int iv = p->interval < 1 ? 1 : p->interval;
				if (E.frame % iv == 0) run_pass(p);
			}
		}

		if (E.present >= 0) present();
		SwapBuffers(E.dc);
		E.frame = (E.frame + 1) % INT32_MAX;
	}
}


void intro_shutdown(void) {
	wglMakeCurrent(0, 0);
	if (E.rc) wglDeleteContext(E.rc);
	if (E.win) {
		ReleaseDC(E.win, E.dc);
		DestroyWindow(E.win);
	}
}

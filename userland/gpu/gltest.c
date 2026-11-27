/*
 * gltest (whyred): Mesa through OSMesa without CoreAnimation. Clears a 256x256 buffer, draws one triangle, reads
 * the pixels back and says which driver drew (GL_RENDERER: freedreno "FD509" on the GPU, "softpipe" without it).
 * Then times N frames of clear + triangle + glFinish. Run as root on the phone: gltest [N].
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>
#include <GLES2/gl2.h>

typedef struct osmesa_context *OSMesaContext;
extern OSMesaContext OSMesaCreateContextAttribs(const int *attributes, OSMesaContext share);
extern unsigned char OSMesaMakeCurrent(OSMesaContext context, void *buffer, unsigned type, int width, int height);

#define OSMESA_FORMAT 0x22
#define OSMESA_RGBA 0x1908
#define OSMESA_PROFILE 0x33
#define OSMESA_ES_PROFILE 0x38  /* ES profile patch (userland/mesa/osmesa-es-profile.patch) */
#define OSMESA_CONTEXT_MAJOR_VERSION 0x36
#define W 256
#define H 256

static const char *vs = "attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }";
static const char *fs = "precision mediump float; void main() { gl_FragColor = vec4(1.0, 0.5, 0.0, 1.0); }";

static GLuint
shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512] = "";
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf("gltest: shader: %s\n", log);
	}
	return s;
}

static void
frame(GLuint prog)
{
	static const float tri[] = { -0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f };
	glViewport(0, 0, W, H);
	glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(prog);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

int
main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 100;
	static uint32_t buf[W * H];
	const int attr[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_PROFILE, OSMESA_ES_PROFILE,
	                     OSMESA_CONTEXT_MAJOR_VERSION, 2, 0 };
	OSMesaContext ctx = OSMesaCreateContextAttribs(attr, NULL);
	if (ctx == NULL || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
		printf("gltest: no OSMesa context\n");
		return 1;
	}
	printf("gltest: GL_RENDERER %s\ngltest: GL_VERSION %s\n", (const char *)glGetString(GL_RENDERER),
	    (const char *)glGetString(GL_VERSION));
	GLuint prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(prog, 0, "p");
	glLinkProgram(prog);
	frame(prog);
	uint32_t px[4] = { 0 };
	glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);       /* inside the triangle */
	glReadPixels(2, H - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &px[1]);           /* outside: clear colour */
	printf("gltest: centre %08x (want ff0080ff, orange), corner %08x (want ffff0000, blue), glGetError %#x\n",
	    px[0], px[1], glGetError());
	int good = (px[0] == 0xff0080ffu || px[0] == 0xff007fffu) && px[1] == 0xffff0000u;    /* 0.5 rounds either way */
	uint64_t t0 = mach_absolute_time();
	for (int i = 0; i < n; i++) {
		frame(prog);
		glFinish();
	}
	uint64_t t1 = mach_absolute_time();
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	uint64_t us = (t1 - t0) * tb.numer / tb.denom / 1000;
	printf("gltest: %d frames in %llu us (%llu us each) %s\n", n, (unsigned long long)us,
	    n ? (unsigned long long)(us / (uint64_t)n) : 0ull, good ? "PIXELS OK" : "PIXELS WRONG");
	return good ? 0 : 1;
}

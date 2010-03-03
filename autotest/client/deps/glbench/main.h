// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BENCH_GL_MAIN_H_
#define BENCH_GL_MAIN_H_

#include <sys/time.h>

#ifdef USE_GLES
#include <EGL/egl.h>
#include <GLES/gl.h>
#else
#include <GL/gl.h>

#define LIST_PROC_FUNCTIONS(F) \
    F(glAttachShader, PFNGLATTACHSHADERPROC) \
    F(glBindBuffer, PFNGLBINDBUFFERPROC) \
    F(glBufferData, PFNGLBUFFERDATAPROC) \
    F(glCompileShader, PFNGLCOMPILESHADERPROC) \
    F(glCreateProgram, PFNGLCREATEPROGRAMPROC) \
    F(glCreateShader, PFNGLCREATESHADERPROC) \
    F(glDeleteBuffers, PFNGLDELETEBUFFERSPROC) \
    F(glDeleteProgram, PFNGLDELETEPROGRAMPROC) \
    F(glDeleteShader, PFNGLDELETESHADERPROC) \
    F(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC) \
    F(glGenBuffers, PFNGLGENBUFFERSPROC) \
    F(glGetAttribLocation, PFNGLGETATTRIBLOCATIONPROC) \
    F(glGetInfoLogARB, PFNGLGETPROGRAMINFOLOGPROC) \
    F(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC) \
    F(glLinkProgram, PFNGLLINKPROGRAMPROC) \
    F(glShaderSource, PFNGLSHADERSOURCEPROC) \
    F(glUniform1f, PFNGLUNIFORM1FPROC) \
    F(glUniform1i, PFNGLUNIFORM1IPROC) \
    F(glUseProgram, PFNGLUSEPROGRAMPROC) \
    F(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)

#define F(fun, type) extern type fun;
LIST_PROC_FUNCTIONS(F)
#undef F

#endif

inline uint64_t GetUTime() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return static_cast<uint64_t>(tv.tv_usec) +
    1000000ULL*static_cast<uint64_t>(tv.tv_sec);
}

extern GLint g_width;
extern GLint g_height;

bool Init();
bool InitContext();
void DestroyContext();
void SwapBuffers();

typedef void (*BenchFunc)(int iter);

uint64_t TimeBench(BenchFunc func, int iter);
void Bench(BenchFunc func, float *slope, int64_t *bias);

void *MmapFile(const char *name, size_t *length);

#endif  // BENCH_GL_MAIN_H_

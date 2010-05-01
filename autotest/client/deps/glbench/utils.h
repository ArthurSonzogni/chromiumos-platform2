// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BENCH_GL_UTILS_H_
#define BENCH_GL_UTILS_H_

#ifdef USE_EGL
#include "GLES2/gl2.h"
#else
#include "GL/gl.h"
#endif


void SetBasePathFromArgv0(const char* argv0, const char* relative);
void *MmapFile(const char *name, size_t *length);
GLuint InitShaderProgram(const char *vertex_src, const char *fragment_src);

#endif // BENCH_GL_UTILS_H_

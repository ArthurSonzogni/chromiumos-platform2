// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "main.h"
#include "xlib_window.h"

GLXContext glx_context = NULL;

PFNGLGENBUFFERSPROC glGenBuffers = 0;
PFNGLBINDBUFFERPROC glBindBuffer = 0;
PFNGLBUFFERDATAPROC glBufferData = 0;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = 0;

bool Init() {
  return XlibInit();
}

bool InitContext() {
  XWindowAttributes attributes;
  XGetWindowAttributes(xlib_display, xlib_window, &attributes);
  XVisualInfo visual_info_template;
  visual_info_template.visualid = XVisualIDFromVisual(attributes.visual);
  int visual_info_count = 0;
  XVisualInfo *visual_info_list = XGetVisualInfo(xlib_display, VisualIDMask,
                                                 &visual_info_template,
                                                 &visual_info_count);
  glx_context = 0;
  for (int i = 0; i < visual_info_count; ++i) {
    glx_context = glXCreateContext(xlib_display, visual_info_list + i, 0, True);
    if (glx_context) break;
  }
  XFree(visual_info_list);
  if (!glx_context) {
    return false;
  }
  if (!glXMakeCurrent(xlib_display, xlib_window, glx_context)) {
    glXDestroyContext(xlib_display, glx_context);
    return false;
  }

  glClearColor(1.f, 0, 0, 1.f);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  SwapBuffers();
  glClearColor(0, 1.f, 0, 1.f);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  SwapBuffers();
  glClearColor(0, 0, 0.f, 0.f);

  const GLubyte *str = glGetString(GL_EXTENSIONS);
  if (!str || !strstr(reinterpret_cast<const char *>(str),
                      "GL_ARB_vertex_buffer_object"))
    return false;

  glGenBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(
    glXGetProcAddress(reinterpret_cast<const GLubyte *>("glGenBuffers")));
  glBindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(
    glXGetProcAddress(reinterpret_cast<const GLubyte *>("glBindBuffer")));
  glBufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(
    glXGetProcAddress(reinterpret_cast<const GLubyte *>("glBufferData")));
  glDeleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(
    glXGetProcAddress(reinterpret_cast<const GLubyte *>("glDeleteBuffers")));

  return true;
}

void DestroyContext() {
  glXMakeCurrent(xlib_display, 0, NULL);
  glXDestroyContext(xlib_display, glx_context);
}

void SwapBuffers() {
  glXSwapBuffers(xlib_display, xlib_window);
}

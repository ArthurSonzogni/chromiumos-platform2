// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "base/logging.h"

#include "main.h"
#include "shaders.h"
#include "utils.h"
#include "yuv2rgb.h"


const int enabled_tests_max = 8;
const char *enabled_tests[enabled_tests_max+1] = {NULL};
int seconds_to_run = 0;

// Runs Bench on a test function f and prints out results.  Bench function
// returns the slope and bias of a linear model relating the argument passed to
// f and time it took f to run.  Normally the argument of f is assumed to be
// number of iterations an operation is executed.
//
// coefficient is multiplied (if inverse is false) or divided (if inverse is
// true) by the slope and the result is printed.
//
// The test will not run if enabled_tests is nonempty and no string in
// enabled_tests is contained in name.
//
// Examples:
//   coefficient = width * height (measured in pixels), inverse = true
//       returns the throughput in megapixels per second;
//
//   coefficient = 1, inverse = false
//       returns number of operations per second.
void RunTest(BenchFunc f, const char *name, float coefficient, bool inverse) {
  float slope;
  int64_t bias;

  if (enabled_tests[0]) {
    bool found = false;
    for (const char **e = enabled_tests; *e; e++)
      if (strstr(name, *e)) {
        found = true;
        break;
      }
    if (!found)
      return;
  }

  GLenum err = glGetError();
  if (err != 0) {
    printf("# %s failed, glGetError returned 0x%x.\n", name, err);
    // float() in python will happily parse Nan.
    printf("%s: Nan\n", name);
  } else {
    if (Bench(f, &slope, &bias)) {
      printf("%s: %g\n", name, coefficient * (inverse ? 1.f / slope : slope));
    } else {
      printf("# %s is too slow, returning zero.\n", name);
      printf("%s: 0\n", name);
    }
  }
}

static int arg1 = 0;
static void *arg2 = NULL;

void SwapTestFunc(int iter) {
  for (int i = 0 ; i < iter; ++i) {
    SwapBuffers();
  }
}

void SwapTest() {
  RunTest(SwapTestFunc, "us_swap_swap", 1.f, false);
}

void ClearTestFunc(int iter) {
  GLbitfield mask = arg1;
  glClear(mask);
  glFlush();  // Kick GPU as soon as possible
  for (int i = 0 ; i < iter-1; ++i) {
    glClear(mask);
  }
}


void ClearTest() {
  arg1 = GL_COLOR_BUFFER_BIT;
  RunTest(ClearTestFunc, "mpixels_sec_clear_color", g_width * g_height, true);

  arg1 = GL_DEPTH_BUFFER_BIT;
  RunTest(ClearTestFunc, "mpixels_sec_clear_depth", g_width * g_height, true);

  arg1 = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
  RunTest(ClearTestFunc, "mpixels_sec_clear_colordepth",
          g_width * g_height, true);

  arg1 = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
  RunTest(ClearTestFunc, "mpixels_sec_clear_depthstencil",
          g_width * g_height, true);

  arg1 = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
  RunTest(ClearTestFunc, "mpixels_sec_clear_colordepthstencil",
          g_width * g_height, true);
}


GLuint SetupVBO(GLenum target, GLsizeiptr size, const GLvoid *data) {
  GLuint buf = ~0;
  glGenBuffers(1, &buf);
  glBindBuffer(target, buf);
  glBufferData(target, size, data, GL_STATIC_DRAW);
  CHECK(!glGetError());
  return buf;
}


GLuint SetupTexture(GLsizei size_log2) {
  GLsizei size = 1 << size_log2;
  GLuint name = ~0;
  glGenTextures(1, &name);
  glBindTexture(GL_TEXTURE_2D, name);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  unsigned char *pixels = new unsigned char[size * size * 4];
  if (!pixels)
    return 0;

  for (GLint level = 0; size > 0; level++, size /= 2) {
    unsigned char *p = pixels;
    for (int i = 0; i < size; i++) {
      for (int j = 0; j < size; j++) {
        *p++ = level %3 != 0 ? (i ^ j) << level : 0;
        *p++ = level %3 != 1 ? (i ^ j) << level : 0;
        *p++ = level %3 != 2 ? (i ^ j) << level : 0;
        *p++ = 255;
      }
    }
    if (size == 1) {
      unsigned char *p = pixels;
      *p++ = 255;
      *p++ = 255;
      *p++ = 255;
      *p++ = 255;
    }
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  }
  delete[] pixels;
  return name;
}


static void DrawArraysTestFunc(int iter);
static void FillRateTestNormal(const char *name, float coeff=1.f);

#ifndef USE_EGL
static void FillRateTestBlendDepth(const char *name);

void FillRateTest() {
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  glDisable(GL_DEPTH_TEST);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  GLfloat buffer_vertex[8] = {
    -1.f, -1.f,
    1.f,  -1.f,
    -1.f, 1.f,
    1.f,  1.f,
  };
  GLfloat buffer_texture[8] = {
    0.f, 0.f,
    1.f, 0.f,
    0.f, 1.f,
    1.f, 1.f,
  };
  glEnableClientState(GL_VERTEX_ARRAY);

  GLuint vbo_vertex = SetupVBO(GL_ARRAY_BUFFER,
                               sizeof(buffer_vertex), buffer_vertex);
  glVertexPointer(2, GL_FLOAT, 0, 0);

  GLuint vbo_texture = SetupVBO(GL_ARRAY_BUFFER,
                                sizeof(buffer_texture), buffer_texture);
  glTexCoordPointer(2, GL_FLOAT, 0, 0);

  glColor4f(1.f, 0.f, 0.f, 1.f);
  FillRateTestNormal("fill_solid");
  FillRateTestBlendDepth("fill_solid");

  glColor4f(1.f, 1.f, 1.f, 1.f);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glEnable(GL_TEXTURE_2D);

  GLuint texture = SetupTexture(9);
  FillRateTestNormal("fill_tex_nearest");

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  FillRateTestNormal("fill_tex_bilinear");

  // lod = 0.5
  glScalef(0.7071f, 0.7071f, 1.f);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_NEAREST);
  FillRateTestNormal("fill_tex_trilinear_nearest_05", 0.7071f * 0.7071f);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  FillRateTestNormal("fill_tex_trilinear_linear_05", 0.7071f * 0.7071f);

  // lod = 0.4
  glLoadIdentity();
  glScalef(0.758f, 0.758f, 1.f);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  FillRateTestNormal("fill_tex_trilinear_linear_04", 0.758f * 0.758f);

  // lod = 0.1
  glLoadIdentity();
  glScalef(0.933f, 0.933f, 1.f);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  FillRateTestNormal("fill_tex_trilinear_linear_01", 0.933f * 0.933f);

  glDeleteBuffers(1, &vbo_vertex);
  glDeleteBuffers(1, &vbo_texture);
  glDeleteTextures(1, &texture);
}
#endif


static void FillRateTestNormal(const char *name, float coeff) {
  const int buffer_len = 64;
  char buffer[buffer_len];
  snprintf(buffer, buffer_len, "mpixels_sec_%s", name);
  RunTest(DrawArraysTestFunc, buffer, coeff * g_width * g_height, true);
}

#ifndef USE_EGL
static void FillRateTestBlendDepth(const char *name) {
  const int buffer_len = 64;
  char buffer[buffer_len];

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_BLEND);
  snprintf(buffer, buffer_len, "mpixels_sec_%s_blended", name);
  RunTest(DrawArraysTestFunc, buffer, g_width * g_height, true);
  glDisable(GL_BLEND);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_NOTEQUAL);
  snprintf(buffer, buffer_len, "mpixels_sec_%s_depth_neq", name);
  RunTest(DrawArraysTestFunc, buffer, g_width * g_height, true);
  glDepthFunc(GL_NEVER);
  snprintf(buffer, buffer_len, "mpixels_sec_%s_depth_never", name);
  RunTest(DrawArraysTestFunc, buffer, g_width * g_height, true);
  glDisable(GL_DEPTH_TEST);
}
#endif


static void DrawArraysTestFunc(int iter) {
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glFlush();
  for (int i = 0; i < iter-1; ++i) {
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
}


static void DrawElementsTestFunc(int iter) {
  glDrawElements(GL_TRIANGLES, arg1, GL_UNSIGNED_INT, 0);
  glFlush();
  for (int i = 0 ; i < iter-1; ++i) {
    glDrawElements(GL_TRIANGLES, arg1, GL_UNSIGNED_INT, 0);
  }
}

// Generates a tautological lattice.
static void CreateLattice(GLfloat **vertices, GLsizeiptr *size,
                          GLfloat size_x, GLfloat size_y,
                          int width, int height)
{
  GLfloat *vptr = *vertices = new GLfloat[2 * (width + 1) * (height + 1)];
  for (int j = 0; j <= height; j++) {
    for (int i = 0; i <= width; i++) {
      *vptr++ = i * size_x;
      *vptr++ = j * size_y;
    }
  }
  *size = (vptr - *vertices) * sizeof(GLfloat);
}

// Generates a mesh of 2*width*height triangles.  The ratio of front facing to
// back facing triangles is culled_ratio/RAND_MAX.  Returns the number of
// vertices in the mesh.
static int CreateMesh(GLuint **indices, GLsizeiptr *size,
                      int width, int height, int culled_ratio) {
  srand(0);

  GLuint *iptr = *indices = new GLuint[2 * 3 * (width * height)];
  const int swath_height = 4;

  CHECK(width % swath_height == 0 && height % swath_height == 0);

  for (int j = 0; j < height; j += swath_height) {
    for (int i = 0; i < width; i++) {
      for (int j2 = 0; j2 < swath_height; j2++) {
        GLuint first = (j + j2) * (width + 1) + i;
        GLuint second = first + 1;
        GLuint third = first + (width + 1);
        GLuint fourth = third + 1;

        bool flag = rand() < culled_ratio;
        *iptr++ = first;
        *iptr++ = flag ? second : third;
        *iptr++ = flag ? third : second;

        *iptr++ = fourth;
        *iptr++ = flag ? third : second;
        *iptr++ = flag ? second : third;
      }
    }
  }
  *size = (iptr - *indices) * sizeof(GLuint);

  return iptr - *indices;
}

#ifndef USE_EGL
void TriangleSetupTest() {
  glViewport(-g_width, -g_height, g_width*2, g_height*2);

  // Larger meshes make this test too slow for devices that do 1 mtri/sec.
  GLint width = 64;
  GLint height = 64;

  GLfloat *vertices = NULL;
  GLsizeiptr vertex_buffer_size = 0;
  CreateLattice(&vertices, &vertex_buffer_size, 1.f / g_width, 1.f / g_height,
                width, height);
  GLuint vertex_buffer = SetupVBO(GL_ARRAY_BUFFER,
                                  vertex_buffer_size, vertices);
  glVertexPointer(2, GL_FLOAT, 0, 0);
  glEnableClientState(GL_VERTEX_ARRAY);

  GLuint *indices = NULL;
  GLuint index_buffer = 0;
  GLsizeiptr index_buffer_size = 0;

  {
    arg1 = CreateMesh(&indices, &index_buffer_size, width, height, 0);

    index_buffer = SetupVBO(GL_ELEMENT_ARRAY_BUFFER,
                            index_buffer_size, indices);
    RunTest(DrawElementsTestFunc, "mtri_sec_triangle_setup", arg1 / 3, true);
    glEnable(GL_CULL_FACE);
    RunTest(DrawElementsTestFunc, "mtri_sec_triangle_setup_all_culled",
            arg1 / 3, true);
    glDisable(GL_CULL_FACE);

    glDeleteBuffers(1, &index_buffer);
    delete[] indices;
  }

  {
    glColor4f(0.f, 1.f, 1.f, 1.f);
    arg1 = CreateMesh(&indices, &index_buffer_size, width, height,
                      RAND_MAX / 2);

    index_buffer = SetupVBO(GL_ELEMENT_ARRAY_BUFFER,
                            index_buffer_size, indices);
    glEnable(GL_CULL_FACE);
    RunTest(DrawElementsTestFunc, "mtri_sec_triangle_setup_half_culled",
            arg1 / 3, true);

    glDeleteBuffers(1, &index_buffer);
    delete[] indices;
  }

  glDeleteBuffers(1, &vertex_buffer);
  delete[] vertices;
}
#endif


void AttributeFetchShaderTest() {
  GLint width = 64;
  GLint height = 64;

  glViewport(-g_width, -g_height, g_width*2, g_height*2);

  GLfloat *vertices = NULL;
  GLsizeiptr vertex_buffer_size = 0;
  CreateLattice(&vertices, &vertex_buffer_size, 1.f / g_width, 1.f / g_height,
                width, height);
  GLuint vertex_buffer = SetupVBO(GL_ARRAY_BUFFER,
                                  vertex_buffer_size, vertices);

  GLuint *indices = NULL;
  GLuint index_buffer = 0;
  GLsizeiptr index_buffer_size = 0;

  // Everything will be back-face culled.
  arg1 = CreateMesh(&indices, &index_buffer_size, width, height, 0);
  index_buffer = SetupVBO(GL_ELEMENT_ARRAY_BUFFER,
                          index_buffer_size, indices);

  glEnable(GL_CULL_FACE);

  GLuint vertex_buffers[8];
  for (GLuint i = 0; i < sizeof(vertex_buffers)/sizeof(vertex_buffers[0]); i++)
    vertex_buffers[i] = vertex_buffer;

  ShaderProgram program = AttributeFetchShaderProgram(1, vertex_buffers);
  RunTest(DrawElementsTestFunc,
          "mvtx_sec_attribute_fetch_shader", arg1, true);
  glDeleteProgram(program);

  program = AttributeFetchShaderProgram(2, vertex_buffers);
  RunTest(DrawElementsTestFunc,
          "mvtx_sec_attribute_fetch_shader_2_attr", arg1, true);
  glDeleteProgram(program);

  program = AttributeFetchShaderProgram(4, vertex_buffers);
  RunTest(DrawElementsTestFunc,
          "mvtx_sec_attribute_fetch_shader_4_attr", arg1, true);
  glDeleteProgram(program);

  program = AttributeFetchShaderProgram(8, vertex_buffers);
  RunTest(DrawElementsTestFunc,
          "mvtx_sec_attribute_fetch_shader_8_attr", arg1, true);
  glDeleteProgram(program);

  glDeleteBuffers(1, &index_buffer);
  delete[] indices;

  glDeleteBuffers(1, &vertex_buffer);
  delete[] vertices;
}


void VaryingsAndDdxyShaderTest() {
  glViewport(-g_width, -g_height, g_width*2, g_height*2);

  const int c = 4;
  GLfloat *vertices = NULL;
  GLsizeiptr vertex_buffer_size = 0;
  CreateLattice(&vertices, &vertex_buffer_size, 1.f / c, 1.f / c, c, c);
  GLuint vertex_buffer = SetupVBO(GL_ARRAY_BUFFER,
                                  vertex_buffer_size, vertices);

  GLuint *indices = NULL;
  GLuint index_buffer = 0;
  GLsizeiptr index_buffer_size = 0;

  arg1 = CreateMesh(&indices, &index_buffer_size, c, c, 0);
  index_buffer = SetupVBO(GL_ELEMENT_ARRAY_BUFFER,
                          index_buffer_size, indices);

  ShaderProgram program = VaryingsShaderProgram(1, vertex_buffer);
  RunTest(DrawElementsTestFunc,
          "mpixels_sec_varyings_shader_1", g_width * g_height, true);
  glDeleteProgram(program);

  program = VaryingsShaderProgram(2, vertex_buffer);
  RunTest(DrawElementsTestFunc,
          "mpixels_sec_varyings_shader_2", g_width * g_height, true);
  glDeleteProgram(program);

  program = VaryingsShaderProgram(4, vertex_buffer);
  RunTest(DrawElementsTestFunc,
          "mpixels_sec_varyings_shader_4", g_width * g_height, true);
  glDeleteProgram(program);

  program = VaryingsShaderProgram(8, vertex_buffer);
  RunTest(DrawElementsTestFunc,
          "mpixels_sec_varyings_shader_8", g_width * g_height, true);
  glDeleteProgram(program);

  program = DdxDdyShaderProgram(true, vertex_buffer);
  RunTest(DrawElementsTestFunc,
          "mpixels_sec_ddx_shader", g_width * g_height, true);
  glDeleteProgram(program);

  program = DdxDdyShaderProgram(false, vertex_buffer);
  RunTest(DrawElementsTestFunc,
          "mpixels_sec_ddy_shader", g_width * g_height, true);
  glDeleteProgram(program);

  glDeleteBuffers(1, &index_buffer);
  delete[] indices;

  glDeleteBuffers(1, &vertex_buffer);
  delete[] vertices;
}


void YuvToRgbShaderTestHelper(int type, const char *name) {
  size_t size = 0;
  GLuint texture[2];
  ShaderProgram program = 0;
  GLuint vertex_buffer = 0;
  GLfloat vertices[8] = {
    0.f, 0.f,
    1.f, 0.f,
    0.f, 1.f,
    1.f, 1.f,
  };
  char evenodd[2] = {0, 255};
  const int pixel_height = YUV2RGB_HEIGHT * 2 / 3;

  char *pixels = static_cast<char *>(MmapFile(YUV2RGB_NAME, &size));
  if (!pixels) {
    printf("# Could not open image file: %s\n", YUV2RGB_NAME);
    goto done;
  }
  if (size != YUV2RGB_SIZE) {
    printf("# Image file of wrong size, got %d, expected %d\n",
           static_cast<int>(size), YUV2RGB_SIZE);
    goto done;
  }

  glGenTextures(2, texture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture[0]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, YUV2RGB_WIDTH, YUV2RGB_HEIGHT,
               0, GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, texture[1]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 2, 1,
               0, GL_LUMINANCE, GL_UNSIGNED_BYTE, evenodd);

  glViewport(-YUV2RGB_WIDTH, -pixel_height, YUV2RGB_WIDTH*2, pixel_height * 2);
  vertex_buffer = SetupVBO(GL_ARRAY_BUFFER, sizeof(vertices), vertices);

  program = YuvToRgbShaderProgram(type, vertex_buffer,
                                  YUV2RGB_WIDTH, pixel_height);

  if (program) {
    float coeff = 1.f *
      (YUV2RGB_WIDTH < g_width ?
       static_cast<float>(YUV2RGB_WIDTH) / g_width : 1.f) *
      (pixel_height < g_height ?
       static_cast<float>(pixel_height) / g_height : 1.f);
    FillRateTestNormal(name, coeff);
  } else {
    printf("# Could not set up YUV shader.\n");
  }

done:
  glDeleteProgram(program);
  glDeleteTextures(2, texture);
  glDeleteBuffers(1, &vertex_buffer);
  munmap(pixels, size);
}

void YuvToRgbShaderTest1() {
  YuvToRgbShaderTestHelper(1, "yuv_shader_1");
}

void YuvToRgbShaderTest2() {
  YuvToRgbShaderTestHelper(2, "yuv_shader_2");
}

static uint32_t texture_base[WINDOW_HEIGHT*WINDOW_WIDTH];
static uint32_t texture_update[WINDOW_HEIGHT*WINDOW_WIDTH];
#ifndef USE_EGL
static GLuint compositing_textures[5];
static ShaderProgram compositing_background_program = 0;
static ShaderProgram compositing_foreground_program = 0;
#endif

void InitBaseTexture() {
  for (int y = 0; y < WINDOW_HEIGHT; y++) {
    for (int x = 0; x < WINDOW_WIDTH; x++) {
      // This color is gray, half alpha.
      texture_base[y*WINDOW_WIDTH+x] = 0x80808080;
    }
  }
}

// UpdateTexture simulates Chrome updating tab contents.
// We cause a bunch of read and write cpu memory bandwidth.
// It's a very rough approximation.
void UpdateTexture() {
  memcpy(texture_update, texture_base, sizeof(texture_base));
}

void LoadTexture() {
  // Use GL_RGBA for compatibility with GLES2.0.
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
               WINDOW_WIDTH, WINDOW_HEIGHT, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, texture_update);
}

#ifndef USE_EGL
// Test how fast we can do full-screen compositing of images
// continuously updated from the CPU.
// This tests both GPU compositing performance and also
// CPU -> GPU data transfer speed.
// It is a basic perf test to make sure we have enough performance
// to run a compositing window manager.
void CompositingTestFunc(int iter) {
  for (int i = 0 ; i < iter; ++i) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw the background
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    // We have to blend three textures, but we use multi-texture for this
    // blending, not fb blend, to avoid the external memory traffic
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, compositing_textures[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, compositing_textures[1]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, compositing_textures[2]);
    // Set up the texture coordinate arrays
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE2);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    // Use the right shader
    glUseProgram(compositing_background_program);
    // Draw the quad
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Set up one texture coordinate array
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE2);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    // Use the right shader
    glUseProgram(compositing_foreground_program);

    // Compositing is blending, so we shall blend.
    glEnable(GL_BLEND);
    // Depth test is on for window occlusion
    glEnable(GL_DEPTH_TEST);

    // Draw window number one
    // This update acts like a chrome webkit sw rendering update.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, compositing_textures[3]);
    UpdateTexture();
    // TODO(papakipos): this LoadTexture is likely doing more CPU memory copies
    // than we would like.
    LoadTexture();
    // TODO(papakipos): add color interpolation here, and modulate
    // texture against it.
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Draw window number two
    // This is a static window, so we don't update it.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, compositing_textures[4]);
    // TODO(papakipos): add color interpolation here, and modulate
    // texture against it.
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
}

void InitializeCompositing() {
  InitBaseTexture();

  glClearColor(0.f, 0.f, 0.f, 0.f);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthFunc(GL_LEQUAL);

  glGenTextures(5, compositing_textures);
  glActiveTexture(GL_TEXTURE0);
  for (int i = 0; i < 5; i++) {
    glBindTexture(GL_TEXTURE_2D, compositing_textures[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    GL_LINEAR);
  }

  glColor4f(1.f, 1.f, 1.f, 1.f);

  // Set up the vertex arrays for drawing textured quads later on.
  glEnableClientState(GL_VERTEX_ARRAY);
  GLfloat buffer_vertex[8] = {
    -1.f, -1.f,
    1.f,  -1.f,
    -1.f, 1.f,
    1.f,  1.f,
  };
  GLuint vbo_vertex = SetupVBO(GL_ARRAY_BUFFER,
                               sizeof(buffer_vertex), buffer_vertex);
  glVertexPointer(2, GL_FLOAT, 0, 0);

  GLfloat buffer_texture[8] = {
    0.f, 0.f,
    1.f, 0.f,
    0.f, 1.f,
    1.f, 1.f,
  };
  GLuint vbo_texture = SetupVBO(GL_ARRAY_BUFFER,
                                sizeof(buffer_texture), buffer_texture);
  for (int i = 0; i < 3; i++) {
    glClientActiveTexture(GL_TEXTURE0 + i);
    glTexCoordPointer(2, GL_FLOAT, 0, 0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  }

  // Set up the static background textures.
  UpdateTexture();
  UpdateTexture();
  UpdateTexture();
  // Load these textures into bound texture ids and keep using them
  // from there to avoid having to reload this texture every frame
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, compositing_textures[0]);
  LoadTexture();
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, compositing_textures[1]);
  LoadTexture();
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, compositing_textures[2]);
  LoadTexture();

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, compositing_textures[3]);
  UpdateTexture();
  LoadTexture();

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, compositing_textures[4]);
  UpdateTexture();
  LoadTexture();

  // Set up vertex & fragment shaders.
  compositing_background_program =
      TripleTextureBlendShaderProgram(vbo_vertex,
                                      vbo_texture, vbo_texture, vbo_texture);
  compositing_foreground_program =
      BasicTextureShaderProgram(vbo_vertex, vbo_texture);
  if ((!compositing_background_program) ||
      (!compositing_foreground_program)) {
    printf("# Could not set up compositing shader.\n");
  }

  glVertexPointer(2, GL_FLOAT, 0, 0);
}

void TeardownCompositing() {
  glDeleteProgram(compositing_background_program);
  glDeleteProgram(compositing_foreground_program);
}

// Notes on the window manager compositing test:
// Depth
//      Depth complexity = 3: background, active window, static window
//      Background: may be a tex-blend of three images (2.5d effect)
// The windows -- at most two, fullscreen
//      Depth test is on, passing most of the time.
//      A lot of texture min-filtering -- not modelled
//      One of the two windows is getting live browser frame updates -- not mod
//          The live window runs at x/2 and y/2 size -- not modelled
//      The two windows are modulated by color interpolation to get gradient
static float screen_scale_factor = (1e6f*
                                    (WINDOW_WIDTH*WINDOW_HEIGHT)/
                                    (1280.f*768));

void WindowManagerCompositingTest() {
  InitializeCompositing();
  RunTest(CompositingTestFunc, "1280x768_fps_compositing",
          screen_scale_factor, true);
  TeardownCompositing();
}

void NoFillWindowManagerCompositingTest() {
  glScissor(0, 0, 1, 1);
  glEnable(GL_SCISSOR_TEST);
  InitializeCompositing();
  RunTest(CompositingTestFunc, "1280x768_fps_no_fill_compositing",
          screen_scale_factor, true);
  TeardownCompositing();
}
#endif

void ReadPixelTestFunc(int iter) {
  glReadPixels(0, 0, g_width, g_height, GL_RGBA, GL_UNSIGNED_BYTE, arg2);
  CHECK(glGetError() == 0);
  for (int i = 0; i < iter; i++)
    glReadPixels(0, 0, g_width, g_height, GL_RGBA, GL_UNSIGNED_BYTE, arg2);
}

// TODO: better test names.
void ReadPixelTest() {
  // One GL_RGBA pixel takes 4 bytes.
  const int row_size = g_width * 4;
  // Default GL_PACK_ALIGNMENT is 4, round up pixel row size to multiple of 4.
  // This is a no-op because row_size is already divisible by 4.
  // One is added so that we can test reads into unaligned location.
  char* pixels = new char[((row_size + 3) & ~3) * g_height + 1];

  arg2 = pixels;
  RunTest(ReadPixelTestFunc, "mpixels_sec_pixel_read",
          g_width * g_height, true);

  // Reducing GL_PACK_ALIGNMENT can only make rows smaller.  No need to
  // reallocate the buffer.
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  RunTest(ReadPixelTestFunc, "mpixels_sec_pixel_read_2",
          g_width * g_height, true);

  arg2 = static_cast<void*>(pixels + 1);
  RunTest(ReadPixelTestFunc, "mpixels_sec_pixel_read_3",
          g_width * g_height, true);

  delete[] pixels;
}

// TODO: use proper command line parsing library.
static void ParseArgs(int argc, char *argv[]) {
  const char **enabled_tests_ptr = enabled_tests;
  bool test_name_arg = false;
  bool duration_arg = false;
  for (int i = 0; i < argc; i++) {
    if (test_name_arg) {
      test_name_arg = false;
      *enabled_tests_ptr++ = argv[i];
      if (enabled_tests_ptr - enabled_tests >= enabled_tests_max)
        break;
    } else if (duration_arg) {
      duration_arg = false;
      seconds_to_run = atoi(argv[i]);
    } else if (strcmp("-t", argv[i]) == 0) {
      test_name_arg = true;
    } else if (strcmp("-d", argv[i]) == 0) {
      duration_arg = true;
    }
  }
  *enabled_tests_ptr++ = NULL;
}


int main(int argc, char *argv[]) {
  SetBasePathFromArgv0(argv[0], "src");
  ParseArgs(argc, argv);
  if (!Init()) {
    printf("# Failed to initialize.\n");
    return 1;
  }

  void (*test[])() = {
    SwapTest,
    ClearTest,
    AttributeFetchShaderTest,
    VaryingsAndDdxyShaderTest,
    YuvToRgbShaderTest1,
    YuvToRgbShaderTest2,
#ifndef USE_EGL
    FillRateTest,
    TriangleSetupTest,
    NoFillWindowManagerCompositingTest,
    WindowManagerCompositingTest,
#endif
    ReadPixelTest,
  };

  uint64_t done = GetUTime() + 1000000ULL * seconds_to_run;
  do {
    for (unsigned int i = 0; i < sizeof(test) / sizeof(*test); i++) {
      InitContext();
      test[i]();
      GLenum err = glGetError();
      if (err != 0)
        printf("# glGetError returned non-zero: 0x%x\n", err);
      DestroyContext();
    }
  } while (GetUTime() < done);

  return 0;
}

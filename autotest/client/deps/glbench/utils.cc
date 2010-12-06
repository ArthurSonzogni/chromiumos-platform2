// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "base/logging.h"
#include "main.h"
#include "utils.h"

const char* kGlesHeader =
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "#endif\n";

FilePath *g_base_path = new FilePath();

// Sets the base path for MmapFile to `dirname($argv0)`/$relative.
void SetBasePathFromArgv0(const char* argv0, const char* relative) {
  if (g_base_path) {
    delete g_base_path;
  }
  FilePath argv0_path = FilePath(argv0).DirName();
  FilePath base_path = relative ? argv0_path.Append(relative) : argv0_path;
  g_base_path = new FilePath(base_path);
}

const FilePath& GetBasePath() {
  return *g_base_path;
}

void *MmapFile(const char* name, size_t* length) {
  FilePath filename = g_base_path->Append(name);
  int fd = open(filename.value().c_str(), O_RDONLY);
  if (fd == -1)
    return NULL;

  struct stat sb;
  CHECK(fstat(fd, &sb) != -1);

  char *mmap_ptr = static_cast<char *>(
    mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));

  close(fd);

  if (mmap_ptr)
    *length = sb.st_size;

  return mmap_ptr;
}


namespace glbench {

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

GLuint SetupVBO(GLenum target, GLsizeiptr size, const GLvoid *data) {
  GLuint buf = ~0;
  glGenBuffers(1, &buf);
  glBindBuffer(target, buf);
  glBufferData(target, size, data, GL_STATIC_DRAW);
  CHECK(!glGetError());
  return buf;
}

// Generates a tautological lattice.
void CreateLattice(GLfloat **vertices, GLsizeiptr *size,
                   GLfloat size_x, GLfloat size_y, int width, int height)
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
int CreateMesh(GLuint **indices, GLsizeiptr *size,
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

static void print_info_log(int obj)
{
  char info_log[4096];
  int length;
  glGetError();
  glGetShaderInfoLog(obj, sizeof(info_log)-1, &length, info_log);
  if (glGetError() != 0)
    glGetProgramInfoLog(obj, sizeof(info_log)-1, &length, info_log);
  char *p = info_log;
  while (p < info_log + length) {
    char *newline = strchr(p, '\n');
    if (newline)
      *newline = '\0';
    printf("# Log: %s\n", p);
    if (!newline)
      break;
    p = newline + 1;
  }
}

GLuint InitShaderProgram(const char *vertex_src, const char *fragment_src) {
  return InitShaderProgramWithHeader(kGlesHeader, vertex_src, fragment_src);
}

GLuint InitShaderProgramWithHeader(const char* header,
                                   const char* vertex_src,
                                   const char* fragment_src) {
  GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

  const char* header_and_body[2];
  header_and_body[0] = header ? header : "";
  header_and_body[1] = vertex_src;
  glShaderSource(vertex_shader,
                 arraysize(header_and_body), header_and_body, NULL);
  header_and_body[1] = fragment_src;
  glShaderSource(fragment_shader,
                 arraysize(header_and_body), header_and_body, NULL);

  glCompileShader(vertex_shader);
  print_info_log(vertex_shader);
  glCompileShader(fragment_shader);
  print_info_log(fragment_shader);

  GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  print_info_log(program);
  glUseProgram(program);

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  return program;
}

} // namespace glbench

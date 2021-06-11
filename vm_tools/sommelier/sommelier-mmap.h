// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_MMAP_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_MMAP_H_

#include <sys/types.h>

typedef void (*sl_begin_end_access_func_t)(int fd, struct sl_context* ctx);

struct sl_mmap {
  int refcount;
  int fd;
  void* addr;
  size_t size;
  size_t bpp;
  size_t num_planes;
  size_t offset[2];
  size_t stride[2];
  size_t y_ss[2];
  sl_begin_end_access_func_t begin_write;
  sl_begin_end_access_func_t end_write;
  struct wl_resource* buffer_resource;
};

struct sl_mmap* sl_mmap_create(int fd,
                               size_t size,
                               size_t bpp,
                               size_t num_planes,
                               size_t offset0,
                               size_t stride0,
                               size_t offset1,
                               size_t stride1,
                               size_t y_ss0,
                               size_t y_ss1);
struct sl_mmap* sl_mmap_ref(struct sl_mmap* map);
void sl_mmap_unref(struct sl_mmap* map);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_MMAP_H_

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier-mmap.h"  // NOLINT(build/include_directory)

#include <assert.h>
#include <unistd.h>

#include "sommelier.h"          // NOLINT(build/include_directory)
#include "sommelier-tracing.h"  // NOLINT(build/include_directory)

struct sl_mmap* sl_mmap_create(int fd,
                               size_t size,
                               size_t bpp,
                               size_t num_planes,
                               size_t offset0,
                               size_t stride0,
                               size_t offset1,
                               size_t stride1,
                               size_t y_ss0,
                               size_t y_ss1) {
  TRACE_EVENT("shm", "sl_mmap_create");
  struct sl_mmap* map = static_cast<sl_mmap*>(malloc(sizeof(*map)));
  assert(map);
  map->refcount = 1;
  map->fd = fd;
  map->size = size;
  map->num_planes = num_planes;
  map->bpp = bpp;
  map->offset[0] = offset0;
  map->stride[0] = stride0;
  map->offset[1] = offset1;
  map->stride[1] = stride1;
  map->y_ss[0] = y_ss0;
  map->y_ss[1] = y_ss1;
  map->begin_write = NULL;
  map->end_write = NULL;
  map->buffer_resource = NULL;
  map->addr =
      mmap(NULL, size + offset0, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  errno_assert(map->addr != MAP_FAILED);

  return map;
}

struct sl_mmap* sl_mmap_ref(struct sl_mmap* map) {
  TRACE_EVENT("shm", "sl_mmap_ref");
  map->refcount++;
  return map;
}

void sl_mmap_unref(struct sl_mmap* map) {
  TRACE_EVENT("shm", "sl_mmap_unref");
  if (map->refcount-- == 1) {
    munmap(map->addr, map->size + map->offset[0]);
    if (map->fd != -1)
      close(map->fd);
    free(map);
  }
}

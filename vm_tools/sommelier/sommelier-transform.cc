// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>

#include "sommelier-tracing.h"    // NOLINT(build/include_directory)
#include "sommelier-transform.h"  // NOLINT(build/include_directory)

bool sl_transform_viewport_scale(struct sl_context* ctx,
                                 double contents_scale,
                                 int32_t* width,
                                 int32_t* height) {
  double scale = ctx->scale * contents_scale;
  bool do_viewport = true;

  *width = ceil(*width / scale);
  *height = ceil(*height / scale);
  return do_viewport;
}

void sl_transform_damage_coord(struct sl_context* ctx,
                               double scalex,
                               double scaley,
                               int64_t* x1,
                               int64_t* y1,
                               int64_t* x2,
                               int64_t* y2) {
  double sx = scalex * ctx->scale;
  double sy = scaley * ctx->scale;

  // Enclosing rect after scaling and outset by one pixel to account for
  // potential filtering.
  *x1 = MAX(MIN_SIZE, (*x1) - 1) / sx;
  *y1 = MAX(MIN_SIZE, (*y1) - 1) / sy;
  *x2 = ceil(MIN((*x2) + 1, MAX_SIZE) / sx);
  *y2 = ceil(MIN((*y2) + 1, MAX_SIZE) / sy);
}

void sl_transform_host_to_guest(struct sl_context* ctx,
                                int32_t* x,
                                int32_t* y) {
  (*x) *= ctx->scale;
  (*y) *= ctx->scale;
}

void sl_transform_host_to_guest_fixed(struct sl_context* ctx,
                                      wl_fixed_t* x,
                                      wl_fixed_t* y) {
  double dx = wl_fixed_to_double(*x);
  double dy = wl_fixed_to_double(*y);

  dx *= ctx->scale;
  dy *= ctx->scale;

  *x = wl_fixed_from_double(dx);
  *y = wl_fixed_from_double(dy);
}

void sl_transform_host_to_guest_fixed(struct sl_context* ctx,
                                      wl_fixed_t* coord,
                                      uint32_t axis) {
  double dx = wl_fixed_to_double(*coord);

  dx *= ctx->scale;
  *coord = wl_fixed_from_double(dx);
}

void sl_transform_guest_to_host(struct sl_context* ctx,
                                int32_t* x,
                                int32_t* y) {
  (*x) /= ctx->scale;
  (*y) /= ctx->scale;
}

void sl_transform_guest_to_host_fixed(struct sl_context* ctx,
                                      wl_fixed_t* x,
                                      wl_fixed_t* y) {
  double dx = wl_fixed_to_double(*x);
  double dy = wl_fixed_to_double(*y);

  dx /= ctx->scale;
  dy /= ctx->scale;

  *x = wl_fixed_from_double(dx);
  *y = wl_fixed_from_double(dy);
}

void sl_transform_guest_to_host_fixed(struct sl_context* ctx,
                                      wl_fixed_t* coord,
                                      uint32_t axis) {
  double dx = wl_fixed_to_double(*coord);

  dx /= ctx->scale;
  *coord = wl_fixed_from_double(dx);
}

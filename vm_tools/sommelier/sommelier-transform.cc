// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>

#include "sommelier-tracing.h"    // NOLINT(build/include_directory)
#include "sommelier-transform.h"  // NOLINT(build/include_directory)

static void sl_transform_get_scale_factors(
    struct sl_context* ctx,
    const struct sl_host_surface* surface,
    double* scalex,
    double* scaley) {
  if (ctx->use_direct_scale && surface && surface->has_own_scale) {
    *scalex = surface->xdg_scale_x;
    *scaley = surface->xdg_scale_y;
  } else {
    *scalex = ctx->xdg_scale_x;
    *scaley = ctx->xdg_scale_y;
  }
}

static double sl_transform_direct_axis_scale(struct sl_context* ctx,
                                             struct sl_host_surface* surface,
                                             uint32_t axis) {
  double scalex, scaley;

  sl_transform_get_scale_factors(ctx, surface, &scalex, &scaley);
  return (axis == 0) ? scaley : scalex;
}

static void sl_transform_direct_to_host_damage(
    struct sl_context* ctx,
    const struct sl_host_surface* surface,
    int64_t* x,
    int64_t* y,
    double scale_x,
    double scale_y) {
  double xwhole = trunc(static_cast<double>(*x) / scale_x);
  double ywhole = trunc(static_cast<double>(*y) / scale_y);

  *x = static_cast<int64_t>(xwhole);
  *y = static_cast<int64_t>(ywhole);
}

static void sl_transform_direct_to_guest_fixed(struct sl_context* ctx,
                                               struct sl_host_surface* surface,
                                               wl_fixed_t* coord,
                                               uint32_t axis) {
  double scale = sl_transform_direct_axis_scale(ctx, surface, axis);
  double result = wl_fixed_to_double(*coord) * scale;

  *coord = wl_fixed_from_double(result);
}

static void sl_transform_direct_to_guest_fixed(struct sl_context* ctx,
                                               struct sl_host_surface* surface,
                                               wl_fixed_t* x,
                                               wl_fixed_t* y) {
  double scalex, scaley;

  sl_transform_get_scale_factors(ctx, surface, &scalex, &scaley);

  double resultx = wl_fixed_to_double(*x) * scalex;
  double resulty = wl_fixed_to_double(*y) * scaley;

  *x = wl_fixed_from_double(resultx);
  *y = wl_fixed_from_double(resulty);
}

static void sl_transform_direct_to_host_fixed(struct sl_context* ctx,
                                              struct sl_host_surface* surface,
                                              wl_fixed_t* coord,
                                              uint32_t axis) {
  double scale = sl_transform_direct_axis_scale(ctx, surface, axis);
  double result = wl_fixed_to_double(*coord) / scale;

  *coord = wl_fixed_from_double(result);
}

static void sl_transform_direct_to_host_fixed(struct sl_context* ctx,
                                              struct sl_host_surface* surface,
                                              wl_fixed_t* x,
                                              wl_fixed_t* y) {
  double scalex, scaley;

  sl_transform_get_scale_factors(ctx, surface, &scalex, &scaley);

  double resultx = wl_fixed_to_double(*x) / scalex;
  double resulty = wl_fixed_to_double(*y) / scaley;

  *x = wl_fixed_from_double(resultx);
  *y = wl_fixed_from_double(resulty);
}

static void sl_transform_direct_to_guest(struct sl_context* ctx,
                                         struct sl_host_surface* surface,
                                         int32_t* x,
                                         int32_t* y) {
  double scalex, scaley;

  sl_transform_get_scale_factors(ctx, surface, &scalex, &scaley);

  double xwhole = trunc(scalex * static_cast<double>(*x));
  double ywhole = trunc(scaley * static_cast<double>(*y));

  *x = static_cast<int32_t>(xwhole);
  *y = static_cast<int32_t>(ywhole);
}

static void sl_transform_direct_to_host(struct sl_context* ctx,
                                        struct sl_host_surface* surface,
                                        int32_t* x,
                                        int32_t* y) {
  double scalex, scaley;

  sl_transform_get_scale_factors(ctx, surface, &scalex, &scaley);

  double xwhole = trunc(static_cast<double>(*x) / scalex);
  double ywhole = trunc(static_cast<double>(*y) / scaley);

  *x = static_cast<int32_t>(xwhole);
  *y = static_cast<int32_t>(ywhole);
}

bool sl_transform_viewport_scale(struct sl_context* ctx,
                                 struct sl_host_surface* surface,
                                 double contents_scale,
                                 int32_t* width,
                                 int32_t* height) {
  double scale = ctx->scale * contents_scale;

  // TODO(mrisaacb): It may be beneficial to skip the set_destination call
  // when the virtual and logical space match.
  bool do_viewport = true;

  if (ctx->use_direct_scale) {
    sl_transform_direct_to_host(ctx, surface, width, height);
  } else {
    *width = ceil(*width / scale);
    *height = ceil(*height / scale);
  }

  return do_viewport;
}

void sl_transform_damage_coord(struct sl_context* ctx,
                               const struct sl_host_surface* surface,
                               double damage_scalex,
                               double damage_scaley,
                               int64_t* x1,
                               int64_t* y1,
                               int64_t* x2,
                               int64_t* y2) {
  if (ctx->use_direct_scale) {
    double scalex, scaley;

    sl_transform_get_scale_factors(ctx, surface, &scalex, &scaley);

    scalex *= damage_scalex;
    scaley *= damage_scaley;

    sl_transform_direct_to_host_damage(ctx, surface, x1, y1, scalex, scaley);
    sl_transform_direct_to_host_damage(ctx, surface, x2, y2, scalex, scaley);
  } else {
    double sx = damage_scalex * ctx->scale;
    double sy = damage_scaley * ctx->scale;

    // Enclosing rect after scaling and outset by one pixel to account for
    // potential filtering.
    *x1 = MAX(MIN_SIZE, (*x1) - 1) / sx;
    *y1 = MAX(MIN_SIZE, (*y1) - 1) / sy;
    *x2 = ceil(MIN((*x2) + 1, MAX_SIZE) / sx);
    *y2 = ceil(MIN((*y2) + 1, MAX_SIZE) / sy);
  }
}

void sl_transform_host_to_guest(struct sl_context* ctx,
                                struct sl_host_surface* surface,
                                int32_t* x,
                                int32_t* y) {
  if (ctx->use_direct_scale) {
    sl_transform_direct_to_guest(ctx, surface, x, y);
  } else {
    (*x) *= ctx->scale;
    (*y) *= ctx->scale;
  }
}

void sl_transform_host_to_guest_fixed(struct sl_context* ctx,
                                      struct sl_host_surface* surface,
                                      wl_fixed_t* x,
                                      wl_fixed_t* y) {
  if (ctx->use_direct_scale) {
    sl_transform_direct_to_guest_fixed(ctx, surface, x, y);
  } else {
    double dx = wl_fixed_to_double(*x);
    double dy = wl_fixed_to_double(*y);

    dx *= ctx->scale;
    dy *= ctx->scale;

    *x = wl_fixed_from_double(dx);
    *y = wl_fixed_from_double(dy);
  }
}

void sl_transform_host_to_guest_fixed(struct sl_context* ctx,
                                      struct sl_host_surface* surface,
                                      wl_fixed_t* coord,
                                      uint32_t axis) {
  if (ctx->use_direct_scale) {
    sl_transform_direct_to_guest_fixed(ctx, surface, coord, axis);
  } else {
    double dx = wl_fixed_to_double(*coord);

    dx *= ctx->scale;
    *coord = wl_fixed_from_double(dx);
  }
}

void sl_transform_guest_to_host(struct sl_context* ctx,
                                struct sl_host_surface* surface,
                                int32_t* x,
                                int32_t* y) {
  if (ctx->use_direct_scale) {
    sl_transform_direct_to_host(ctx, surface, x, y);
  } else {
    (*x) /= ctx->scale;
    (*y) /= ctx->scale;
  }
}

void sl_transform_guest_to_host_fixed(struct sl_context* ctx,
                                      struct sl_host_surface* surface,
                                      wl_fixed_t* x,
                                      wl_fixed_t* y) {
  if (ctx->use_direct_scale) {
    sl_transform_direct_to_host_fixed(ctx, surface, x, y);
  } else {
    double dx = wl_fixed_to_double(*x);
    double dy = wl_fixed_to_double(*y);

    dx /= ctx->scale;
    dy /= ctx->scale;

    *x = wl_fixed_from_double(dx);
    *y = wl_fixed_from_double(dy);
  }
}

void sl_transform_guest_to_host_fixed(struct sl_context* ctx,
                                      struct sl_host_surface* surface,
                                      wl_fixed_t* coord,
                                      uint32_t axis) {
  if (ctx->use_direct_scale) {
    sl_transform_direct_to_host_fixed(ctx, surface, coord, axis);
  } else {
    double dx = wl_fixed_to_double(*coord);

    dx /= ctx->scale;
    *coord = wl_fixed_from_double(dx);
  }
}

void sl_transform_output_dimensions(struct sl_context* ctx,
                                    int32_t* width,
                                    int32_t* height) {
  *width = (*width) * ctx->scale;
  *height = (*height) * ctx->scale;
}

// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_TRANSFORM_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_TRANSFORM_H_

#include "sommelier-ctx.h"  // NOLINT(build/include_directory)

// Coordinate transform functions
//
// In general, the transformation functions fall under one of these
// two classes:
//
// 1. Transformations which follow the basic rules:
//    A straight multiply for host->guest and straight divide for the opposite
// 2. Transformations which perform their transformations in a slightly
//    different manner.
//
// The functions immediately following this block fall under the latter
// They are separate functions so these cases can be easily identified
// throughout the rest of the code.
//
// The functions that fall under the latter case work in the
// guest->host direction and do not have variants which work in the
// opposite direction.

// This particular function will return true if setting a destination
// viewport size is necessary. It can be false if the host/guest spaces
// matches.
// This is a potential optimization as it removes one step
// from the guest->host surface_attach cycle.
bool sl_transform_viewport_scale(struct sl_context* ctx,
                                 double contents_scale,
                                 int32_t* width,
                                 int32_t* height);

void sl_transform_damage_coord(struct sl_context* ctx,
                               double scalex,
                               double scaley,
                               int64_t* x1,
                               int64_t* y1,
                               int64_t* x2,
                               int64_t* y2);

// Basic Transformations
// The following transformations fall under the basic type

// 1D transformation functions have an axis specifier
// to indicate along which axis the transformation is to
// take place.
//
// The axis specifier will follow the wl_pointer::axis definitions
// 0 = vertical axis (Y)
// 1 = horizontal axis (X)

void sl_transform_host_to_guest(struct sl_context* ctx, int32_t* x, int32_t* y);

void sl_transform_host_to_guest_fixed(struct sl_context* ctx,
                                      wl_fixed_t* x,
                                      wl_fixed_t* y);

void sl_transform_host_to_guest_fixed(struct sl_context* ctx,
                                      wl_fixed_t* coord,
                                      uint32_t axis);

// Opposite Direction
void sl_transform_guest_to_host(struct sl_context* ctx, int32_t* x, int32_t* y);

void sl_transform_guest_to_host_fixed(struct sl_context* ctx,
                                      wl_fixed_t* x,
                                      wl_fixed_t* y);

void sl_transform_guest_to_host_fixed(struct sl_context* ctx,
                                      wl_fixed_t* coord,
                                      uint32_t axis);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_TRANSFORM_H_

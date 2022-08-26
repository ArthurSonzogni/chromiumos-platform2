// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <pixman.h>

#include "sommelier.h"          // NOLINT(build/include_directory)
#include "sommelier-tracing.h"  // NOLINT(build/include_directory)
#include "sommelier-xshape.h"   // NOLINT(build/include_directory)

static void sl_clear_shape_region(sl_window* window) {
  window->shaped = false;
  pixman_region32_fini(&window->shape_rectangles);
}

static void sl_attach_shape_region(struct sl_context* ctx,
                                   xcb_window_t window) {
  sl_window* sl_window = nullptr;
  xcb_shape_get_rectangles_reply_t* reply;
  int i;

  sl_window = sl_lookup_window(ctx, window);
  if (!sl_window)
    return;

  reply = xcb_shape_get_rectangles_reply(
      ctx->connection,
      xcb_shape_get_rectangles(ctx->connection, window, XCB_SHAPE_SK_BOUNDING),
      NULL);

  if (!reply)
    return;

  int nrects = xcb_shape_get_rectangles_rectangles_length(reply);
  xcb_rectangle_t* rects = xcb_shape_get_rectangles_rectangles(reply);

  if (!rects || nrects <= 0)
    return;

  pixman_box32_t* boxes =
      static_cast<pixman_box32_t*>(calloc(sizeof(pixman_box32_t), nrects));

  if (!boxes) {
    free(reply);
    return;
  }

  for (i = 0; i < nrects; i++) {
    boxes[i].x1 = rects[i].x;
    boxes[i].y1 = rects[i].y;

    boxes[i].x2 = rects[i].x + rects[i].width;
    boxes[i].y2 = rects[i].y + rects[i].height;
  }

  pixman_region32_init_rects(&sl_window->shape_rectangles, boxes, nrects);
  free(boxes);
  free(reply);

  sl_window->shaped = true;
}

void sl_handle_shape_notify(struct sl_context* ctx,
                            struct xcb_shape_notify_event_t* event) {
  sl_window* window = nullptr;

  window = sl_lookup_window(ctx, event->affected_window);

  if (!window)
    return;

  sl_clear_shape_region(window);

  if (event->shaped)
    sl_attach_shape_region(ctx, event->affected_window);

  return;
}

void sl_shape_query(struct sl_context* ctx, xcb_window_t xwindow) {
  xcb_shape_query_extents_reply_t* reply;
  sl_window* sl_window = nullptr;

  sl_window = sl_lookup_window(ctx, xwindow);
  if (!sl_window)
    return;

  reply = xcb_shape_query_extents_reply(
      ctx->connection, xcb_shape_query_extents(ctx->connection, xwindow), NULL);

  if (!reply)
    return;

  sl_clear_shape_region(sl_window);

  if (reply->bounding_shaped) {
    sl_attach_shape_region(ctx, xwindow);
  }
}

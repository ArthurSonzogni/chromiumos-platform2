// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_WINDOW_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_WINDOW_H_

#include <wayland-server-core.h>
#include <xcb/xcb.h>

#define US_POSITION (1L << 0)
#define US_SIZE (1L << 1)
#define P_POSITION (1L << 2)
#define P_SIZE (1L << 3)
#define P_MIN_SIZE (1L << 4)
#define P_MAX_SIZE (1L << 5)
#define P_RESIZE_INC (1L << 6)
#define P_ASPECT (1L << 7)
#define P_BASE_SIZE (1L << 8)
#define P_WIN_GRAVITY (1L << 9)

struct sl_config {
  uint32_t serial;
  uint32_t mask;
  uint32_t values[5];
  uint32_t states_length;
  uint32_t states[3];
};

struct sl_window {
  struct sl_context* ctx;
  xcb_window_t id;
  xcb_window_t frame_id;
  uint32_t host_surface_id;
  int unpaired;
  int x;
  int y;
  int width;
  int height;
  int border_width;
  int depth;
  int managed;
  int realized;
  int activated;
  int maximized;
  int allow_resize;
  xcb_window_t transient_for;
  xcb_window_t client_leader;
  int decorated;
  char* name;
  char* clazz;
  char* startup_id;
  int dark_frame;
  uint32_t size_flags;
  int focus_model_take_focus;
  int min_width;
  int min_height;
  int max_width;
  int max_height;
  struct sl_config next_config;
  struct sl_config pending_config;
  struct zxdg_surface_v6* xdg_surface;
  struct zxdg_toplevel_v6* xdg_toplevel;
  struct zxdg_popup_v6* xdg_popup;
  struct zaura_surface* aura_surface;
  struct wl_list link;
};

enum {
  PROPERTY_WM_NAME,
  PROPERTY_WM_CLASS,
  PROPERTY_WM_TRANSIENT_FOR,
  PROPERTY_WM_NORMAL_HINTS,
  PROPERTY_WM_CLIENT_LEADER,
  PROPERTY_WM_PROTOCOLS,
  PROPERTY_MOTIF_WM_HINTS,
  PROPERTY_NET_STARTUP_ID,
  PROPERTY_NET_WM_STATE,
  PROPERTY_GTK_THEME_VARIANT,
  PROPERTY_XWAYLAND_RANDR_EMU_MONITOR_RECTS,
};

struct sl_wm_size_hints {
  uint32_t flags;
  int32_t x, y;
  int32_t width, height;
  int32_t min_width, min_height;
  int32_t max_width, max_height;
  int32_t width_inc, height_inc;
  struct {
    int32_t x;
    int32_t y;
  } min_aspect, max_aspect;
  int32_t base_width, base_height;
  int32_t win_gravity;
};

// WM_HINTS is defined at: https://tronche.com/gui/x/icccm/sec-4.html

#define WM_HINTS_FLAG_INPUT (1L << 0)
#define WM_HINTS_FLAG_STATE (1L << 1)
#define WM_HINTS_FLAG_ICON_PIXMAP (1L << 2)
#define WM_HINTS_FLAG_ICON_WINDOW (1L << 3)
#define WM_HINTS_FLAG_ICON_POSITION (1L << 4)
#define WM_HINTS_FLAG_ICON_MASK (1L << 5)
#define WM_HINTS_FLAG_WINDOW_GROUP (1L << 6)
#define WM_HINTS_FLAG_MESSAGE (1L << 7)
#define WM_HINTS_FLAG_URGENCY (1L << 8)

struct sl_wm_hints {
  uint32_t flags;
  uint32_t input;
  uint32_t initiali_state;
  xcb_pixmap_t icon_pixmap;
  xcb_window_t icon_window;
  int32_t icon_x;
  int32_t icon_y;
  xcb_pixmap_t icon_mask;
};

#define MWM_HINTS_FUNCTIONS (1L << 0)
#define MWM_HINTS_DECORATIONS (1L << 1)
#define MWM_HINTS_INPUT_MODE (1L << 2)
#define MWM_HINTS_STATUS (1L << 3)

#define MWM_DECOR_ALL (1L << 0)
#define MWM_DECOR_BORDER (1L << 1)
#define MWM_DECOR_RESIZEH (1L << 2)
#define MWM_DECOR_TITLE (1L << 3)
#define MWM_DECOR_MENU (1L << 4)
#define MWM_DECOR_MINIMIZE (1L << 5)
#define MWM_DECOR_MAXIMIZE (1L << 6)

struct sl_mwm_hints {
  uint32_t flags;
  uint32_t functions;
  uint32_t decorations;
  int32_t input_mode;
  uint32_t status;
};

#define NET_WM_MOVERESIZE_SIZE_TOPLEFT 0
#define NET_WM_MOVERESIZE_SIZE_TOP 1
#define NET_WM_MOVERESIZE_SIZE_TOPRIGHT 2
#define NET_WM_MOVERESIZE_SIZE_RIGHT 3
#define NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT 4
#define NET_WM_MOVERESIZE_SIZE_BOTTOM 5
#define NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT 6
#define NET_WM_MOVERESIZE_SIZE_LEFT 7
#define NET_WM_MOVERESIZE_MOVE 8

#define NET_WM_STATE_REMOVE 0
#define NET_WM_STATE_ADD 1
#define NET_WM_STATE_TOGGLE 2

#define WM_STATE_WITHDRAWN 0
#define WM_STATE_NORMAL 1
#define WM_STATE_ICONIC 3

void sl_window_update(struct sl_window* window);
void sl_update_application_id(struct sl_context* ctx, struct sl_window* window);
void sl_configure_window(struct sl_window* window);
void sl_send_configure_notify(struct sl_window* window);

int sl_process_pending_configure_acks(struct sl_window* window,
                                      struct sl_host_surface* host_surface);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_WINDOW_H_

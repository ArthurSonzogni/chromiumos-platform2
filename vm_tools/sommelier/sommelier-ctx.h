// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_CTX_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_CTX_H_

#include <memory>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xcb/xcb.h>

#include "sommelier-timing.h"  // NOLINT(build/include_directory)
#include "sommelier-util.h"    // NOLINT(build/include_directory)
#include "virtualization/wayland_channel.h"

// A list of atoms to intern (create/fetch) when connecting to the X server.
//
// To add an atom, declare it here and define it in |sl_context_atom_name|.
enum {
  ATOM_WM_S0,
  ATOM_WM_PROTOCOLS,
  ATOM_WM_STATE,
  ATOM_WM_CHANGE_STATE,
  ATOM_WM_DELETE_WINDOW,
  ATOM_WM_TAKE_FOCUS,
  ATOM_WM_CLIENT_LEADER,
  ATOM_WL_SURFACE_ID,
  ATOM_UTF8_STRING,
  ATOM_MOTIF_WM_HINTS,
  ATOM_NET_ACTIVE_WINDOW,
  ATOM_NET_FRAME_EXTENTS,
  ATOM_NET_STARTUP_ID,
  ATOM_NET_SUPPORTED,
  ATOM_NET_SUPPORTING_WM_CHECK,
  ATOM_NET_WM_NAME,
  ATOM_NET_WM_MOVERESIZE,
  ATOM_NET_WM_STATE,
  ATOM_NET_WM_STATE_FULLSCREEN,
  ATOM_NET_WM_STATE_MAXIMIZED_VERT,
  ATOM_NET_WM_STATE_MAXIMIZED_HORZ,
  ATOM_NET_WM_STATE_FOCUSED,
  ATOM_CLIPBOARD,
  ATOM_CLIPBOARD_MANAGER,
  ATOM_TARGETS,
  ATOM_TIMESTAMP,
  ATOM_TEXT,
  ATOM_INCR,
  ATOM_WL_SELECTION,
  ATOM_GTK_THEME_VARIANT,
  ATOM_XWAYLAND_RANDR_EMU_MONITOR_RECTS,
  ATOM_LAST = ATOM_XWAYLAND_RANDR_EMU_MONITOR_RECTS,
};

struct sl_context {
  char** runprog;
  struct wl_display* display;
  struct wl_display* host_display;
  struct wl_client* client;
  struct sl_compositor* compositor;
  struct sl_subcompositor* subcompositor;
  struct sl_shm* shm;
  struct sl_shell* shell;
  struct sl_data_device_manager* data_device_manager;
  struct sl_xdg_shell* xdg_shell;
  struct sl_aura_shell* aura_shell;
  struct sl_viewporter* viewporter;
  struct sl_linux_dmabuf* linux_dmabuf;
  struct sl_linux_explicit_synchronization* linux_explicit_synchronization;
  struct sl_keyboard_extension* keyboard_extension;
  struct sl_text_input_manager* text_input_manager;
#ifdef GAMEPAD_SUPPORT
  struct sl_gaming_input_manager* gaming_input_manager;
#endif
  struct sl_relative_pointer_manager* relative_pointer_manager;
  struct sl_pointer_constraints* pointer_constraints;
  struct wl_list outputs;
  struct wl_list seats;
  std::unique_ptr<struct wl_event_source> display_event_source;
  std::unique_ptr<struct wl_event_source> display_ready_event_source;
  std::unique_ptr<struct wl_event_source> sigchld_event_source;
  std::unique_ptr<struct wl_event_source> sigusr1_event_source;
  std::unique_ptr<struct wl_event_source> clipboard_event_source;
  struct wl_array dpi;
  int wm_fd;
  int wayland_channel_fd;
  int virtwl_socket_fd;
  int virtwl_display_fd;
  std::unique_ptr<struct wl_event_source> wayland_channel_event_source;
  std::unique_ptr<struct wl_event_source> virtwl_socket_event_source;
  const char* vm_id;
  const char* drm_device;
  struct gbm_device* gbm;
  int xwayland;
  pid_t xwayland_pid;
  pid_t child_pid;
  pid_t peer_pid;
  struct xkb_context* xkb_context;
  struct wl_list accelerators;
  struct wl_list registries;
  struct wl_list globals;
  struct wl_list host_outputs;
  int next_global_id;
  xcb_connection_t* connection;
  std::unique_ptr<struct wl_event_source> connection_event_source;
  const xcb_query_extension_reply_t* xfixes_extension;
  xcb_screen_t* screen;
  xcb_window_t window;
  struct wl_list windows, unpaired_windows;
  struct sl_window* host_focus_window;
  int needs_set_input_focus;
#ifdef GAMEPAD_SUPPORT
  struct wl_list gamepads;
#endif
  double desired_scale;
  double scale;
  const char* application_id;
  int exit_with_child;
  const char* sd_notify;
  int clipboard_manager;
  uint32_t frame_color;
  uint32_t dark_frame_color;
  bool support_damage_buffer;
  int fullscreen_mode;
  struct sl_host_seat* default_seat;
  xcb_window_t selection_window;
  xcb_window_t selection_owner;
  int selection_incremental_transfer;
  xcb_selection_request_event_t selection_request;
  xcb_timestamp_t selection_timestamp;
  struct wl_data_device* selection_data_device;
  struct sl_data_offer* selection_data_offer;
  struct sl_data_source* selection_data_source;
  int selection_data_source_send_fd;
  struct wl_list selection_data_source_send_pending;
  std::unique_ptr<struct wl_event_source> selection_send_event_source;
  xcb_get_property_reply_t* selection_property_reply;
  int selection_property_offset;
  std::unique_ptr<struct wl_event_source> selection_event_source;
  xcb_atom_t selection_data_type;
  struct wl_array selection_data;
  int selection_data_offer_receive_fd;
  int selection_data_ack_pending;
  union {
    const char* name;
    xcb_intern_atom_cookie_t cookie;
    xcb_atom_t value;
  } atoms[ATOM_LAST + 1];
  xcb_visualid_t visual_ids[256];
  xcb_colormap_t colormaps[256];
  Timing* timing;
  const char* trace_filename;
  bool trace_system;
  bool use_explicit_fence;
  bool use_virtgpu_channel;
  // Never freed after allocation due the fact sommelier doesn't have a
  // shutdown function yet.
  WaylandChannel* channel;
};

// Returns the string mapped to the given ATOM_ enum value.
//
// Note this is NOT the atom value sent via the X protocol, despite both being
// ints. Use |sl_context::atoms| to map between X protocol atoms and ATOM_ enum
// values: If `atoms[i].value = j`, i is the ATOM_ enum value and j is the
// X protocol atom.
//
// If the given value is out of range of the ATOM_ enum, returns NULL.
const char* sl_context_atom_name(int atom_enum);
void sl_context_init_default(struct sl_context* ctx);

bool sl_context_init_wayland_channel(struct sl_context* ctx,
                                     struct wl_event_loop* event_loop,
                                     bool display);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_CTX_H_

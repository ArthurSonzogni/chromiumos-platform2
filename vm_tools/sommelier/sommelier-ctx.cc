// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier-ctx.h"  // NOLINT(build/include_directory)

#include <assert.h>

#include "aura-shell-client-protocol.h"  // NOLINT(build/include_directory)
#include "sommelier.h"                   // NOLINT(build/include_directory)

// TODO(b/173147612): Use container_token rather than this name.
#define DEFAULT_VM_NAME "termina"

// Returns the string mapped to the given ATOM_ enum value.
//
// Note this is NOT the atom value sent via the X protocol, despite both being
// ints. Use |sl_context::atoms| to map between X protocol atoms and ATOM_ enum
// values: If `atoms[i].value = j`, i is the ATOM_ enum value and j is the
// X protocol atom.
//
// If the given value is out of range of the ATOM_ enum, returns NULL.
const char* sl_context_atom_name(int atom_enum) {
  switch (atom_enum) {
    case ATOM_WM_S0:
      return "WM_S0";
    case ATOM_WM_PROTOCOLS:
      return "WM_PROTOCOLS";
    case ATOM_WM_STATE:
      return "WM_STATE";
    case ATOM_WM_CHANGE_STATE:
      return "WM_CHANGE_STATE";
    case ATOM_WM_DELETE_WINDOW:
      return "WM_DELETE_WINDOW";
    case ATOM_WM_TAKE_FOCUS:
      return "WM_TAKE_FOCUS";
    case ATOM_WM_CLIENT_LEADER:
      return "WM_CLIENT_LEADER";
    case ATOM_WL_SURFACE_ID:
      return "WL_SURFACE_ID";
    case ATOM_UTF8_STRING:
      return "UTF8_STRING";
    case ATOM_MOTIF_WM_HINTS:
      return "_MOTIF_WM_HINTS";
    case ATOM_NET_ACTIVE_WINDOW:
      return "_NET_ACTIVE_WINDOW";
    case ATOM_NET_FRAME_EXTENTS:
      return "_NET_FRAME_EXTENTS";
    case ATOM_NET_STARTUP_ID:
      return "_NET_STARTUP_ID";
    case ATOM_NET_SUPPORTED:
      return "_NET_SUPPORTED";
    case ATOM_NET_SUPPORTING_WM_CHECK:
      return "_NET_SUPPORTING_WM_CHECK";
    case ATOM_NET_WM_NAME:
      return "_NET_WM_NAME";
    case ATOM_NET_WM_MOVERESIZE:
      return "_NET_WM_MOVERESIZE";
    case ATOM_NET_WM_STATE:
      return "_NET_WM_STATE";
    case ATOM_NET_WM_STATE_FULLSCREEN:
      return "_NET_WM_STATE_FULLSCREEN";
    case ATOM_NET_WM_STATE_MAXIMIZED_VERT:
      return "_NET_WM_STATE_MAXIMIZED_VERT";
    case ATOM_NET_WM_STATE_MAXIMIZED_HORZ:
      return "_NET_WM_STATE_MAXIMIZED_HORZ";
    case ATOM_NET_WM_STATE_FOCUSED:
      return "_NET_WM_STATE_FOCUSED";
    case ATOM_CLIPBOARD:
      return "CLIPBOARD";
    case ATOM_CLIPBOARD_MANAGER:
      return "CLIPBOARD_MANAGER";
    case ATOM_TARGETS:
      return "TARGETS";
    case ATOM_TIMESTAMP:
      return "TIMESTAMP";
    case ATOM_TEXT:
      return "TEXT";
    case ATOM_INCR:
      return "INCR";
    case ATOM_WL_SELECTION:
      return "_WL_SELECTION";
    case ATOM_GTK_THEME_VARIANT:
      return "_GTK_THEME_VARIANT";
    case ATOM_XWAYLAND_RANDR_EMU_MONITOR_RECTS:
      return "_XWAYLAND_RANDR_EMU_MONITOR_RECTS";
  }
  return NULL;
}

void sl_context_init_default(struct sl_context* ctx) {
  *ctx = {0};
  ctx->runprog = NULL;
  ctx->display = NULL;
  ctx->host_display = NULL;
  ctx->client = NULL;
  ctx->compositor = NULL;
  ctx->subcompositor = NULL;
  ctx->shm = NULL;
  ctx->shell = NULL;
  ctx->data_device_manager = NULL;
  ctx->xdg_shell = NULL;
  ctx->aura_shell = NULL;
  ctx->viewporter = NULL;
  ctx->linux_dmabuf = NULL;
  ctx->keyboard_extension = NULL;
  ctx->text_input_manager = NULL;
#ifdef GAMEPAD_SUPPORT
  ctx->gaming_input_manager = NULL;
#endif
  ctx->display_event_source = NULL;
  ctx->display_ready_event_source = NULL;
  ctx->sigchld_event_source = NULL;
  ctx->sigusr1_event_source = NULL;
  ctx->wm_fd = -1;
  ctx->virtwl_ctx_fd = -1;
  ctx->virtwl_socket_fd = -1;
  ctx->virtwl_ctx_event_source = NULL;
  ctx->virtwl_socket_event_source = NULL;
  ctx->vm_id = DEFAULT_VM_NAME;
  ctx->drm_device = NULL;
  ctx->gbm = NULL;
  ctx->xwayland = 0;
  ctx->xwayland_pid = -1;
  ctx->child_pid = -1;
  ctx->peer_pid = -1;
  ctx->xkb_context = NULL;
  ctx->next_global_id = 1;
  ctx->connection = NULL;
  ctx->connection_event_source = NULL;
  ctx->xfixes_extension = NULL;
  ctx->screen = NULL;
  ctx->window = 0;
  ctx->host_focus_window = NULL;
  ctx->needs_set_input_focus = 0;
  ctx->desired_scale = 1.0;
  ctx->scale = 1.0;
  ctx->application_id = NULL;
  ctx->exit_with_child = 1;
  ctx->sd_notify = NULL;
  ctx->clipboard_manager = 0;
  ctx->frame_color = 0xffffffff;
  ctx->dark_frame_color = 0xff000000;
  ctx->support_damage_buffer = true;
  ctx->fullscreen_mode = ZAURA_SURFACE_FULLSCREEN_MODE_IMMERSIVE;
  ctx->default_seat = NULL;
  ctx->selection_window = XCB_WINDOW_NONE;
  ctx->selection_owner = XCB_WINDOW_NONE;
  ctx->selection_incremental_transfer = 0;
  ctx->selection_request.requestor = XCB_NONE;
  ctx->selection_request.property = XCB_ATOM_NONE;
  ctx->selection_timestamp = XCB_CURRENT_TIME;
  ctx->selection_data_device = NULL;
  ctx->selection_data_offer = NULL;
  ctx->selection_data_source = NULL;
  ctx->selection_data_source_send_fd = -1;
  ctx->selection_send_event_source = NULL;
  ctx->selection_property_reply = NULL;
  ctx->selection_property_offset = 0;
  ctx->selection_event_source = NULL;
  ctx->selection_data_offer_receive_fd = -1;
  ctx->selection_data_ack_pending = 0;
  for (unsigned i = 0; i < ARRAY_SIZE(ctx->atoms); i++) {
    const char* name = sl_context_atom_name(i);
    assert(name != NULL);
    ctx->atoms[i].name = name;
  }
  ctx->timing = NULL;
  ctx->trace_filename = NULL;
  ctx->trace_system = false;
}

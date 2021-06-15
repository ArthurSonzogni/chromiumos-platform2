// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_CTX_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_CTX_H_

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

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_CTX_H_

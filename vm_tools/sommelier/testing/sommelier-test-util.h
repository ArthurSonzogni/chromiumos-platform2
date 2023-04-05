// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_TESTING_SOMMELIER_TEST_UTIL_H_
#define VM_TOOLS_SOMMELIER_TESTING_SOMMELIER_TEST_UTIL_H_

#include <wayland-server.h>

#include "../sommelier.h"                // NOLINT(build/include_directory)
#include "aura-shell-client-protocol.h"  // NOLINT(build/include_directory)
#include "viewporter-client-protocol.h"  // NOLINT(build/include_directory)
#include "xdg-output-unstable-v1-client-protocol.h"  // NOLINT(build/include_directory)
#include "xdg-shell-client-protocol.h"   // NOLINT(build/include_directory)

namespace vm_tools {
namespace sommelier {

// This family of functions retrieves Sommelier's listeners for events received
// from the host, so we can call them directly in the test rather than
// (a) exporting the actual functions (which are typically static), or (b)
// creating a fake host compositor to dispatch events via libwayland
// (unnecessarily complicated).
const zaura_toplevel_listener* HostEventHandler(
    struct zaura_toplevel* aura_toplevel);

const xdg_surface_listener* HostEventHandler(struct xdg_surface* xdg_surface);

const xdg_toplevel_listener* HostEventHandler(
    struct xdg_toplevel* xdg_toplevel);

const wl_callback_listener* HostEventHandler(struct wl_callback* callback);

const wl_output_listener* HostEventHandler(struct wl_output* output);

const zaura_output_listener* HostEventHandler(struct zaura_output* output);

const wl_surface_listener* HostEventHandler(struct wl_surface* surface);

const zxdg_output_v1_listener* HostEventHandler(struct zxdg_output_v1* output);

uint32_t XdgToplevelId(sl_window* window);
uint32_t AuraSurfaceId(sl_window* window);
uint32_t AuraToplevelId(sl_window* window);

}  // namespace sommelier
}  // namespace vm_tools

#endif  // VM_TOOLS_SOMMELIER_TESTING_SOMMELIER_TEST_UTIL_H_

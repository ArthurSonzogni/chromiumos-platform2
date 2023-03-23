// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier-test-util.h"  // NOLINT(build/include_directory)

#include <gtest/gtest.h>

namespace vm_tools {
namespace sommelier {

const zaura_toplevel_listener* HostEventHandler(
    struct zaura_toplevel* aura_toplevel) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(aura_toplevel));
  EXPECT_NE(listener, nullptr);
  return static_cast<const zaura_toplevel_listener*>(listener);
}

const xdg_surface_listener* HostEventHandler(struct xdg_surface* xdg_surface) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(xdg_surface));
  EXPECT_NE(listener, nullptr);
  return static_cast<const xdg_surface_listener*>(listener);
}

const xdg_toplevel_listener* HostEventHandler(
    struct xdg_toplevel* xdg_toplevel) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(xdg_toplevel));
  EXPECT_NE(listener, nullptr);
  return static_cast<const xdg_toplevel_listener*>(listener);
}

const wl_output_listener* HostEventHandler(struct wl_output* output) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(output));
  EXPECT_NE(listener, nullptr);
  return static_cast<const wl_output_listener*>(listener);
}

const zaura_output_listener* HostEventHandler(struct zaura_output* output) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(output));
  EXPECT_NE(listener, nullptr);
  return static_cast<const zaura_output_listener*>(listener);
}

const wl_surface_listener* HostEventHandler(struct wl_surface* surface) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(surface));
  EXPECT_NE(listener, nullptr);
  return static_cast<const wl_surface_listener*>(listener);
}

const zxdg_output_v1_listener* HostEventHandler(struct zxdg_output_v1* output) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(output));
  EXPECT_NE(listener, nullptr);
  return static_cast<const zxdg_output_v1_listener*>(listener);
}

uint32_t XdgToplevelId(sl_window* window) {
  assert(window->xdg_toplevel);
  return wl_proxy_get_id(reinterpret_cast<wl_proxy*>(window->xdg_toplevel));
}

uint32_t AuraSurfaceId(sl_window* window) {
  assert(window->aura_surface);
  return wl_proxy_get_id(reinterpret_cast<wl_proxy*>(window->aura_surface));
}

uint32_t AuraToplevelId(sl_window* window) {
  assert(window->aura_toplevel);
  return wl_proxy_get_id(reinterpret_cast<wl_proxy*>(window->aura_toplevel));
}

}  // namespace sommelier
}  // namespace vm_tools

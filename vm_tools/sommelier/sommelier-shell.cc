// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier.h"  // NOLINT(build/include_directory)
#include "sommelier-tracing.h"  // NOLINT(build/include_directory)

#include <assert.h>
#include <stdlib.h>
#include <wayland-client.h>

struct sl_host_shell_surface {
  struct wl_resource* resource;
  struct wl_shell_surface* proxy;
};
MAP_STRUCTS(wl_shell_surface, sl_host_shell_surface);

struct sl_host_shell {
  struct sl_shell* shell;
  struct wl_resource* resource;
  struct wl_shell* proxy;
};

static const struct wl_shell_surface_interface sl_shell_surface_implementation =
    {
        ForwardRequest<wl_shell_surface_pong>,
        ForwardRequest<wl_shell_surface_move>,
        ForwardRequest<wl_shell_surface_resize>,
        ForwardRequest<wl_shell_surface_set_toplevel>,
        ForwardRequest<wl_shell_surface_set_transient>,
        ForwardRequest<wl_shell_surface_set_fullscreen,
                       AllowNullResource::kYes>,
        ForwardRequest<wl_shell_surface_set_popup>,
        ForwardRequest<wl_shell_surface_set_maximized, AllowNullResource::kYes>,
        ForwardRequest<wl_shell_surface_set_title>,
        ForwardRequest<wl_shell_surface_set_class>,
};

static void sl_shell_surface_ping(void* data,
                                  struct wl_shell_surface* shell_surface,
                                  uint32_t serial) {
  struct sl_host_shell_surface* host = static_cast<sl_host_shell_surface*>(
      wl_shell_surface_get_user_data(shell_surface));

  wl_shell_surface_send_ping(host->resource, serial);
}

static void sl_shell_surface_configure(void* data,
                                       struct wl_shell_surface* shell_surface,
                                       uint32_t edges,
                                       int32_t width,
                                       int32_t height) {
  TRACE_EVENT("shell", "sl_shell_surface_configure");
  struct sl_host_shell_surface* host = static_cast<sl_host_shell_surface*>(
      wl_shell_surface_get_user_data(shell_surface));

  wl_shell_surface_send_configure(host->resource, edges, width, height);
}

static void sl_shell_surface_popup_done(
    void* data, struct wl_shell_surface* shell_surface) {
  struct sl_host_shell_surface* host = static_cast<sl_host_shell_surface*>(
      wl_shell_surface_get_user_data(shell_surface));

  wl_shell_surface_send_popup_done(host->resource);
}

static const struct wl_shell_surface_listener sl_shell_surface_listener = {
    sl_shell_surface_ping, sl_shell_surface_configure,
    sl_shell_surface_popup_done};

static void sl_destroy_host_shell_surface(struct wl_resource* resource) {
  struct sl_host_shell_surface* host =
      static_cast<sl_host_shell_surface*>(wl_resource_get_user_data(resource));

  wl_shell_surface_destroy(host->proxy);
  wl_resource_set_user_data(resource, NULL);
  free(host);
}

static void sl_host_shell_get_shell_surface(
    struct wl_client* client,
    struct wl_resource* resource,
    uint32_t id,
    struct wl_resource* surface_resource) {
  struct sl_host_shell* host =
      static_cast<sl_host_shell*>(wl_resource_get_user_data(resource));
  struct sl_host_surface* host_surface = static_cast<sl_host_surface*>(
      wl_resource_get_user_data(surface_resource));
  struct sl_host_shell_surface* host_shell_surface =
      static_cast<sl_host_shell_surface*>(malloc(sizeof(*host_shell_surface)));
  assert(host_shell_surface);
  host_shell_surface->resource =
      wl_resource_create(client, &wl_shell_surface_interface, 1, id);
  wl_resource_set_implementation(
      host_shell_surface->resource, &sl_shell_surface_implementation,
      host_shell_surface, sl_destroy_host_shell_surface);
  host_shell_surface->proxy =
      wl_shell_get_shell_surface(host->proxy, host_surface->proxy);
  wl_shell_surface_set_user_data(host_shell_surface->proxy, host_shell_surface);
  wl_shell_surface_add_listener(host_shell_surface->proxy,
                                &sl_shell_surface_listener, host_shell_surface);
  host_surface->has_role = 1;
}  // NOLINT(whitespace/indent)

static const struct wl_shell_interface sl_shell_implementation = {
    sl_host_shell_get_shell_surface};

static void sl_destroy_host_shell(struct wl_resource* resource) {
  struct sl_host_shell* host =
      static_cast<sl_host_shell*>(wl_resource_get_user_data(resource));

  wl_shell_destroy(host->proxy);
  wl_resource_set_user_data(resource, NULL);
  free(host);
}

static void sl_bind_host_shell(struct wl_client* client,
                               void* data,
                               uint32_t version,
                               uint32_t id) {
  struct sl_context* ctx = (struct sl_context*)data;
  struct sl_host_shell* host =
      static_cast<sl_host_shell*>(malloc(sizeof(*host)));
  assert(host);
  host->shell = ctx->shell;
  host->resource = wl_resource_create(client, &wl_shell_interface, 1, id);
  wl_resource_set_implementation(host->resource, &sl_shell_implementation, host,
                                 sl_destroy_host_shell);
  host->proxy = static_cast<wl_shell*>(wl_registry_bind(
      wl_display_get_registry(ctx->display), ctx->shell->id,
      &wl_shell_interface, wl_resource_get_version(host->resource)));
  wl_shell_set_user_data(host->proxy, host);
}

struct sl_global* sl_shell_global_create(struct sl_context* ctx) {
  return sl_global_create(ctx, &wl_shell_interface, 1, ctx, sl_bind_host_shell);
}

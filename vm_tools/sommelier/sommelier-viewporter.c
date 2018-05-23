// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier.h"

#include <assert.h>
#include <stdlib.h>

#include "viewporter-client-protocol.h"
#include "viewporter-server-protocol.h"

struct sl_host_viewporter {
  struct sl_viewporter* viewporter;
  struct wl_resource* resource;
  struct wp_viewporter* proxy;
};

struct sl_host_viewport {
  struct wl_resource* resource;
  struct sl_viewport viewport;
};

static void sl_viewport_destroy(struct wl_client* client,
                                struct wl_resource* resource) {
  wl_resource_destroy(resource);
}

static void sl_viewport_set_source(struct wl_client* client,
                                   struct wl_resource* resource,
                                   wl_fixed_t x,
                                   wl_fixed_t y,
                                   wl_fixed_t width,
                                   wl_fixed_t height) {
  struct sl_host_viewport* host = wl_resource_get_user_data(resource);

  host->viewport.src_x = x;
  host->viewport.src_y = y;
  host->viewport.src_width = width;
  host->viewport.src_height = height;
}

static void sl_viewport_set_destination(struct wl_client* client,
                                        struct wl_resource* resource,
                                        int32_t width,
                                        int32_t height) {
  struct sl_host_viewport* host = wl_resource_get_user_data(resource);

  host->viewport.dst_width = width;
  host->viewport.dst_height = height;
}

static const struct wp_viewport_interface sl_viewport_implementation = {
    sl_viewport_destroy, sl_viewport_set_source, sl_viewport_set_destination};

static void sl_destroy_host_viewport(struct wl_resource* resource) {
  struct sl_host_viewport* host = wl_resource_get_user_data(resource);

  wl_resource_set_user_data(resource, NULL);
  wl_list_remove(&host->viewport.link);
  free(host);
}

static void sl_viewporter_destroy(struct wl_client* client,
                                  struct wl_resource* resource) {
  wl_resource_destroy(resource);
}

static void sl_viewporter_get_viewport(struct wl_client* client,
                                       struct wl_resource* resource,
                                       uint32_t id,
                                       struct wl_resource* surface_resource) {
  struct sl_host_surface* host_surface =
      wl_resource_get_user_data(surface_resource);
  struct sl_host_viewport* host_viewport;

  host_viewport = malloc(sizeof(*host_viewport));
  assert(host_viewport);

  host_viewport->viewport.src_x = -1;
  host_viewport->viewport.src_y = -1;
  host_viewport->viewport.src_width = -1;
  host_viewport->viewport.src_height = -1;
  host_viewport->viewport.dst_width = -1;
  host_viewport->viewport.dst_height = -1;
  wl_list_insert(&host_surface->contents_viewport,
                 &host_viewport->viewport.link);
  host_viewport->resource =
      wl_resource_create(client, &wp_viewport_interface, 1, id);
  wl_resource_set_implementation(host_viewport->resource,
                                 &sl_viewport_implementation, host_viewport,
                                 sl_destroy_host_viewport);
}

static const struct wp_viewporter_interface sl_viewporter_implementation = {
    sl_viewporter_destroy, sl_viewporter_get_viewport};

static void sl_destroy_host_viewporter(struct wl_resource* resource) {
  struct sl_host_viewporter* host = wl_resource_get_user_data(resource);

  wp_viewporter_destroy(host->proxy);
  wl_resource_set_user_data(resource, NULL);
  free(host);
}

static void sl_bind_host_viewporter(struct wl_client* client,
                                    void* data,
                                    uint32_t version,
                                    uint32_t id) {
  struct sl_context* ctx = (struct sl_context*)data;
  struct sl_host_viewporter* host;

  host = malloc(sizeof(*host));
  assert(host);
  host->viewporter = ctx->viewporter;
  host->resource = wl_resource_create(client, &wp_viewporter_interface, 1, id);
  wl_resource_set_implementation(host->resource, &sl_viewporter_implementation,
                                 host, sl_destroy_host_viewporter);
  host->proxy =
      wl_registry_bind(wl_display_get_registry(ctx->display),
                       ctx->viewporter->id, &wp_viewporter_interface, 1);
  wp_viewporter_set_user_data(host->proxy, host);
}

struct sl_global* sl_viewporter_global_create(struct sl_context* ctx) {
  return sl_global_create(ctx, &wp_viewporter_interface, 1, ctx,
                          sl_bind_host_viewporter);
}
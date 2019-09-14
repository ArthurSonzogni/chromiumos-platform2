// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-server-core.h>

#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"

#define SL_HOST_OBJECT(NAME, INTERFACE)                                \
  struct sl_host_##NAME {                                              \
    struct sl_context* ctx;                                            \
    struct wl_resource* resource;                                      \
    struct INTERFACE* proxy;                                           \
  };                                                                   \
  static void sl_destroy_host_##NAME(struct wl_resource* resource) {   \
    struct sl_host_##NAME* host = wl_resource_get_user_data(resource); \
    INTERFACE##_destroy(host->proxy);                                  \
    wl_resource_set_user_data(resource, NULL);                         \
    free(host);                                                        \
  }                                                                    \
  static void sl_##NAME##_destroy(struct wl_client* client,            \
                                  struct wl_resource* resource) {      \
    wl_resource_destroy(resource);                                     \
  }

SL_HOST_OBJECT(pointer_constraints, zwp_pointer_constraints_v1);

SL_HOST_OBJECT(locked_pointer, zwp_locked_pointer_v1);

SL_HOST_OBJECT(confined_pointer, zwp_confined_pointer_v1);

static void sl_locked_pointer_locked(
    void* data, struct zwp_locked_pointer_v1* locked_pointer) {
  struct sl_host_locked_pointer* host =
      zwp_locked_pointer_v1_get_user_data(locked_pointer);

  zwp_locked_pointer_v1_send_locked(host->resource);
}

static void sl_locked_pointer_unlocked(
    void* data, struct zwp_locked_pointer_v1* locked_pointer) {
  struct sl_host_locked_pointer* host =
      zwp_locked_pointer_v1_get_user_data(locked_pointer);

  zwp_locked_pointer_v1_send_unlocked(host->resource);
}

static void sl_locked_pointer_set_cursor_position_hint(
    struct wl_client* client,
    struct wl_resource* resource,
    wl_fixed_t surface_x,
    wl_fixed_t surface_y) {
  struct sl_host_locked_pointer* host = wl_resource_get_user_data(resource);

  zwp_locked_pointer_v1_set_cursor_position_hint(host->proxy, surface_x,
                                                 surface_y);
}

static void sl_locked_pointer_set_region(struct wl_client* client,
                                         struct wl_resource* resource,
                                         struct wl_resource* region) {
  struct sl_host_locked_pointer* host = wl_resource_get_user_data(resource);
  struct sl_host_region* host_region =
      region ? wl_resource_get_user_data(region) : NULL;

  zwp_locked_pointer_v1_set_region(host->proxy,
                                   host_region ? host_region->proxy : NULL);
}

static struct zwp_locked_pointer_v1_listener sl_locked_pointer_listener = {
    sl_locked_pointer_locked,
    sl_locked_pointer_unlocked,
};

static struct zwp_locked_pointer_v1_interface sl_locked_pointer_implementation =
    {sl_locked_pointer_destroy, sl_locked_pointer_set_cursor_position_hint,
     sl_locked_pointer_set_region};

static void sl_confined_pointer_confined(
    void* data, struct zwp_confined_pointer_v1* confined_pointer) {
  struct sl_host_confined_pointer* host =
      zwp_confined_pointer_v1_get_user_data(confined_pointer);

  zwp_confined_pointer_v1_send_confined(host->resource);
}

static void sl_confined_pointer_unconfined(
    void* data, struct zwp_confined_pointer_v1* confined_pointer) {
  struct sl_host_confined_pointer* host =
      zwp_confined_pointer_v1_get_user_data(confined_pointer);

  zwp_confined_pointer_v1_send_unconfined(host->resource);
}

static void sl_confined_pointer_set_region(struct wl_client* client,
                                           struct wl_resource* resource,
                                           struct wl_resource* region) {
  struct sl_host_confined_pointer* host = wl_resource_get_user_data(resource);
  struct sl_host_region* host_region =
      region ? wl_resource_get_user_data(region) : NULL;

  zwp_confined_pointer_v1_set_region(host->proxy,
                                     host_region ? host_region->proxy : NULL);
}

static struct zwp_confined_pointer_v1_listener sl_confined_pointer_listener = {
    sl_confined_pointer_confined,
    sl_confined_pointer_unconfined,
};

static struct zwp_confined_pointer_v1_interface
    sl_confined_pointer_implementation = {
        sl_confined_pointer_destroy,
        sl_confined_pointer_set_region,
};

static void sl_pointer_constraints_lock_pointer(struct wl_client* client,
                                                struct wl_resource* resource,
                                                uint32_t id,
                                                struct wl_resource* surface,
                                                struct wl_resource* pointer,
                                                struct wl_resource* region,
                                                uint32_t lifetime) {
  struct sl_host_pointer_constraints* host =
      wl_resource_get_user_data(resource);
  struct wl_resource* locked_pointer_resource =
      wl_resource_create(client, &zwp_locked_pointer_v1_interface, 1, id);
  struct sl_host_locked_pointer* locked_pointer_host;

  struct sl_host_surface* host_surface = wl_resource_get_user_data(surface);
  struct sl_host_pointer* host_pointer = wl_resource_get_user_data(pointer);
  struct sl_host_region* host_region =
      region ? wl_resource_get_user_data(region) : NULL;

  locked_pointer_host = malloc(sizeof(struct sl_host_locked_pointer));
  assert(locked_pointer_host);
  locked_pointer_host->resource = locked_pointer_resource;
  locked_pointer_host->ctx = host->ctx;
  locked_pointer_host->proxy = zwp_pointer_constraints_v1_lock_pointer(
      host->ctx->pointer_constraints->internal, host_surface->proxy,
      host_pointer->proxy, host_region ? host_region->proxy : NULL, lifetime);
  wl_resource_set_implementation(
      locked_pointer_resource, &sl_locked_pointer_implementation,
      locked_pointer_host, sl_destroy_host_locked_pointer);
  zwp_locked_pointer_v1_set_user_data(locked_pointer_host->proxy,
                                      locked_pointer_host);
  zwp_locked_pointer_v1_add_listener(locked_pointer_host->proxy,
                                     &sl_locked_pointer_listener,
                                     locked_pointer_host);
}

static void sl_pointer_constraints_confine_pointer(struct wl_client* client,
                                                   struct wl_resource* resource,
                                                   uint32_t id,
                                                   struct wl_resource* surface,
                                                   struct wl_resource* pointer,
                                                   struct wl_resource* region,
                                                   uint32_t lifetime) {
  struct sl_host_pointer_constraints* host =
      wl_resource_get_user_data(resource);
  struct wl_resource* confined_pointer_resource =
      wl_resource_create(client, &zwp_confined_pointer_v1_interface, 1, id);
  struct sl_host_confined_pointer* confined_pointer_host;

  struct sl_host_surface* host_surface = wl_resource_get_user_data(surface);
  struct sl_host_pointer* host_pointer = wl_resource_get_user_data(pointer);
  struct sl_host_region* host_region =
      region ? wl_resource_get_user_data(region) : NULL;

  confined_pointer_host = malloc(sizeof(struct sl_host_confined_pointer));
  assert(confined_pointer_host);
  confined_pointer_host->resource = confined_pointer_resource;
  confined_pointer_host->ctx = host->ctx;
  confined_pointer_host->proxy = zwp_pointer_constraints_v1_confine_pointer(
      host->ctx->pointer_constraints->internal, host_surface->proxy,
      host_pointer->proxy, host_region ? host_region->proxy : NULL, lifetime);
  wl_resource_set_implementation(
      confined_pointer_resource, &sl_confined_pointer_implementation,
      confined_pointer_host, sl_destroy_host_confined_pointer);
  zwp_confined_pointer_v1_set_user_data(confined_pointer_host->proxy,
                                        confined_pointer_host);
  zwp_confined_pointer_v1_add_listener(confined_pointer_host->proxy,
                                       &sl_confined_pointer_listener,
                                       confined_pointer_host);
}

static struct zwp_pointer_constraints_v1_interface
    sl_pointer_constraints_implementation = {
        sl_pointer_constraints_destroy,
        sl_pointer_constraints_lock_pointer,
        sl_pointer_constraints_confine_pointer,
};

static void sl_bind_host_pointer_constraints(struct wl_client* client,
                                             void* data,
                                             uint32_t version,
                                             uint32_t id) {
  struct sl_context* ctx = (struct sl_context*)data;
  struct sl_pointer_constraints* pointer_constraints = ctx->pointer_constraints;
  struct sl_host_pointer_constraints* host =
      malloc(sizeof(struct sl_host_pointer_constraints));

  assert(host);
  host->ctx = ctx;
  host->resource =
      wl_resource_create(client, &zwp_pointer_constraints_v1_interface, 1, id);
  wl_resource_set_implementation(host->resource,
                                 &sl_pointer_constraints_implementation, host,
                                 sl_destroy_host_pointer_constraints);
  host->proxy = wl_registry_bind(wl_display_get_registry(ctx->display),
                                 pointer_constraints->id,
                                 &zwp_pointer_constraints_v1_interface,
                                 wl_resource_get_version(host->resource));
  zwp_pointer_constraints_v1_set_user_data(host->proxy, host);
}

struct sl_global* sl_pointer_constraints_global_create(struct sl_context* ctx) {
  return sl_global_create(ctx, &zwp_pointer_constraints_v1_interface, 1, ctx,
                          sl_bind_host_pointer_constraints);
}

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/mock_wayland_client.h"

wl_registry* wl_display_get_registry(wl_display*) {
  return nullptr;
}

void wl_registry_add_listener(wl_registry* registry,
                              const wl_registry_listener* listener,
                              void* data) {
  listener->global(data, registry, /*name=*/0, "zwp_text_input_manager_v1",
                   /*version=*/1);
}

void* wl_registry_bind(wl_registry* wl_registry,
                       uint32_t name,
                       const wl_interface* interface,
                       uint32_t version) {
  // Generate a non-null void*. This is currently only called once.
  static int text_input_manager;
  return &text_input_manager;
}

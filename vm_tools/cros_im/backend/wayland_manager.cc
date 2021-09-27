// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/wayland_manager.h"

#include <wayland-client.h>
#include <cstdio>
#include <cstring>

#include "text-input-unstable-v1-client-protocol.h"  // NOLINT(build/include_directory)

namespace cros_im {

namespace {

WaylandManager* g_instance = nullptr;

void on_global(void* data,
               wl_registry* registry,
               uint32_t name,
               const char* interface,
               uint32_t version) {
  reinterpret_cast<WaylandManager*>(data)->OnGlobal(registry, name, interface,
                                                    version);
}

void on_global_remove(void* data, wl_registry* registry, uint32_t name) {
  reinterpret_cast<WaylandManager*>(data)->OnGlobalRemove(registry, name);
}

constexpr wl_registry_listener kRegistryListener = {
    .global = on_global,
    .global_remove = on_global_remove,
};

}  // namespace

void WaylandManager::CreateInstance(wl_display* display) {
  if (g_instance) {
    printf("WaylandManager has already been instantiated.\n");
    return;
  }

  g_instance = new WaylandManager(display);
}

bool WaylandManager::HasInstance() {
  return g_instance;
}

WaylandManager* WaylandManager::Get() {
  return g_instance;
}

zwp_text_input_v1* WaylandManager::CreateTextInput(
    const zwp_text_input_v1_listener* listener, void* listener_data) {
  if (!text_input_manager_)
    return nullptr;
  auto* text_input =
      zwp_text_input_manager_v1_create_text_input(text_input_manager_);
  zwp_text_input_v1_set_user_data(text_input, nullptr);
  zwp_text_input_v1_add_listener(text_input, listener, listener_data);
  return text_input;
}

void WaylandManager::OnGlobal(wl_registry* registry,
                              uint32_t name,
                              const char* interface,
                              uint32_t version) {
  if (strcmp(interface, "zwp_text_input_manager_v1") != 0)
    return;
  text_input_manager_ =
      reinterpret_cast<zwp_text_input_manager_v1*>(wl_registry_bind(
          registry, name, &zwp_text_input_manager_v1_interface, version));
  text_input_manager_id_ = name;
}

void WaylandManager::OnGlobalRemove(wl_registry* registry, uint32_t name) {
  if (name != text_input_manager_id_)
    return;
  printf("The global zwp_text_input_manager_v1 was removed.\n");
  text_input_manager_ = nullptr;
  text_input_manager_id_ = 0;
}

WaylandManager::WaylandManager(wl_display* display) {
  wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &kRegistryListener, this);
}

WaylandManager::~WaylandManager() = default;

}  // namespace cros_im

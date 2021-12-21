// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_runtime_probe_client.h"

#include <string>
#include <utility>
#include <vector>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace rmad {

namespace {

const ComponentsWithIdentifier kDefaultProbedComponents = {
    {RMAD_COMPONENT_BATTERY, "fake_battery"},
    {RMAD_COMPONENT_STORAGE, "fake_storage"},
    {RMAD_COMPONENT_CAMERA, "fake_camera"},
    {RMAD_COMPONENT_TOUCHPAD, "fake_touchpad"},
    {RMAD_COMPONENT_TOUCHSCREEN, "fake_touchscreen"},
    {RMAD_COMPONENT_CELLULAR, "fake_cellular"},
    {RMAD_COMPONENT_ETHERNET, "fake_ethernet"},
    {RMAD_COMPONENT_WIRELESS, "fake_wireless"},
    {RMAD_COMPONENT_BASE_ACCELEROMETER, "fake_base_accelerometer"},
    {RMAD_COMPONENT_LID_ACCELEROMETER, "fake_lid_accelerometer"},
    {RMAD_COMPONENT_BASE_GYROSCOPE, "fake_base_gyroscope"},
    {RMAD_COMPONENT_LID_GYROSCOPE, "fake_lid_gyroscope"}};

}  // namespace

namespace fake {

bool FakeRuntimeProbeClient::ProbeCategories(
    const std::vector<RmadComponent>& categories,
    ComponentsWithIdentifier* components) {
  components->clear();
  if (categories.size()) {
    // Everything is probed.
    for (RmadComponent i : categories) {
      components->push_back({i, "fake_identifier"});
    }
  } else {
    // Return a fixed set of components.
    for (const auto& [component, identifier] : kDefaultProbedComponents) {
      components->push_back({component, identifier});
    }
  }
  return true;
}

}  // namespace fake

}  // namespace rmad

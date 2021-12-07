// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_runtime_probe_client.h"

#include <set>
#include <vector>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace {

const std::vector<rmad::RmadComponent> kDefaultProbedComponents = {
    rmad::RMAD_COMPONENT_BATTERY,
    rmad::RMAD_COMPONENT_STORAGE,
    rmad::RMAD_COMPONENT_CAMERA,
    rmad::RMAD_COMPONENT_TOUCHPAD,
    rmad::RMAD_COMPONENT_TOUCHSCREEN,
    rmad::RMAD_COMPONENT_CELLULAR,
    rmad::RMAD_COMPONENT_ETHERNET,
    rmad::RMAD_COMPONENT_WIRELESS,
    rmad::RMAD_COMPONENT_BASE_ACCELEROMETER,
    rmad::RMAD_COMPONENT_LID_ACCELEROMETER,
    rmad::RMAD_COMPONENT_BASE_GYROSCOPE,
    rmad::RMAD_COMPONENT_LID_GYROSCOPE};

}  // namespace

namespace rmad {
namespace fake {

bool FakeRuntimeProbeClient::ProbeCategories(
    const std::vector<RmadComponent>& categories,
    std::set<RmadComponent>* components) {
  components->clear();
  if (categories.size()) {
    // Everything is probed.
    for (RmadComponent i : categories) {
      components->insert(i);
    }
  } else {
    // Return a fixed set of components.
    for (RmadComponent i : kDefaultProbedComponents) {
      components->insert(i);
    }
  }
  return true;
}

}  // namespace fake
}  // namespace rmad

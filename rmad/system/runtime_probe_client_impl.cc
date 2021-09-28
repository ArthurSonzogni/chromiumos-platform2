// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/runtime_probe_client_impl.h"

#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <dbus/runtime_probe/dbus-constants.h>
#include <rmad/proto_bindings/rmad.pb.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace {

constexpr int kDefaultTimeoutMs = 10 * 1000;  // 10 seconds.

const std::unordered_map<rmad::RmadComponent,
                         runtime_probe::ProbeRequest::SupportCategory>
    kRmadToRuntimeProbeComponentMap = {
        {rmad::RMAD_COMPONENT_AUDIO_CODEC,
         runtime_probe::ProbeRequest::audio_codec},
        {rmad::RMAD_COMPONENT_BATTERY, runtime_probe::ProbeRequest::battery},
        {rmad::RMAD_COMPONENT_STORAGE, runtime_probe::ProbeRequest::storage},
        {rmad::RMAD_COMPONENT_VPD_CACHED,
         runtime_probe::ProbeRequest::vpd_cached},
        {rmad::RMAD_COMPONENT_NETWORK, runtime_probe::ProbeRequest::network},
        {rmad::RMAD_COMPONENT_CAMERA, runtime_probe::ProbeRequest::camera},
        {rmad::RMAD_COMPONENT_STYLUS, runtime_probe::ProbeRequest::stylus},
        {rmad::RMAD_COMPONENT_TOUCHPAD, runtime_probe::ProbeRequest::touchpad},
        {rmad::RMAD_COMPONENT_TOUCHSCREEN,
         runtime_probe::ProbeRequest::touchscreen},
        {rmad::RMAD_COMPONENT_DRAM, runtime_probe::ProbeRequest::dram},
        {rmad::RMAD_COMPONENT_DISPLAY_PANEL,
         runtime_probe::ProbeRequest::display_panel},
        {rmad::RMAD_COMPONENT_CELLULAR, runtime_probe::ProbeRequest::cellular},
        {rmad::RMAD_COMPONENT_ETHERNET, runtime_probe::ProbeRequest::ethernet},
        {rmad::RMAD_COMPONENT_WIRELESS, runtime_probe::ProbeRequest::wireless}};

const std::vector<
    std::pair<rmad::RmadComponent, int (runtime_probe::ProbeResult::*)() const>>
    kProbedComponentSizes = {
        {rmad::RMAD_COMPONENT_AUDIO_CODEC,
         &runtime_probe::ProbeResult::audio_codec_size},
        {rmad::RMAD_COMPONENT_BATTERY,
         &runtime_probe::ProbeResult::battery_size},
        {rmad::RMAD_COMPONENT_STORAGE,
         &runtime_probe::ProbeResult::storage_size},
        {rmad::RMAD_COMPONENT_CAMERA, &runtime_probe::ProbeResult::camera_size},
        {rmad::RMAD_COMPONENT_STYLUS, &runtime_probe::ProbeResult::stylus_size},
        {rmad::RMAD_COMPONENT_TOUCHPAD,
         &runtime_probe::ProbeResult::touchpad_size},
        {rmad::RMAD_COMPONENT_TOUCHSCREEN,
         &runtime_probe::ProbeResult::touchscreen_size},
        {rmad::RMAD_COMPONENT_DRAM, &runtime_probe::ProbeResult::dram_size},
        {rmad::RMAD_COMPONENT_DISPLAY_PANEL,
         &runtime_probe::ProbeResult::display_panel_size},
        {rmad::RMAD_COMPONENT_CELLULAR,
         &runtime_probe::ProbeResult::cellular_size},
        {rmad::RMAD_COMPONENT_ETHERNET,
         &runtime_probe::ProbeResult::ethernet_size},
        {rmad::RMAD_COMPONENT_WIRELESS,
         &runtime_probe::ProbeResult::wireless_size},
};

}  // namespace

namespace rmad {

RuntimeProbeClientImpl::RuntimeProbeClientImpl(
    const scoped_refptr<dbus::Bus>& bus) {
  proxy_ = bus->GetObjectProxy(
      runtime_probe::kRuntimeProbeServiceName,
      dbus::ObjectPath(runtime_probe::kRuntimeProbeServicePath));
}

bool RuntimeProbeClientImpl::ProbeCategories(
    const std::vector<RmadComponent>& categories,
    std::set<RmadComponent>* components) {
  dbus::MethodCall method_call(runtime_probe::kRuntimeProbeInterfaceName,
                               runtime_probe::kProbeCategoriesMethod);
  dbus::MessageWriter writer(&method_call);
  runtime_probe::ProbeRequest request;
  if (categories.size()) {
    request.set_probe_default_category(false);
    for (RmadComponent category : categories) {
      CHECK_NE(kRmadToRuntimeProbeComponentMap.count(category), 0);
      request.add_categories(kRmadToRuntimeProbeComponentMap.at(category));
    }
  } else {
    request.set_probe_default_category(true);
  }
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode runtime_probe protobuf request";
    return false;
  }

  std::unique_ptr<dbus::Response> response =
      proxy_->CallMethodAndBlock(&method_call, kDefaultTimeoutMs);
  if (!response.get()) {
    LOG(ERROR) << "Failed to call runtime_probe D-Bus service";
    return false;
  }

  runtime_probe::ProbeResult reply;
  dbus::MessageReader reader(response.get());
  if (!reader.PopArrayOfBytesAsProto(&reply)) {
    LOG(ERROR) << "Failed to decode runtime_probe protobuf response";
    return false;
  }
  if (reply.error() != runtime_probe::RUNTIME_PROBE_ERROR_NOT_SET) {
    LOG(ERROR) << "runtime_probe returns error code " << reply.error();
    return false;
  }

  components->clear();
  for (auto& [component, probed_component_size] : kProbedComponentSizes) {
    if ((reply.*probed_component_size)() > 0) {
      components->insert(component);
    }
  }
  return true;
}

}  // namespace rmad

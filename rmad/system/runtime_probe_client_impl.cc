// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/runtime_probe_client_impl.h"

#include <memory>
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

#include "rmad/utils/component_utils.h"

namespace {

const std::unordered_map<rmad::RmadComponent,
                         runtime_probe::ProbeRequest::SupportCategory>
    kRmadToRuntimeProbeComponentMap = {
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

template <typename T>
void AppendComponents(rmad::RmadComponent component,
                      const T& arr,
                      int size,
                      rmad::ComponentsWithIdentifier* component_list) {
  for (int i = 0; i < size; ++i) {
    component_list->push_back(
        std::make_pair(component, rmad::GetComponentIdentifier(arr.Get(i))));
  }
}

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
    ComponentsWithIdentifier* component_list) {
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

  std::unique_ptr<dbus::Response> response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
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

  component_list->clear();
  AppendComponents(rmad::RMAD_COMPONENT_BATTERY, reply.battery(),
                   reply.battery_size(), component_list);
  AppendComponents(rmad::RMAD_COMPONENT_STORAGE, reply.storage(),
                   reply.storage_size(), component_list);
  AppendComponents(rmad::RMAD_COMPONENT_CAMERA, reply.camera(),
                   reply.camera_size(), component_list);
  AppendComponents(rmad::RMAD_COMPONENT_STYLUS, reply.stylus(),
                   reply.stylus_size(), component_list);
  AppendComponents(rmad::RMAD_COMPONENT_TOUCHPAD, reply.touchpad(),
                   reply.touchpad_size(), component_list);
  AppendComponents(rmad::RMAD_COMPONENT_TOUCHSCREEN, reply.touchscreen(),
                   reply.touchscreen_size(), component_list);
  AppendComponents(rmad::RMAD_COMPONENT_DRAM, reply.dram(), reply.dram_size(),
                   component_list);
  AppendComponents(rmad::RMAD_COMPONENT_DISPLAY_PANEL, reply.display_panel(),
                   reply.display_panel_size(), component_list);
  AppendComponents(rmad::RMAD_COMPONENT_CELLULAR, reply.cellular(),
                   reply.cellular_size(), component_list);
  AppendComponents(rmad::RMAD_COMPONENT_ETHERNET, reply.ethernet(),
                   reply.ethernet_size(), component_list);
  AppendComponents(rmad::RMAD_COMPONENT_WIRELESS, reply.wireless(),
                   reply.wireless_size(), component_list);

  return true;
}

}  // namespace rmad

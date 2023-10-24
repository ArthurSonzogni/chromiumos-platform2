// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>

#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/wifi/local_device.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
}  // namespace Logging

// Constructor function
LocalDevice::LocalDevice(Manager* manager,
                         IfaceType type,
                         std::optional<std::string> link_name,
                         uint32_t phy_index,
                         const EventCallback& callback)
    : link_name_(link_name),
      enabled_(false),
      manager_(manager),
      iface_type_(type),
      phy_index_(phy_index),
      callback_(std::move(callback)) {
  SLOG(1) << "LocalDevice(): " << link_name_.value_or("(no link_name)")
          << " type: " << iface_type_ << " Phy index: " << phy_index_;
}

LocalDevice::~LocalDevice() {
  SLOG(1) << "~LocalDevice(): " << link_name_.value_or("(no link_name)")
          << " type: " << iface_type_ << " Phy index: " << phy_index_;
}

bool LocalDevice::SetEnabled(bool enable) {
  if (enabled_ == enable)
    return true;

  LOG(INFO) << (enable ? "Enable " : "Disable ")
            << "device: " << link_name_.value_or("(no link_name)");

  if (enable) {
    if (!Start()) {
      LOG(ERROR) << "Failed to start the local device.";
      return false;
    }
  } else {
    Stop();
  }

  enabled_ = enable;
  return true;
}

void LocalDevice::PostDeviceEvent(DeviceEvent event) const {
  SLOG(1) << "Device " << link_name_.value_or("(no link_name)")
          << " posts event: " << event;

  manager_->dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(&LocalDevice::DeviceEventTask,
                                weak_factory_.GetWeakPtr(), event));
}

void LocalDevice::DeviceEventTask(DeviceEvent event) const {
  SLOG(1) << "Device " << link_name_.value_or("(no link_name)")
          << " handles event: " << event;
  callback_.Run(event, this);
}

EventDispatcher* LocalDevice::Dispatcher() const {
  return manager_->dispatcher();
}

SupplicantProcessProxyInterface* LocalDevice::SupplicantProcessProxy() const {
  return manager_->supplicant_manager()->proxy();
}

ControlInterface* LocalDevice::ControlInterface() const {
  return manager_->control_interface();
}

bool LocalDevice::IsServiceUp() const {
  return GetService() != nullptr && GetService()->IsUp();
}

std::ostream& operator<<(std::ostream& stream, LocalDevice::IfaceType type) {
  if (type == LocalDevice::IfaceType::kAP) {
    stream << "ap";
  } else if (type == LocalDevice::IfaceType::kP2PGO) {
    stream << "p2p_go";
  } else if (type == LocalDevice::IfaceType::kP2PClient) {
    stream << "p2p_client";
  } else {
    stream << "unknown";
  }

  return stream;
}

std::ostream& operator<<(std::ostream& stream, LocalDevice::DeviceEvent event) {
  switch (event) {
    case LocalDevice::DeviceEvent::kInterfaceDisabled:
      stream << "InterfaceDisabled";
      return stream;
    case LocalDevice::DeviceEvent::kInterfaceEnabled:
      stream << "InterfaceEnabled";
      return stream;
    case LocalDevice::DeviceEvent::kLinkUp:
      stream << "LinkUp";
      return stream;
    case LocalDevice::DeviceEvent::kLinkDown:
      stream << "LinkDown";
      return stream;
    case LocalDevice::DeviceEvent::kLinkFailure:
      stream << "LinkFailure";
      return stream;
    case LocalDevice::DeviceEvent::kNetworkUp:
      stream << "NetworkUp";
      return stream;
    case LocalDevice::DeviceEvent::kNetworkDown:
      stream << "NetworkDown";
      return stream;
    case LocalDevice::DeviceEvent::kNetworkFailure:
      stream << "NetworkFailure";
      return stream;
    case LocalDevice::DeviceEvent::kPeerConnected:
      stream << "PeerConnected";
      return stream;
    case LocalDevice::DeviceEvent::kPeerDisconnected:
      stream << "PeerDisconnected";
      return stream;
  }

  stream << "unknown";
  return stream;
}

}  // namespace shill

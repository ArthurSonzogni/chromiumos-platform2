// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "patchpanel/qos_service.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/flat_set.h>

#include "patchpanel/datapath.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

QoSService::QoSService(Datapath* datapath) : datapath_(datapath) {
  process_runner_ = std::make_unique<MinijailedProcessRunner>();
}

QoSService::QoSService(Datapath* datapath,
                       std::unique_ptr<MinijailedProcessRunner> process_runner)
    : datapath_(datapath) {
  process_runner_ = std::move(process_runner);
}

QoSService::~QoSService() = default;

namespace {
// TCP protocol used to set protocol field in conntrack command.
constexpr char kProtocolTCP[] = "TCP";

// UDP protocol used to set protocol field in conntrack command.
constexpr char kProtocolUDP[] = "UDP";

}  // namespace

void QoSService::Enable() {
  if (is_enabled_) {
    return;
  }
  is_enabled_ = true;

  datapath_->EnableQoSDetection();
  for (const auto& ifname : interfaces_) {
    datapath_->EnableQoSApplyingDSCP(ifname);
  }
}

void QoSService::Disable() {
  if (!is_enabled_) {
    return;
  }
  is_enabled_ = false;

  for (const auto& ifname : interfaces_) {
    datapath_->DisableQoSApplyingDSCP(ifname);
  }
  datapath_->DisableQoSDetection();
}

void QoSService::OnPhysicalDeviceAdded(const ShillClient::Device& device) {
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  if (!interfaces_.insert(device.ifname).second) {
    LOG(ERROR) << "Failed to start tracking " << device.ifname;
    return;
  }
  if (!is_enabled_) {
    return;
  }
  datapath_->EnableQoSApplyingDSCP(device.ifname);
}

void QoSService::OnPhysicalDeviceRemoved(const ShillClient::Device& device) {
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  if (interfaces_.erase(device.ifname) != 1) {
    LOG(ERROR) << "Failed to stop tracking " << device.ifname;
    return;
  }
  if (!is_enabled_) {
    return;
  }
  datapath_->DisableQoSApplyingDSCP(device.ifname);
}

void QoSService::ProcessSocketConnectionEvent(
    const patchpanel::SocketConnectionEvent& msg) {
  if (!is_enabled_) {
    return;
  }

  const auto src_addr = net_base::IPAddress::CreateFromBytes(msg.saddr());
  if (!src_addr.has_value()) {
    LOG(ERROR) << __func__ << ": failed to convert source IP address.";
    return;
  }

  const auto dst_addr = net_base::IPAddress::CreateFromBytes(msg.daddr());
  if (!dst_addr.has_value()) {
    LOG(ERROR) << __func__ << ": failed to convert destination IP address.";
    return;
  }

  std::string proto;
  if (msg.proto() == patchpanel::SocketConnectionEvent::IpProtocol::
                         SocketConnectionEvent_IpProtocol_TCP) {
    proto = kProtocolTCP;
  } else if (msg.proto() == patchpanel::SocketConnectionEvent::IpProtocol::
                                SocketConnectionEvent_IpProtocol_UDP) {
    proto = kProtocolUDP;
  } else {
    LOG(ERROR) << __func__ << ": invalid protocol: " << msg.proto();
  }

  std::string mark;
  if (msg.category() ==
      patchpanel::SocketConnectionEvent::QosCategory::
          SocketConnectionEvent_QosCategory_REALTIME_INTERACTIVE) {
    mark = QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive);
  } else if (msg.category() ==
             patchpanel::SocketConnectionEvent::QosCategory::
                 SocketConnectionEvent_QosCategory_MULTIMEDIA_CONFERENCING) {
    mark = QoSFwmarkWithMask(QoSCategory::kMultimediaConferencing);
  } else {
    LOG(ERROR) << __func__ << ": invalid QoS category: " << msg.category();
  }

  // TODO(chuweih): Add check to make sure socket connection exists in
  // conntrack table before updating its connmark.
  if (msg.event() == patchpanel::SocketConnectionEvent::SocketEvent::
                         SocketConnectionEvent_SocketEvent_CLOSE) {
    mark = QoSFwmarkWithMask(QoSCategory::kDefault);
  } else if (msg.event() != patchpanel::SocketConnectionEvent::SocketEvent::
                                SocketConnectionEvent_SocketEvent_OPEN) {
    LOG(ERROR) << __func__ << ": invalid socket event: " << msg.event();
  }

  // Update connmark based on QoS category or set to default connmark if socket
  // connection event is CLOSE.
  std::vector<std::string> args = {"-p",      proto,
                                   "-s",      src_addr.value().ToString(),
                                   "-d",      dst_addr.value().ToString(),
                                   "--sport", std::to_string(msg.sport()),
                                   "--dport", std::to_string(msg.dport()),
                                   "-m",      mark};
  process_runner_->conntrack("-U", args);
}

}  // namespace patchpanel

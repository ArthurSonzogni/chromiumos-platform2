// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/new_l2tp_ipsec_driver.h"

#include <string>

#include <chromeos/dbus/service_constants.h>

namespace shill {

namespace {

const char kL2TPIPsecIPsecTimeoutProperty[] = "L2TPIPsec.IPsecTimeout";
const char kL2TPIPsecLeftProtoPortProperty[] = "L2TPIPsec.LeftProtoPort";
const char kL2TPIPsecLengthBitProperty[] = "L2TPIPsec.LengthBit";
const char kL2TPIPsecPFSProperty[] = "L2TPIPsec.PFS";
const char kL2TPIPsecRefusePapProperty[] = "L2TPIPsec.RefusePap";
const char kL2TPIPsecRekeyProperty[] = "L2TPIPsec.Rekey";
const char kL2TPIPsecRequireAuthProperty[] = "L2TPIPsec.RequireAuth";
const char kL2TPIPsecRequireChapProperty[] = "L2TPIPsec.RequireChap";
const char kL2TPIPsecRightProtoPortProperty[] = "L2TPIPsec.RightProtoPort";

}  // namespace

const VPNDriver::Property NewL2TPIPsecDriver::kProperties[] = {
    {kL2TPIPsecClientCertIdProperty, 0},
    {kL2TPIPsecClientCertSlotProperty, 0},
    {kL2TPIPsecPasswordProperty, Property::kCredential | Property::kWriteOnly},
    {kL2TPIPsecPinProperty, Property::kCredential},
    {kL2TPIPsecPskProperty, Property::kCredential | Property::kWriteOnly},
    {kL2TPIPsecUseLoginPasswordProperty, 0},
    {kL2TPIPsecUserProperty, 0},
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
    {kL2TPIPsecCaCertPemProperty, Property::kArray},
    {kL2TPIPsecTunnelGroupProperty, 0},
    {kL2TPIPsecIPsecTimeoutProperty, 0},
    {kL2TPIPsecLeftProtoPortProperty, 0},
    {kL2TPIPsecLengthBitProperty, 0},
    {kL2TPIPsecPFSProperty, 0},
    {kL2TPIPsecRefusePapProperty, 0},
    {kL2TPIPsecRekeyProperty, 0},
    {kL2TPIPsecRequireAuthProperty, 0},
    {kL2TPIPsecRequireChapProperty, 0},
    {kL2TPIPsecRightProtoPortProperty, 0},
    {kL2TPIPsecXauthUserProperty, Property::kCredential | Property::kWriteOnly},
    {kL2TPIPsecXauthPasswordProperty,
     Property::kCredential | Property::kWriteOnly},
    {kL2TPIPsecLcpEchoDisabledProperty, 0},
};

NewL2TPIPsecDriver::NewL2TPIPsecDriver(Manager* manager,
                                       ProcessManager* process_manager)
    : VPNDriver(
          manager, process_manager, kProperties, base::size(kProperties)) {}

NewL2TPIPsecDriver::~NewL2TPIPsecDriver() {}

base::TimeDelta NewL2TPIPsecDriver::ConnectAsync(EventHandler* handler) {
  event_handler_ = handler;

  // TODO(b/165170125): Implement ConnectAsync.
  // TODO(b/165170125): Use the correct timeout value.
  return base::TimeDelta::FromSeconds(0);
}

void NewL2TPIPsecDriver::Disconnect() {
  event_handler_ = nullptr;
  // TODO(b/165170125): Implement Disconnect;
}

IPConfig::Properties NewL2TPIPsecDriver::GetIPProperties() const {
  // TODO(b/165170125): Implement GetIPProperties;
  return IPConfig::Properties();
}

std::string NewL2TPIPsecDriver::GetProviderType() const {
  return kProviderL2tpIpsec;
}

void NewL2TPIPsecDriver::OnConnectTimeout() {
  // TODO(b/165170125): Implement OnConnectTimeout.
}

void NewL2TPIPsecDriver::OnBeforeSuspend(const ResultCallback& callback) {
  // TODO(b/165170125): Check state.
  // ipsec_connection_->Disconnect();
  callback.Run(Error(Error::kSuccess));
}

void NewL2TPIPsecDriver::OnDefaultPhysicalServiceEvent(
    DefaultPhysicalServiceEvent event) {
  // TODO(b/165170125): Check state.
  switch (event) {
    case kDefaultPhysicalServiceUp:
      return;
    case kDefaultPhysicalServiceDown:
      // ipsec_connection_->Disconnect();
      return;
    case kDefaultPhysicalServiceChanged:
      // ipsec_connection_->Disconnect();
      return;
    default:
      NOTREACHED();
  }
}

}  // namespace shill

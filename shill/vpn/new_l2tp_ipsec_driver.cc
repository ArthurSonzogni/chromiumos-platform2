// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/new_l2tp_ipsec_driver.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/error.h"
#include "shill/ipconfig.h"
#include "shill/manager.h"
#include "shill/vpn/ipsec_connection.h"
#include "shill/vpn/l2tp_connection.h"
#include "shill/vpn/vpn_service.h"

namespace shill {

namespace {

const char kL2TPIPsecIPsecTimeoutProperty[] = "L2TPIPsec.IPsecTimeout";
const char kL2TPIPsecLeftProtoPortProperty[] = "L2TPIPsec.LeftProtoPort";
const char kL2TPIPsecLengthBitProperty[] = "L2TPIPsec.LengthBit";
const char kL2TPIPsecRefusePapProperty[] = "L2TPIPsec.RefusePap";
const char kL2TPIPsecRekeyProperty[] = "L2TPIPsec.Rekey";
const char kL2TPIPsecRequireAuthProperty[] = "L2TPIPsec.RequireAuth";
const char kL2TPIPsecRequireChapProperty[] = "L2TPIPsec.RequireChap";
const char kL2TPIPsecRightProtoPortProperty[] = "L2TPIPsec.RightProtoPort";

std::unique_ptr<IPsecConnection::Config> MakeIPsecConfig(
    const KeyValueStore& args) {
  auto config = std::make_unique<IPsecConnection::Config>();

  config->remote = args.Lookup<std::string>(kProviderHostProperty, "");
  if (!args.Lookup<std::string>(kL2TPIPsecPskProperty, "").empty()) {
    config->psk = args.Get<std::string>(kL2TPIPsecPskProperty);
  }
  if (!args.Lookup<Strings>(kL2TPIPsecCaCertPemProperty, Strings{}).empty()) {
    config->ca_cert_pem_strings =
        args.Get<Strings>(kL2TPIPsecCaCertPemProperty);
  }
  if (!args.Lookup<std::string>(kL2TPIPsecClientCertIdProperty, "").empty()) {
    config->client_cert_id =
        args.Get<std::string>(kL2TPIPsecClientCertIdProperty);
  }
  if (!args.Lookup<std::string>(kL2TPIPsecClientCertSlotProperty, "").empty()) {
    config->client_cert_slot =
        args.Get<std::string>(kL2TPIPsecClientCertSlotProperty);
  }
  if (!args.Lookup<std::string>(kL2TPIPsecPinProperty, "").empty()) {
    config->client_cert_pin = args.Get<std::string>(kL2TPIPsecPinProperty);
  }

  // 17 = UDP, 1701 = L2TP.
  config->local_proto_port =
      args.Lookup<std::string>(kL2TPIPsecLeftProtoPortProperty, "17/1701");
  config->remote_proto_port =
      args.Lookup<std::string>(kL2TPIPsecRightProtoPortProperty, "17/1701");

  return config;
}

std::unique_ptr<L2TPConnection::Config> MakeL2TPConfig(
    const KeyValueStore& args) {
  auto config = std::make_unique<L2TPConnection::Config>();

  // TODO(b/165170125): Add fields.

  return config;
}

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

  dispatcher()->PostTask(
      FROM_HERE, base::BindOnce(&NewL2TPIPsecDriver::StartIPsecConnection,
                                weak_factory_.GetWeakPtr()));

  // TODO(165170125): Use a large value for debugging now.
  return base::TimeDelta::FromSeconds(120);
}

void NewL2TPIPsecDriver::StartIPsecConnection() {
  if (ipsec_connection_) {
    LOG(ERROR) << "The previous IPsecConnection is still running.";
    NotifyServiceOfFailure(Service::kFailureInternal);
    return;
  }

  // Callbacks for L2TP will be set and handled in IPsecConnection.
  auto l2tp_connection = std::make_unique<L2TPConnection>(
      MakeL2TPConfig(*const_args()), /*callbacks=*/nullptr, control_interface(),
      manager()->device_info(), manager()->dispatcher(), process_manager());

  auto callbacks = std::make_unique<IPsecConnection::Callbacks>(
      base::BindRepeating(&NewL2TPIPsecDriver::OnIPsecConnected,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&NewL2TPIPsecDriver::OnIPsecFailure,
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(&NewL2TPIPsecDriver::OnIPsecStopped,
                     weak_factory_.GetWeakPtr()));

  ipsec_connection_ = std::make_unique<IPsecConnection>(
      MakeIPsecConfig(*const_args()), std::move(callbacks),
      std::move(l2tp_connection), manager()->dispatcher(), process_manager());

  ipsec_connection_->Connect();
}

void NewL2TPIPsecDriver::Disconnect() {
  event_handler_ = nullptr;
  if (!ipsec_connection_) {
    LOG(ERROR) << "Disconnect() called but IPsecConnection is not running";
    return;
  }
  if (!ipsec_connection_->IsConnectingOrConnected()) {
    LOG(ERROR) << "Disconnect() called but IPsecConnection is in "
               << ipsec_connection_->state() << " state";
    return;
  }
  ipsec_connection_->Disconnect();
}

IPConfig::Properties NewL2TPIPsecDriver::GetIPProperties() const {
  return ip_properties_;
}

std::string NewL2TPIPsecDriver::GetProviderType() const {
  return kProviderL2tpIpsec;
}

void NewL2TPIPsecDriver::OnConnectTimeout() {
  LOG(INFO) << "Connect timeout";
  if (!ipsec_connection_) {
    LOG(ERROR)
        << "OnConnectTimeout() called but IPsecConnection is not running";
    return;
  }
  if (!ipsec_connection_->IsConnectingOrConnected()) {
    LOG(ERROR) << "OnConnectTimeout() called but IPsecConnection is in "
               << ipsec_connection_->state() << " state";
    return;
  }
  ipsec_connection_->Disconnect();
  NotifyServiceOfFailure(Service::kFailureConnect);
}

void NewL2TPIPsecDriver::OnBeforeSuspend(const ResultCallback& callback) {
  if (ipsec_connection_ && ipsec_connection_->IsConnectingOrConnected()) {
    ipsec_connection_->Disconnect();
  }
  callback.Run(Error(Error::kSuccess));
}

void NewL2TPIPsecDriver::OnDefaultPhysicalServiceEvent(
    DefaultPhysicalServiceEvent event) {
  if (!ipsec_connection_ || !ipsec_connection_->IsConnectingOrConnected()) {
    return;
  }
  switch (event) {
    case kDefaultPhysicalServiceUp:
      return;
    case kDefaultPhysicalServiceDown:
      ipsec_connection_->Disconnect();
      return;
    case kDefaultPhysicalServiceChanged:
      ipsec_connection_->Disconnect();
      return;
    default:
      NOTREACHED();
  }
}

void NewL2TPIPsecDriver::NotifyServiceOfFailure(
    Service::ConnectFailure failure) {
  LOG(ERROR) << "Driver failure due to "
             << Service::ConnectFailureToString(failure);
  if (event_handler_) {
    event_handler_->OnDriverFailure(failure, Service::kErrorDetailsNone);
    event_handler_ = nullptr;
  }
}

void NewL2TPIPsecDriver::OnIPsecConnected(
    const std::string& link_name,
    int interface_index,
    const IPConfig::Properties& ip_properties) {
  if (!event_handler_) {
    LOG(ERROR) << "OnIPsecConnected() triggered in illegal service state";
    return;
  }
  ip_properties_ = ip_properties;
  event_handler_->OnDriverConnected(link_name, interface_index);
}

void NewL2TPIPsecDriver::OnIPsecFailure(Service::ConnectFailure failure) {
  NotifyServiceOfFailure(failure);
}

void NewL2TPIPsecDriver::OnIPsecStopped() {
  ipsec_connection_ = nullptr;
}

}  // namespace shill

// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_control.h"

#include <memory>

#include <gmock/gmock.h>

#include "shill/mock_adaptors.h"

using testing::NiceMock;

namespace shill {

MockControl::MockControl() = default;

MockControl::~MockControl() = default;

std::unique_ptr<DeviceAdaptorInterface> MockControl::CreateDeviceAdaptor(
    Device* /*device*/) {
  return std::make_unique<NiceMock<DeviceMockAdaptor>>();
}

std::unique_ptr<IPConfigAdaptorInterface> MockControl::CreateIPConfigAdaptor(
    IPConfig* /*config*/) {
  return std::make_unique<NiceMock<IPConfigMockAdaptor>>();
}

std::unique_ptr<ManagerAdaptorInterface> MockControl::CreateManagerAdaptor(
    Manager* /*manager*/) {
  return std::make_unique<NiceMock<ManagerMockAdaptor>>();
}

std::unique_ptr<ProfileAdaptorInterface> MockControl::CreateProfileAdaptor(
    Profile* /*profile*/) {
  return std::make_unique<ProfileStubAdaptor>();
}

std::unique_ptr<RpcTaskAdaptorInterface> MockControl::CreateRpcTaskAdaptor(
    RpcTask* /*task*/) {
  return std::make_unique<RpcTaskStubAdaptor>();
}

std::unique_ptr<ServiceAdaptorInterface> MockControl::CreateServiceAdaptor(
    Service* /*service*/) {
  return std::make_unique<NiceMock<ServiceMockAdaptor>>();
}

#ifndef DISABLE_VPN
std::unique_ptr<ThirdPartyVpnAdaptorInterface>
MockControl::CreateThirdPartyVpnAdaptor(ThirdPartyVpnDriver* /*driver*/) {
  return std::make_unique<NiceMock<ThirdPartyVpnMockAdaptor>>();
}
#endif

std::unique_ptr<SupplicantProcessProxyInterface>
MockControl::CreateSupplicantProcessProxy(
    const base::RepeatingClosure& appear,
    const base::RepeatingClosure& vanish) {
  supplicant_appear_ = appear;
  supplicant_vanish_ = vanish;
  return std::make_unique<NiceMock<MockSupplicantProcessProxy>>();
}

const base::RepeatingClosure& MockControl::supplicant_appear() const {
  return supplicant_appear_;
}

const base::RepeatingClosure& MockControl::supplicant_vanish() const {
  return supplicant_vanish_;
}

}  // namespace shill

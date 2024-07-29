// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/mock_context.h"

#include <memory>

#include <attestation/proto_bindings/interface.pb.h>
// NOLINTNEXTLINE(build/include_alpha) dbus-proxy-mocks.h needs interface.pb.h
#include <attestation-client-test/attestation/dbus-proxy-mocks.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_monitor.h>
#include <cras/dbus-proxy-mocks.h>
#include <debugd/dbus-proxy-mocks.h>
#include <fwupd/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <power_manager/dbus-proxy-mocks.h>
#include <spaced/proto_bindings/spaced.pb.h>
// NOLINTNEXTLINE(build/include_alpha) dbus-proxy-mocks.h needs spaced.pb.h
#include <spaced/dbus-proxy-mocks.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
// NOLINTNEXTLINE(build/include_alpha) dbus-proxy-mocks.h needs tpm_manager.pb.h
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#include "diagnostics/cros_healthd/service_config.h"
#include "diagnostics/cros_healthd/system/cros_config.h"
#include "diagnostics/cros_healthd/system/fake_bluez_event_hub.h"
#include "diagnostics/cros_healthd/system/fake_floss_event_hub.h"
#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/fake_powerd_adapter.h"
#include "diagnostics/cros_healthd/system/fake_system_config.h"
#include "diagnostics/cros_healthd/system/fake_system_utilities.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/system/mock_bluez_controller.h"
#include "diagnostics/cros_healthd/system/mock_floss_controller.h"
#include "diagnostics/cros_healthd/utils/resource_queue.h"

namespace diagnostics {

MockContext::MockContext() {
  attestation_proxy_ = std::make_unique<
      testing::StrictMock<org::chromium::AttestationProxyMock>>();
  cros_config_ = std::make_unique<CrosConfig>(ServiceConfig{});
  cras_proxy_ = std::make_unique<
      testing::StrictMock<org::chromium::cras::ControlProxyMock>>();
  debugd_proxy_ =
      std::make_unique<testing::StrictMock<org::chromium::debugdProxyMock>>();
  fwupd_proxy_ =
      std::make_unique<testing::StrictMock<org::freedesktop::fwupdProxyMock>>();
  ground_truth_ = std::make_unique<GroundTruth>(this);
  mojo_service_ = std::make_unique<FakeMojoService>();
  power_manager_proxy_ = std::make_unique<
      testing::StrictMock<org::chromium::PowerManagerProxyMock>>();
  powerd_adapter_ = std::make_unique<FakePowerdAdapter>();
  system_config_ = std::make_unique<FakeSystemConfig>();
  system_utils_ = std::make_unique<FakeSystemUtilities>();
  bluez_controller_ = std::make_unique<MockBluezController>();
  bluez_event_hub_ = std::make_unique<FakeBluezEventHub>();
  floss_controller_ = std::make_unique<MockFlossController>();
  floss_event_hub_ = std::make_unique<FakeFlossEventHub>();
  tpm_manager_proxy_ = std::make_unique<
      testing::StrictMock<org::chromium::TpmManagerProxyMock>>();
  udev_ = std::make_unique<brillo::MockUdev>();
  udev_monitor_ = std::make_unique<brillo::MockUdevMonitor>();
  spaced_proxy_ = std::make_unique<org::chromium::SpacedProxyMock>();

  memory_cpu_resource_queue_ = std::make_unique<ResourceQueue>();
}

std::unique_ptr<PciUtil> MockContext::CreatePciUtil() {
  return std::make_unique<FakePciUtil>(fake_pci_util_);
}

org::chromium::AttestationProxyMock* MockContext::mock_attestation_proxy()
    const {
  return static_cast<testing::StrictMock<org::chromium::AttestationProxyMock>*>(
      attestation_proxy_.get());
}

ash::cros_healthd::mojom::Executor* MockContext::executor() {
  return &mock_executor_;
}

org::chromium::debugdProxyMock* MockContext::mock_debugd_proxy() const {
  return static_cast<testing::StrictMock<org::chromium::debugdProxyMock>*>(
      debugd_proxy_.get());
}

org::chromium::PowerManagerProxyMock* MockContext::mock_power_manager_proxy()
    const {
  return static_cast<
      testing::StrictMock<org::chromium::PowerManagerProxyMock>*>(
      power_manager_proxy_.get());
}

org::chromium::cras::ControlProxyMock* MockContext::mock_cras_proxy() const {
  return static_cast<
      testing::StrictMock<org::chromium::cras::ControlProxyMock>*>(
      cras_proxy_.get());
}

org::freedesktop::fwupdProxyMock* MockContext::mock_fwupd_proxy() const {
  return static_cast<testing::StrictMock<org::freedesktop::fwupdProxyMock>*>(
      fwupd_proxy_.get());
}

FakeMojoService* MockContext::fake_mojo_service() const {
  return static_cast<FakeMojoService*>(mojo_service_.get());
}
FakePowerdAdapter* MockContext::fake_powerd_adapter() const {
  return static_cast<FakePowerdAdapter*>(powerd_adapter_.get());
}

FakeSystemConfig* MockContext::fake_system_config() const {
  return static_cast<FakeSystemConfig*>(system_config_.get());
}

FakeSystemUtilities* MockContext::fake_system_utils() const {
  return static_cast<FakeSystemUtilities*>(system_utils_.get());
}

MockBluezController* MockContext::mock_bluez_controller() const {
  return static_cast<MockBluezController*>(bluez_controller_.get());
}

FakeBluezEventHub* MockContext::fake_bluez_event_hub() const {
  return static_cast<FakeBluezEventHub*>(bluez_event_hub_.get());
}

MockFlossController* MockContext::mock_floss_controller() const {
  return static_cast<MockFlossController*>(floss_controller_.get());
}

FakeFlossEventHub* MockContext::fake_floss_event_hub() const {
  return static_cast<FakeFlossEventHub*>(floss_event_hub_.get());
}

MockExecutor* MockContext::mock_executor() {
  return &mock_executor_;
}

org::chromium::TpmManagerProxyMock* MockContext::mock_tpm_manager_proxy()
    const {
  return static_cast<testing::StrictMock<org::chromium::TpmManagerProxyMock>*>(
      tpm_manager_proxy_.get());
}

brillo::MockUdev* MockContext::mock_udev() const {
  return static_cast<brillo::MockUdev*>(udev_.get());
}

brillo::MockUdevMonitor* MockContext::mock_udev_monitor() const {
  return static_cast<brillo::MockUdevMonitor*>(udev_monitor_.get());
}

org::chromium::SpacedProxyMock* MockContext::mock_spaced_proxy() const {
  return static_cast<org::chromium::SpacedProxyMock*>(spaced_proxy_.get());
}

}  // namespace diagnostics

// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_CONTEXT_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_CONTEXT_H_

#include <memory>

#include "diagnostics/cros_healthd/executor/mock_executor.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/fake_pci_util.h"

namespace brillo {
class MockUdev;
class MockUdevMonitor;
}  // namespace brillo

namespace org {
namespace chromium {
class AttestationProxyMock;
class debugdProxyMock;
class PowerManagerProxyMock;
class SpacedProxyMock;
class TpmManagerProxyMock;

namespace cras {
class ControlProxyMock;
}  // namespace cras
}  // namespace chromium

namespace freedesktop {
class fwupdProxyMock;
}  // namespace freedesktop
}  // namespace org

namespace diagnostics {
class FakeBluezEventHub;
class FakeFlossEventHub;
class FakeMojoService;
class FakeMeminfoReader;
class FakePowerdAdapter;
class FakeSystemConfig;
class FakeSystemUtilities;
class MockBluezController;
class MockExecutor;
class MockFlossController;

// A mock context class for testing.
class MockContext final : public Context {
 public:
  MockContext();
  MockContext(const MockContext&) = delete;
  MockContext& operator=(const MockContext&) = delete;
  ~MockContext() override = default;

  std::unique_ptr<PciUtil> CreatePciUtil() override;

  ash::cros_healthd::mojom::Executor* executor() override;

  // Accessors to the fake and mock objects held by MockContext:
  org::chromium::AttestationProxyMock* mock_attestation_proxy() const;
  org::chromium::debugdProxyMock* mock_debugd_proxy() const;
  org::chromium::PowerManagerProxyMock* mock_power_manager_proxy() const;
  org::chromium::cras::ControlProxyMock* mock_cras_proxy() const;
  org::freedesktop::fwupdProxyMock* mock_fwupd_proxy() const;
  FakeMojoService* fake_mojo_service() const;
  FakeMeminfoReader* fake_meminfo_reader() const;
  FakePowerdAdapter* fake_powerd_adapter() const;
  FakeSystemConfig* fake_system_config() const;
  FakeSystemUtilities* fake_system_utils() const;
  MockBluezController* mock_bluez_controller() const;
  FakeBluezEventHub* fake_bluez_event_hub() const;
  MockFlossController* mock_floss_controller() const;
  FakeFlossEventHub* fake_floss_event_hub() const;
  MockExecutor* mock_executor();
  org::chromium::TpmManagerProxyMock* mock_tpm_manager_proxy() const;
  brillo::MockUdev* mock_udev() const;
  brillo::MockUdevMonitor* mock_udev_monitor() const;
  org::chromium::SpacedProxyMock* mock_spaced_proxy() const;

 private:
  // Used to create a fake pci util.
  FakePciUtil fake_pci_util_;
  // Mock executor.
  MockExecutor mock_executor_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_CONTEXT_H_

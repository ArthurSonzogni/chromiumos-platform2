// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/state_factory.h"

#include <memory>

#include <base/logging.h>
#include <oobe_config/metrics/enterprise_rollback_metrics_handler.h>
#include <session_manager/dbus-proxies.h>

#include "update_engine/common/system_state.h"
#include "update_engine/cros/dbus_connection.h"
#include "update_engine/cros/shill_proxy.h"
#include "update_engine/update_manager/fake_shill_provider.h"
#include "update_engine/update_manager/real_config_provider.h"
#include "update_engine/update_manager/real_device_policy_provider.h"
#include "update_engine/update_manager/real_random_provider.h"
#include "update_engine/update_manager/real_shill_provider.h"
#include "update_engine/update_manager/real_state.h"
#include "update_engine/update_manager/real_system_provider.h"
#include "update_engine/update_manager/real_time_provider.h"
#include "update_engine/update_manager/real_updater_provider.h"

using chromeos_update_engine::SystemState;
using std::unique_ptr;

namespace chromeos_update_manager {

State* DefaultStateFactory(
    policy::PolicyProvider* policy_provider,
    org::chromium::KioskAppServiceInterfaceProxyInterface* kiosk_app_proxy) {
  unique_ptr<RealConfigProvider> config_provider(
      new RealConfigProvider(SystemState::Get()->hardware()));
  scoped_refptr<dbus::Bus> bus =
      chromeos_update_engine::DBusConnection::Get()->GetDBus();
  unique_ptr<RealDevicePolicyProvider> device_policy_provider(
      new RealDevicePolicyProvider(
          std::make_unique<org::chromium::SessionManagerInterfaceProxy>(bus),
          policy_provider,
          std::make_unique<oobe_config::EnterpriseRollbackMetricsHandler>()));
  unique_ptr<RealShillProvider> shill_provider(
      new RealShillProvider(new chromeos_update_engine::ShillProxy()));
  unique_ptr<RealRandomProvider> random_provider(new RealRandomProvider());
  unique_ptr<RealSystemProvider> system_provider(
      new RealSystemProvider(kiosk_app_proxy));

  unique_ptr<RealTimeProvider> time_provider(new RealTimeProvider());
  unique_ptr<RealUpdaterProvider> updater_provider(new RealUpdaterProvider());

  if (!(config_provider->Init() && device_policy_provider->Init() &&
        random_provider->Init() && shill_provider->Init() &&
        system_provider->Init() && time_provider->Init() &&
        updater_provider->Init())) {
    LOG(ERROR) << "Error initializing providers";
    return nullptr;
  }

  return new RealState(config_provider.release(),
                       device_policy_provider.release(),
                       random_provider.release(), shill_provider.release(),
                       system_provider.release(), time_provider.release(),
                       updater_provider.release());
}

}  // namespace chromeos_update_manager

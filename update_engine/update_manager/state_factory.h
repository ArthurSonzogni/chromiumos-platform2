// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_STATE_FACTORY_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_STATE_FACTORY_H_

#include "update_engine/update_manager/state.h"

namespace org {
namespace chromium {
class KioskAppServiceInterfaceProxyInterface;
}  // namespace chromium
}  // namespace org

namespace chromeos_update_manager {

// Creates and initializes a new UpdateManager State instance containing real
// providers instantiated using the passed interfaces. The State doesn't take
// ownership of the passed interfaces, which need to remain available during the
// life of this instance.  Returns null if one of the underlying providers fails
// to initialize.
State* DefaultStateFactory(
    policy::PolicyProvider* policy_provider,
    org::chromium::KioskAppServiceInterfaceProxyInterface* kiosk_app_proxy);

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_STATE_FACTORY_H_

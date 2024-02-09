// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_SHILL_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_SHILL_PROVIDER_H_

#include <base/time/time.h>

#include "update_engine/common/connection_utils.h"
#include "update_engine/update_manager/provider.h"
#include "update_engine/update_manager/variable.h"

namespace chromeos_update_manager {

// Provider for networking related information.
class ShillProvider : public Provider {
 public:
  ShillProvider(const ShillProvider&) = delete;
  ShillProvider& operator=(const ShillProvider&) = delete;
  ~ShillProvider() override {}

  // A variable returning whether we currently have network connectivity.
  virtual Variable<bool>* var_is_connected() = 0;

  // A variable returning the current network connection type. Unknown if not
  // connected.
  virtual Variable<chromeos_update_engine::ConnectionType>* var_conn_type() = 0;

  // A variable returning whether currently network is metered.
  virtual Variable<bool>* var_is_metered() = 0;

  // A variable returning the time when network connection last changed.
  // Initialized to current time.
  virtual Variable<base::Time>* var_conn_last_changed() = 0;

 protected:
  ShillProvider() {}
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_SHILL_PROVIDER_H_

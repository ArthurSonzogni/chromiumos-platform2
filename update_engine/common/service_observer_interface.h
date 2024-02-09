// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_SERVICE_OBSERVER_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_SERVICE_OBSERVER_INTERFACE_H_

#include <memory>
#include <string>

#include "update_engine/client_library/include/update_engine/update_status.h"
#include "update_engine/common/error_code.h"

namespace chromeos_update_engine {

class ServiceObserverInterface {
 public:
  virtual ~ServiceObserverInterface() = default;

  // Called whenever the value of these parameters changes. For |progress|
  // value changes, this method will be called only if it changes significantly.
  virtual void SendStatusUpdate(
      const update_engine::UpdateEngineStatus& update_engine_status) = 0;

 protected:
  ServiceObserverInterface() = default;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_SERVICE_OBSERVER_INTERFACE_H_

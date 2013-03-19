// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_PLATFORM_UPDATE_ENGINE_SYSTEM_STATE_H_
#define CHROMEOS_PLATFORM_UPDATE_ENGINE_SYSTEM_STATE_H_

#include "metrics/metrics_library.h"
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include "update_engine/gpio_handler.h"
#include "update_engine/omaha_request_params.h"

namespace chromeos_update_engine {

// SystemState is the root class within the update engine. So we should avoid
// any circular references in header file inclusion. Hence forward-declaring
// the required classes.
class ConnectionManager;
class PrefsInterface;
class PayloadStateInterface;
class GpioHandler;
class UpdateAttempter;

// An interface to global system context, including platform resources,
// the current state of the system, high-level objects whose lifetime is same
// as main, system interfaces, etc.
// Carved out separately so it can be mocked for unit tests.
// Currently it has only one method, but we should start migrating other
// methods to use this as and when needed to unit test them.
// TODO (jaysri): Consider renaming this to something like GlobalContext.
class SystemState {
 public:
  // Destructs this object.
  virtual ~SystemState() {}

  // Returns true if the OOBE process has been completed and EULA accepted.
  // False otherwise.
  virtual bool IsOOBEComplete() = 0;

  // Sets or gets the latest device policy.
  virtual void set_device_policy(const policy::DevicePolicy* device_policy) = 0;
  virtual const policy::DevicePolicy* device_policy() const = 0;

  // Gets the connection manager object.
  virtual ConnectionManager* connection_manager() = 0;

  // Gets the Metrics Library interface for reporting UMA stats.
  virtual MetricsLibraryInterface* metrics_lib() = 0;

  // Gets the interface object for persisted store.
  virtual PrefsInterface* prefs() = 0;

  // Gets the interface for the payload state object.
  virtual PayloadStateInterface* payload_state() = 0;

  // Returns a pointer to the GPIO handler.
  virtual GpioHandler* gpio_handler() const = 0;

  // Returns a pointer to the update attempter object.
  virtual UpdateAttempter* update_attempter() = 0;

  // Returns a pointer to the object that stores the parameters that are
  // common to all Omaha requests.
  virtual OmahaRequestParams* request_params() = 0;
};

}  // namespace chromeos_update_engine

#endif  // CHROMEOS_PLATFORM_UPDATE_ENGINE_SYSTEM_STATE_H_

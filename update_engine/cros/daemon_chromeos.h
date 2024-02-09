// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_DAEMON_CHROMEOS_H_
#define UPDATE_ENGINE_CROS_DAEMON_CHROMEOS_H_

#include <memory>

#include "update_engine/common/daemon_base.h"
#include "update_engine/common/daemon_state_interface.h"
#include "update_engine/common/subprocess.h"
#include "update_engine/cros/dbus_service.h"
#include "update_engine/cros/real_system_state.h"

namespace chromeos_update_engine {

class DaemonChromeOS : public DaemonBase {
 public:
  DaemonChromeOS() = default;
  DaemonChromeOS(const DaemonChromeOS&) = delete;
  DaemonChromeOS& operator=(const DaemonChromeOS&) = delete;

 protected:
  int OnInit() override;

 private:
  // Run from the main loop when the |dbus_adaptor_| object is registered. At
  // this point we can request ownership of the DBus service name and continue
  // initialization.
  void OnDBusRegistered(bool succeeded);

  // The Subprocess singleton class requires a brillo::MessageLoop in the
  // current thread, so we need to initialize it from this class instead of
  // the main() function. This has to be defined before system_state_ because of
  // dependency.
  Subprocess subprocess_;

  // |SystemState| is a global context, but we can't have a static singleton of
  // its object because the style guide does not allow that (it has non-trivial
  // dtor). We need an instance of |SystemState| in this class instead and have
  // a global pointer to it. This is better to be defined as early in this class
  // as possible so it is initialized first and destructed last.
  RealSystemState system_state_;

  // Main D-Bus service adaptor.
  std::unique_ptr<UpdateEngineAdaptor> dbus_adaptor_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_DAEMON_CHROMEOS_H_

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_CLIENT_VTPM_DBUS_PROXY_H_
#define VTPM_CLIENT_VTPM_DBUS_PROXY_H_

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <base/threading/platform_thread.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>

#include "trunks/command_transceiver.h"
#include "vtpm/vtpm_interface.pb.h"
// Requires vtpm/vtpm_interface.pb.h
#include "vtpm/dbus_proxies.h"

namespace vtpm {
// VtpmDBusProxy is a CommandTransceiver implementation that forwards all
// commands to the trunksd D-Bus daemon. See TrunksDBusService for details on
// how the commands are handled once they reach trunksd. A VtpmDBusProxy
// instance must be used in only one thread.
class VtpmDBusProxy : public trunks::CommandTransceiver {
 public:
  VtpmDBusProxy();
  explicit VtpmDBusProxy(scoped_refptr<dbus::Bus> bus);
  ~VtpmDBusProxy() override;

  // Initializes the D-Bus client. Returns true on success.
  bool Init() override;

  // CommandTransceiver methods.
  void SendCommand(const std::string& command,
                   ResponseCallback callback) override;
  std::string SendCommandAndWait(const std::string& command) override;

  // Returns the service readiness flag. Forces re-check for readiness if
  // the flag is not set or |force_check| is passed.
  bool IsServiceReady(bool force_check);

  void set_init_timeout(base::TimeDelta init_timeout) {
    init_timeout_ = init_timeout;
  }
  void set_init_attempt_delay(base::TimeDelta init_attempt_delay) {
    init_attempt_delay_ = init_attempt_delay;
  }
  base::PlatformThreadId origin_thread_id_for_testing() {
    return origin_thread_id_;
  }
  void set_origin_thread_id_for_testing(
      base::PlatformThreadId testing_thread_id) {
    origin_thread_id_ = testing_thread_id;
  }

 private:
  VtpmDBusProxy(const VtpmDBusProxy&) = delete;
  VtpmDBusProxy& operator=(const VtpmDBusProxy&) = delete;

  // Checks service readiness, i.e. that vTPM is registered on dbus.
  bool CheckIfServiceReady();

  // Handles errors received from dbus.
  void OnError(ResponseCallback callback, brillo::Error* error);

  base::WeakPtr<VtpmDBusProxy> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  bool service_ready_ = false;
  // Timeout waiting for trunksd service readiness on dbus when initializing.
  base::TimeDelta init_timeout_ = base::Seconds(30);
  // Delay between subsequent checks if trunksd is ready on dbus.
  base::TimeDelta init_attempt_delay_ = base::Milliseconds(300);

  base::PlatformThreadId origin_thread_id_;
  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::VtpmProxy> vtpm_proxy_;

  // Declared last so weak pointers are invalidated first on destruction.
  base::WeakPtrFactory<VtpmDBusProxy> weak_factory_{this};
};

}  // namespace vtpm

#endif  // VTPM_CLIENT_VTPM_DBUS_PROXY_H_

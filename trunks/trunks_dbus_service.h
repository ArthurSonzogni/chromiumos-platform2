// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TRUNKS_DBUS_SERVICE_H_
#define TRUNKS_TRUNKS_DBUS_SERVICE_H_

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>

#include "trunks/command_transceiver.h"
#include "trunks/power_manager.h"
#include "trunks/resilience/write_error_tracker.h"
#include "trunks/trunks_interface.pb.h"
// Requires trunks/trunks_interface.pb.h
#include "trunks/dbus_adaptors/org.chromium.Trunks.h"

namespace trunks {

using brillo::dbus_utils::DBusMethodResponse;

// Forward declaration
class TrunksDBusService;

class TrunksDBusAdaptor : public org::chromium::TrunksInterface,
                          public org::chromium::TrunksAdaptor {
 public:
  explicit TrunksDBusAdaptor(scoped_refptr<dbus::Bus> bus,
                             CommandTransceiver& command_transceiver,
                             WriteErrorTracker& write_error_tracker,
                             TrunksDBusService& dbus_service);
  TrunksDBusAdaptor(const TrunksDBusAdaptor&) = delete;
  TrunksDBusAdaptor& operator=(const TrunksDBusAdaptor&) = delete;

  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  // org::chromium::TrunksInterface overrides.
  void SendCommand(
      std::unique_ptr<DBusMethodResponse<SendCommandResponse>> response,
      const SendCommandRequest& in_request) override;

  // org::chromium::TrunksInterface overrides.
  void StartEvent(
      std::unique_ptr<DBusMethodResponse<StartEventResponse>> response,
      const StartEventRequest& in_request) override;

  // org::chromium::TrunksInterface overrides.
  void StopEvent(
      std::unique_ptr<DBusMethodResponse<StopEventResponse>> response,
      const StopEventRequest& in_request) override;

 private:
  void SendCommandCallback(
      std::unique_ptr<DBusMethodResponse<SendCommandResponse>> response,
      const std::string& response_from_tpm);

  brillo::dbus_utils::DBusObject dbus_object_;
  CommandTransceiver& command_transceiver_;
  WriteErrorTracker& write_error_tracker_;
  TrunksDBusService& dbus_service_;

  // Declared last so weak pointers are invalidated first on destruction.
  base::WeakPtrFactory<TrunksDBusAdaptor> weak_factory_{this};
};

// TrunksDBusService registers for and handles all incoming D-Bus messages for
// the trunksd system daemon.
class TrunksDBusService : public brillo::DBusServiceDaemon {
 public:
  explicit TrunksDBusService(CommandTransceiver& command_transceiver,
                             WriteErrorTracker& write_error_tracker);
  TrunksDBusService(const TrunksDBusService&) = delete;
  TrunksDBusService& operator=(const TrunksDBusService&) = delete;

  ~TrunksDBusService() override = default;

  // The |power_manager| will be initialized with D-Bus object.
  // This class does not take ownership of |power_manager|.
  void set_power_manager(PowerManager* power_manager) {
    power_manager_ = power_manager;
  }

 protected:
  // Exports D-Bus methods.
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Tears down dependant objects.
  void OnShutdown(int* exit_code) override;

 private:
  base::WeakPtr<TrunksDBusService> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }
  std::unique_ptr<TrunksDBusAdaptor> adaptor_;
  PowerManager* power_manager_ = nullptr;
  // Store the points that needs to pass to and used by adaptor_
  CommandTransceiver& command_transceiver_;
  WriteErrorTracker& write_error_tracker_;

  // Declared last so weak pointers are invalidated first on destruction.
  base::WeakPtrFactory<TrunksDBusService> weak_factory_{this};
};

}  // namespace trunks

#endif  // TRUNKS_TRUNKS_DBUS_SERVICE_H_

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_DBUS_SERVICE_H_
#define RMAD_DBUS_SERVICE_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>

#include "rmad/rmad_interface.h"

namespace rmad {

class DBusService : public brillo::DBusServiceDaemon {
 public:
  explicit DBusService(RmadInterface* rmad_interface);
  // Used to inject a mock bus.
  DBusService(const scoped_refptr<dbus::Bus>& bus,
              RmadInterface* rmad_interface);
  DBusService(const DBusService&) = delete;
  DBusService& operator=(const DBusService&) = delete;

  ~DBusService() override = default;

 protected:
  // brillo::DBusServiceDaemon overrides.
  int OnInit() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  friend class DBusServiceTest;

  using GetStateResponse =
      brillo::dbus_utils::DBusMethodResponse<const GetStateReply&>;
  // Handler for GetCurrentState D-Bus call.
  void HandleGetCurrentState(std::unique_ptr<GetStateResponse> response);

  // Handler for TransitionNextState D-Bus call.
  void HandleTransitionNextState(std::unique_ptr<GetStateResponse> response,
                                 const TransitionNextStateRequest& request);

  // Handler for TransitionPreviousState D-Bus call.
  void HandleTransitionPreviousState(
      std::unique_ptr<GetStateResponse> response);

  // Handler for AbortRma D-Bus call.
  using AbortRmaResponse =
      brillo::dbus_utils::DBusMethodResponse<const AbortRmaReply&>;
  void HandleAbortRma(std::unique_ptr<AbortRmaResponse> response,
                      const AbortRmaRequest& request);

  template <typename ResponseType, typename ReplyProtobufType>
  void ReplyAndQuit(std::shared_ptr<ResponseType> response,
                    const ReplyProtobufType& reply);
  // Schedule an asynchronous D-Bus shutdown and exit the daemon.
  void PostQuitTask();

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  RmadInterface* rmad_interface_;
};

}  // namespace rmad

#endif  // RMAD_DBUS_SERVICE_H_

// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_DBUS_SERVICE_H_
#define RMAD_DBUS_SERVICE_H_

#include <memory>
#include <utility>

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

  template <typename... Types>
  using DBusMethodResponse = brillo::dbus_utils::DBusMethodResponse<Types...>;

  // Template for handling D-Bus methods.
  template <typename RequestProtobufType, typename ReplyProtobufType>
  using HandlerFunction = void (RmadInterface::*)(
      const RequestProtobufType&,
      const base::Callback<void(const ReplyProtobufType&)>&);

  template <
      typename RequestProtobufType,
      typename ReplyProtobufType,
      DBusService::HandlerFunction<RequestProtobufType, ReplyProtobufType> func>
  void HandleMethod(
      std::unique_ptr<DBusMethodResponse<ReplyProtobufType>> response,
      const RequestProtobufType& request) {
    // Convert to shared_ptr so rmad_interface_ can safely copy the callback.
    using SharedResponsePointer =
        std::shared_ptr<DBusMethodResponse<ReplyProtobufType>>;
    (rmad_interface_->*func)(
        request, base::Bind(&DBusService::SendReply<ReplyProtobufType>,
                            base::Unretained(this),
                            SharedResponsePointer(std::move(response))));
  }

  // Template for handling D-Bus methods without request protobuf.
  template <typename ReplyProtobufType>
  using HandlerFunctionEmptyRequest = void (RmadInterface::*)(
      const base::Callback<void(const ReplyProtobufType&)>&);

  template <typename ReplyProtobufType,
            DBusService::HandlerFunctionEmptyRequest<ReplyProtobufType> func>
  void HandleMethod(
      std::unique_ptr<DBusMethodResponse<ReplyProtobufType>> response) {
    // Convert to shared_ptr so rmad_interface_ can safely copy the callback.
    using SharedResponsePointer =
        std::shared_ptr<DBusMethodResponse<ReplyProtobufType>>;
    (rmad_interface_->*func)(base::Bind(
        &DBusService::SendReply<ReplyProtobufType>, base::Unretained(this),
        SharedResponsePointer(std::move(response))));
  }

  // Template for sending out the reply.
  template <typename ReplyProtobufType>
  void SendReply(
      std::shared_ptr<DBusMethodResponse<ReplyProtobufType>> response,
      const ReplyProtobufType& reply) {
    response->Return(reply);

    // Quit the daemon after sending the reply if RMA is not required.
    rmad_interface_->GetCurrentState(
        base::Bind(&DBusService::QuitIfRmaNotRequired, base::Unretained(this)));
  }

  // If RMA is not required, quit the daemon.
  void QuitIfRmaNotRequired(const GetStateReply& reply);

  // Schedule an asynchronous D-Bus shutdown and exit the daemon.
  void PostQuitTask();

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  RmadInterface* rmad_interface_;
};

}  // namespace rmad

#endif  // RMAD_DBUS_SERVICE_H_

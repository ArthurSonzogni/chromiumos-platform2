//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef TPM_MANAGER_SERVER_DBUS_SERVICE_H_
#define TPM_MANAGER_SERVER_DBUS_SERVICE_H_

#include <memory>

#include <base/callback.h>
#include <base/callback_forward.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/dbus_signal.h>
#include <dbus/bus.h>

#include "tpm_manager/common/tpm_nvram_interface.h"
#include "tpm_manager/common/tpm_ownership_interface.h"

namespace tpm_manager {

using brillo::dbus_utils::DBusMethodResponse;
using brillo::dbus_utils::DBusObject;
using brillo::dbus_utils::DBusSignal;

using OwnershipTakenCallBack = base::Closure;

// Handles D-Bus communication with the TpmManager daemon.
class DBusService : public brillo::DBusServiceDaemon {
 public:
  // Does not take ownership of |nvram_service| or |ownership_service|. The
  // services provided must be initialized, and must remain valid for the
  // lifetime of this instance.
  DBusService(TpmNvramInterface* nvram_service,
              TpmOwnershipInterface* ownership_service);
  // Used to inject a mock bus.
  DBusService(scoped_refptr<dbus::Bus> bus,
              TpmNvramInterface* nvram_service,
              TpmOwnershipInterface* ownership_service);
  ~DBusService() override = default;

  // Registers objects exported by this service.
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Callback function being called when ownership is (already) taken in
  // TpmInitializer.
  void NotifyOwnershipIsTaken();

  // Sends ownership taken signal to D-Bus if all the following conditions are
  // true:
  //  1. The signal has never been sent before.
  //  2. The signal is successfully registered in the TPM ownership interface.
  //  3. TPM ownership is already taken.
  //
  // Returns whether the signal was successfully sent.
  bool MaybeSendOwnershipTakenSignal();

 private:
  friend class DBusServiceTest;

  template <typename RequestProtobufType,
            typename ReplyProtobufType,
            typename TpmInterface>
  using HandlerFunction = void (TpmInterface::*)(
      const RequestProtobufType&,
      const base::Callback<void(const ReplyProtobufType&)>&);

  // Templates to handle D-Bus calls.
  template <typename RequestProtobufType,
            typename ReplyProtobufType,
            DBusService::HandlerFunction<RequestProtobufType,
                                         ReplyProtobufType,
                                         TpmNvramInterface> func>
  void HandleNvramDBusMethod(
      std::unique_ptr<DBusMethodResponse<const ReplyProtobufType&>> response,
      const RequestProtobufType& request);

  template <typename RequestProtobufType,
            typename ReplyProtobufType,
            DBusService::HandlerFunction<RequestProtobufType,
                                         ReplyProtobufType,
                                         TpmOwnershipInterface> func>
  void HandleOwnershipDBusMethod(
      std::unique_ptr<DBusMethodResponse<const ReplyProtobufType&>> response,
      const RequestProtobufType& request);

  std::unique_ptr<DBusObject> dbus_object_;
  TpmNvramInterface* nvram_service_;
  TpmOwnershipInterface* ownership_service_;

  bool ownership_already_taken_ = false;
  bool already_sent_ownership_taken_signal_ = false;

  // Pointer of the ownership taken signal. The signal is indirectly owned by
  // dbus_object_.
  std::weak_ptr<DBusSignal<bool>> ownership_taken_signal_;

  DISALLOW_COPY_AND_ASSIGN(DBusService);
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_DBUS_SERVICE_H_

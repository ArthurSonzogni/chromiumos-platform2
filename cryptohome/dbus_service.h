// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DBUS_SERVICE_H_
#define CRYPTOHOME_DBUS_SERVICE_H_

#include <memory>

#include <base/check.h>
#include <brillo/daemons/dbus_daemon.h>

#include "cryptohome/service_userdataauth.h"
#include "cryptohome/system_apis.h"
#include "cryptohome/userdataauth.h"

namespace cryptohome {

class UserDataAuthDaemon : public brillo::DBusServiceDaemon {
 public:
  UserDataAuthDaemon()
      : DBusServiceDaemon(::user_data_auth::kUserDataAuthServiceName),
        service_(system_apis_.ToBackingApis()) {}
  UserDataAuthDaemon(const UserDataAuthDaemon&) = delete;
  UserDataAuthDaemon& operator=(const UserDataAuthDaemon&) = delete;

  // Retrieve the UserDataAuth object, it holds the service's state and provides
  // a good chunk of functionality.
  UserDataAuth* GetUserDataAuth() { return &service_; }

 protected:
  void OnShutdown(int* exit_code) override {
    brillo::DBusServiceDaemon::OnShutdown(exit_code);
  }

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    // Initialize the UserDataAuth service.
    // Note that the initialization should be done after setting the options.
    CHECK(service_.Initialize(nullptr));

    CHECK(!dbus_object_);
    dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
        nullptr, bus_,
        dbus::ObjectPath(::user_data_auth::kUserDataAuthServicePath));

    userdataauth_adaptor_ = std::make_unique<UserDataAuthAdaptor>(
        bus_, dbus_object_.get(), &service_);
    userdataauth_adaptor_->RegisterAsync();

    pkcs11_adaptor_ =
        std::make_unique<Pkcs11Adaptor>(bus_, dbus_object_.get(), &service_);
    pkcs11_adaptor_->RegisterAsync();

    misc_adaptor_ = std::make_unique<CryptohomeMiscAdaptor>(
        bus_, dbus_object_.get(), &service_);
    misc_adaptor_->RegisterAsync();

    dbus_object_->RegisterAsync(
        sequencer->GetHandler("RegisterAsync() for UserDataAuth failed", true));
  }

 private:
  std::unique_ptr<UserDataAuthAdaptor> userdataauth_adaptor_;
  std::unique_ptr<Pkcs11Adaptor> pkcs11_adaptor_;
  std::unique_ptr<CryptohomeMiscAdaptor> misc_adaptor_;

  SystemApis system_apis_;
  UserDataAuth service_;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_DBUS_SERVICE_H_

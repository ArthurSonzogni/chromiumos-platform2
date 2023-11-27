// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_MODEM_DBUS_ADAPTOR_H_
#define MODEMLOGGERD_MODEM_DBUS_ADAPTOR_H_

#include <memory>
#include <string>
#include <vector>

#include "modemloggerd/adaptor_interfaces.h"

namespace modemloggerd {

class Modem;

class ModemDBusAdaptor : public ModemAdaptorInterface,
                         public org::chromium::Modemloggerd::ModemInterface {
 public:
  template <typename... T>
  using DBusResponse = brillo::dbus_utils::DBusMethodResponse<T...>;

  ModemDBusAdaptor(Modem* modem, dbus::Bus* bus);
  ModemDBusAdaptor(const ModemDBusAdaptor&) = delete;
  ModemDBusAdaptor& operator=(const ModemDBusAdaptor&) = delete;

  // org::chromium::Modemloggerd::ModemInterface overrides.

  void SetEnabled(std::unique_ptr<DBusResponse<>> response,
                  const bool in_enable) override;
  void Start(std::unique_ptr<DBusResponse<>> response) override;
  void Stop(std::unique_ptr<DBusResponse<>> response) override;
  void SetOutputDir(std::unique_ptr<DBusResponse<>> response,
                    const std::string& in_output_dir) override;
  // ModemAdaptorInterface override.
  dbus::ObjectPath object_path() const override { return object_path_; }

 private:
  // Id for next created Modem object.
  static uint16_t next_id_;

  Modem* modem_;
  dbus::ObjectPath object_path_;
  brillo::dbus_utils::DBusObject dbus_object_;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_MODEM_DBUS_ADAPTOR_H_

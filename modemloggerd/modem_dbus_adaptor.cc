// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemloggerd/modem_dbus_adaptor.h"

#include <memory>
#include <string>

#include <base/strings/string_number_conversions.h>

#include "modemloggerd/modem.h"

namespace modemloggerd {

namespace {

const char kBasePath[] = "/org/chromium/Modemloggerd/Modem/";

}  // namespace

// static
uint16_t ModemDBusAdaptor::next_id_ = 0;

ModemDBusAdaptor::ModemDBusAdaptor(Modem* modem, dbus::Bus* bus)
    : ModemAdaptorInterface(this),
      modem_(modem),
      object_path_(kBasePath + base::NumberToString(next_id_++)),
      dbus_object_(nullptr, bus, object_path_) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
}

void ModemDBusAdaptor::SetEnabled(std::unique_ptr<DBusResponse<>> response,
                                  const bool in_enable) {
  auto error = modem_->SetEnabled(in_enable);
  if (error) {
    response->ReplyWithError(error.get());
    return;
  }
  response->Return();
}

void ModemDBusAdaptor::Start(std::unique_ptr<DBusResponse<>> response) {
  auto error = modem_->Start();
  if (error) {
    response->ReplyWithError(error.get());
    return;
  }
  response->Return();
}

void ModemDBusAdaptor::Stop(std::unique_ptr<DBusResponse<>> response) {
  auto error = modem_->Stop();
  if (error) {
    response->ReplyWithError(error.get());
    return;
  }
  response->Return();
}

void ModemDBusAdaptor::SetOutputDir(std::unique_ptr<DBusResponse<>> response,
                                    const std::string& in_output_dir) {
  auto error = modem_->SetOutputDir(in_output_dir);
  if (error) {
    response->ReplyWithError(error.get());
    return;
  }
  response->Return();
}

}  // namespace modemloggerd

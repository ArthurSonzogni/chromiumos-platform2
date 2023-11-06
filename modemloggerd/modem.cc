// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemloggerd/modem.h"

#include <csignal>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "modemloggerd/dbus-constants.h"

namespace {
const char kVarLog[] = "/var/log";
}

namespace modemloggerd {

Modem::Modem(dbus::Bus* bus,
             AdaptorFactoryInterface* adaptor_factory,
             HelperEntry logging_helper)
    : output_dir_(kVarLog),
      dbus_adaptor_(adaptor_factory->CreateModemAdaptor(this, bus)),
      logging_helper_(logging_helper) {
  LOG(INFO) << __func__ << logging_helper_.filename();
}

brillo::ErrorPtr Modem::Start() {
  // TODO(pholla): Sandbox if daemon moves into release images
  LOG(INFO) << __func__;
  if (RunHelperProcessWithLogs()) {
    return nullptr;
  }
  return brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain,
                               kErrorOperationFailed, "Failed to start logger");
}

brillo::ErrorPtr Modem::Stop() {
  LOG(INFO) << __func__;
  const int kStopTimeoutSeconds = 2;
  if (process_.Kill(SIGKILL, kStopTimeoutSeconds)) {
    return nullptr;
  }
  return brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain,
                               kErrorOperationFailed, "Failed to stop logger");
}

bool Modem::RunHelperProcessWithLogs() {
  process_.AddArg(logging_helper_.filename());
  for (const std::string& extra_argument : logging_helper_.extra_argument())
    process_.AddArg(extra_argument);

  if (logging_helper_.has_output_dir_argument()) {
    process_.AddArg(logging_helper_.output_dir_argument());
    process_.AddArg(output_dir_);
  }

  base::Time::Exploded time;
  base::Time::Now().LocalExplode(&time);
  const std::string output_log_file = base::StringPrintf(
      "%s/helper_log.%4u%02u%02u-%02u%02u%02u%03u", output_dir_.c_str(),
      time.year, time.month, time.day_of_month, time.hour, time.minute,
      time.second, time.millisecond);
  process_.RedirectOutput(output_log_file);

  return process_.Start();
}

dbus::ObjectPath Modem::GetDBusPath() {
  return dbus_adaptor_->object_path();
}

brillo::ErrorPtr Modem::SetOutputDir(const std::string& output_dir) {
  LOG(INFO) << __func__ << ": " << output_dir;
  output_dir_ = output_dir;
  return nullptr;
}

}  // namespace modemloggerd

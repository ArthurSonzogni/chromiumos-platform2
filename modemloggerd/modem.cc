// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemloggerd/modem.h"

#include <csignal>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "modemloggerd/dbus-constants.h"
#include "modemloggerd/logging_prefs.h"

namespace {
const char kVarLog[] = "/var/log/modemloggerd";
}

namespace modemloggerd {

Modem::Modem(dbus::Bus* bus,
             AdaptorFactoryInterface* adaptor_factory,
             HelperEntry logging_helper)
    : output_dir_(kVarLog),
      dbus_adaptor_(adaptor_factory->CreateModemAdaptor(this, bus)),
      logging_helper_(logging_helper) {
  LOG(INFO) << __func__ << ": " << logging_helper_.exe().filename();
  if (!LoggingPrefs::Get()->GetAutoStart(logging_helper_.modem_name())) {
    return;
  }
  auto start_result = Start();
  if (start_result != nullptr) {
    LOG(ERROR) << "Failed to auto start logger: " << start_result->GetMessage();
  }
}

brillo::ErrorPtr Modem::SetEnabled(bool enable) {
  LOG(INFO) << __func__ << ": " << enable;
  if ((enable && !logging_helper_.has_enable_exe()) ||
      (!enable && !logging_helper_.has_disable_exe())) {
    return nullptr;
  }
  const int exit_code = RunEnableHelper(enable);
  if (exit_code == 0) {
    return nullptr;
  }
  return brillo::Error::Create(
      FROM_HERE, brillo::errors::dbus::kDomain, kErrorOperationFailed,
      base::StringPrintf("Failed to run helper (exit_code=%d)", exit_code));
}

brillo::ErrorPtr Modem::SetAutoStart(bool autostart) {
  LOG(INFO) << __func__ << ": " << autostart;
  if (!LoggingPrefs::Get()->SetAutoStart(logging_helper_.modem_name(),
                                         autostart)) {
    return brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain,
                                 kErrorOperationFailed,
                                 "Failed to set auto start");
  }
  return nullptr;
}

brillo::ErrorPtr Modem::Start() {
  // TODO(pholla): Sandbox if daemon moves into release images
  LOG(INFO) << __func__;
  if (StartLoggingHelper()) {
    return nullptr;
  }
  return brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain,
                               kErrorOperationFailed, "Failed to start logger");
}

brillo::ErrorPtr Modem::Stop() {
  LOG(INFO) << __func__;
  const int kStopTimeoutSeconds = 2;
  if (logger_process_.Kill(SIGKILL, kStopTimeoutSeconds)) {
    return nullptr;
  }
  return brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain,
                               kErrorOperationFailed, "Failed to stop logger");
}

bool Modem::StartLoggingHelper() {
  logger_process_.AddArg(logging_helper_.exe().filename());
  for (const std::string& extra_argument :
       logging_helper_.exe().extra_argument()) {
    logger_process_.AddArg(extra_argument);
  }
  if (logging_helper_.exe().has_output_dir_argument()) {
    logger_process_.AddArg(logging_helper_.exe().output_dir_argument());
    logger_process_.AddArg(output_dir_);
  }
  logger_process_.RedirectOutput(GetLogPath(logging_helper_.exe().filename()));
  return logger_process_.Start();
}

int Modem::RunEnableHelper(bool enable) {
  auto exe =
      enable ? logging_helper_.enable_exe() : logging_helper_.disable_exe();
  brillo::ProcessImpl process;
  process.AddArg(exe.filename());
  for (const std::string& extra_argument : exe.extra_argument()) {
    process.AddArg(extra_argument);
  }
  process.RedirectOutput(GetLogPath(exe.filename()));
  return process.Run();
}

std::string Modem::GetLogPath(const std::string& filename) {
  const std::string log_prefix =
      base::FilePath(filename).BaseName().MaybeAsASCII();
  base::Time::Exploded time;
  base::Time::Now().LocalExplode(&time);
  return base::StringPrintf("%s/%s_log.%4u%02u%02u-%02u%02u%02u%03u",
                            output_dir_.c_str(), log_prefix.c_str(), time.year,
                            time.month, time.day_of_month, time.hour,
                            time.minute, time.second, time.millisecond);
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

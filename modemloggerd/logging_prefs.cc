// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemloggerd/logging_prefs.h"

#include <string>

#include <base/logging.h>

namespace {

constexpr std::string_view kPrefPath = "/var/lib/modemloggerd/prefs";

}  // namespace

namespace modemloggerd {

LoggingPrefs* LoggingPrefs::logging_prefs_ = nullptr;

LoggingPrefs::LoggingPrefs() {
  const base::FilePath pref_file_path(kPrefPath);
  if (!base::PathExists(pref_file_path)) {
    base::File prefs_file(pref_file_path, base::File::FLAG_CREATE_ALWAYS |
                                              base::File::FLAG_WRITE);
    if (!prefs_file.IsValid()) {
      LOG(ERROR) << "Could not open prefs file";
      return;
    }
    if (!brillo::WriteTextProtobuf(prefs_file.GetPlatformFile(), prefs_)) {
      LOG(ERROR) << "Could not write prefs file";
      return;
    }
    return;
  }

  base::File prefs_file(pref_file_path,
                        base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!prefs_file.IsValid()) {
    LOG(ERROR) << "Could not open prefs file";
    return;
  }
  if (!brillo::ReadTextProtobuf(prefs_file.GetPlatformFile(), &prefs_)) {
    LOG(ERROR) << "Could not read prefs file";
    return;
  }
}

bool LoggingPrefs::Write() {
  base::File prefs_file(
      base::FilePath(kPrefPath),
      base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!prefs_file.IsValid()) {
    LOG(ERROR) << "Could not open prefs file";
    return false;
  }
  prefs_file.Seek(base::File::FROM_BEGIN, 0);
  VLOG(2) << prefs_.DebugString();
  return brillo::WriteTextProtobuf(prefs_file.GetPlatformFile(), prefs_);
}

bool LoggingPrefs::GetAutoStart(const std::string& modem_name) {
  for (const auto& modem_log_pref : prefs_.modem_log_pref()) {
    if (modem_log_pref.modem_name() == modem_name) {
      return modem_log_pref.auto_start();
    }
  }
  return false;
}

bool LoggingPrefs::SetAutoStart(const std::string& modem_name,
                                bool auto_start) {
  for (int i = 0; i < prefs_.modem_log_pref_size(); ++i) {
    auto modem_log_pref = prefs_.mutable_modem_log_pref(i);
    if (modem_log_pref->modem_name() == modem_name) {
      modem_log_pref->set_auto_start(auto_start);
      return Write();
    }
  }
  auto modem_log_pref = prefs_.add_modem_log_pref();
  modem_log_pref->set_modem_name(modem_name);
  modem_log_pref->set_auto_start(auto_start);
  return Write();
}

LoggingPrefs* LoggingPrefs::Get() {
  if (!logging_prefs_) {
    logging_prefs_ = new LoggingPrefs();
  }
  return logging_prefs_;
}

}  // namespace modemloggerd

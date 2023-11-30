// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_LOGGING_PREFS_H_
#define MODEMLOGGERD_LOGGING_PREFS_H_

#include "modemloggerd/prefs.pb.h"

#include <string>

#include <base/logging.h>
#include <base/files/file_util.h>
#include <brillo/proto_file_io.h>

namespace modemloggerd {

class LoggingPrefs {
 public:
  static LoggingPrefs* Get();
  bool GetAutoStart(const std::string& modem_name);
  bool SetAutoStart(const std::string& modem_name, bool auto_start);

 private:
  LoggingPrefs();
  bool Write();

  Prefs prefs_;
  static LoggingPrefs* logging_prefs_;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_LOGGING_PREFS_H_

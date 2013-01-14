// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOG_TOOL_H
#define LOG_TOOL_H

#include <string>
#include <map>

#include <base/basictypes.h>
#include <dbus-c++/dbus.h>

namespace debugd {

class LogTool {
 public:
  LogTool();
  ~LogTool();

  typedef std::map<std::string, std::string> LogMap;

  std::string GetLog(const std::string& name, DBus::Error& error); // NOLINT
  LogMap GetAllLogs(DBus::Error& error); // NOLINT
  LogMap GetFeedbackLogs(DBus::Error& error); // NOLINT
  LogMap GetUserLogFiles(DBus::Error& error);  // NOLINT

 private:
  friend class LogToolTest;

  void AnonymizeLogMap(LogMap* log_map);

  DISALLOW_COPY_AND_ASSIGN(LogTool);
};

};  // namespace debugd

#endif  // LOG_TOOL_H

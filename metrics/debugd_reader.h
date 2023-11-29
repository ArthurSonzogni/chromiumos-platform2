// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_DEBUGD_READER_H_
#define METRICS_DEBUGD_READER_H_

#include <memory>
#include <optional>
#include <string>

#include <base/time/time.h>

#include <debugd/dbus-proxies.h>

namespace chromeos_metrics {

// Reads a single log entry from debugd over dbus.
// For a list of available entries please refer to log_entries docs in debugd.
class DebugdReader {
 public:
  DebugdReader(dbus::Bus* bus, std::string log_name);
  DebugdReader(const DebugdReader&) = delete;
  DebugdReader& operator=(const DebugdReader&) = delete;

  // Virtual only because of mock.
  virtual ~DebugdReader();

  // Fetch log from debugd.
  // Returns the data on success and nullopt if the dbus call failed or the
  // response was empty. Note that calling this results in a blocking IPC. The
  // timeout is set to the dbus system default. (DBUS_TIMEOUT_USE_DEFAULT)
  virtual std::optional<std::string> Read();

 private:
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;
  // Debugd log name. Needs to match an entry from debugd log_entries.
  const std::string log_name_;
};

}  // namespace chromeos_metrics

#endif  // METRICS_DEBUGD_READER_H_

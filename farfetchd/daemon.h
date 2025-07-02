// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FARFETCHD_DAEMON_H_
#define FARFETCHD_DAEMON_H_

#include <memory>
#include <string>
#include <vector>

#include <brillo/daemons/dbus_daemon.h>

#include "farfetchd/dbus_adaptors/org.chromium.Farfetchd.h"
#include "farfetchd/prefetch_helper.h"
#include "farfetchd/trace_manager.h"
#include "libstorage/platform/platform.h"

namespace farfetchd {

class DBusAdaptor : public org::chromium::FarfetchdInterface,
                    public org::chromium::FarfetchdAdaptor {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus);
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  ~DBusAdaptor() override = default;

  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  // Caches file at 'path' synchronously using pread().
  bool PreloadFile(const std::string& path) override;

  // Calls for the kernel to cache the file at 'path' asynchronously.
  // Actual caching of the data may be delayed.
  bool PreloadFileAsync(const std::string& path) override;

  // Cache the file at 'path' asynchronously by calling mmap() with
  // the MAP_POPULATE flag.
  bool PreloadFileMmap(const std::string& path) override;

  // Tracing methods
  std::string StartTrace(
      const std::string& app_name,
      const std::vector<std::string>& process_names,
      const std::vector<std::string>& path_allowlist,
      const std::vector<std::string>& path_denylist) override;

  bool StopTrace(const std::string& trace_id) override;
  bool CancelTrace(const std::string& trace_id) override;
  std::string GetTraceStatus(const std::string& trace_id) override;
  std::string GetTracePath(const std::string& trace_id) override;

 private:
  std::unique_ptr<farfetchd::PrefetchHelper> helper_;
  std::unique_ptr<farfetchd::TraceManager> trace_manager_;
  libstorage::Platform platform_;
  brillo::dbus_utils::DBusObject dbus_object_;
};

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon();
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

  ~Daemon() override = default;

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  std::unique_ptr<DBusAdaptor> adaptor_;
};
}  // namespace farfetchd

#endif  // FARFETCHD_DAEMON_H_

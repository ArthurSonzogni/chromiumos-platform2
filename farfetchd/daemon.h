// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FARFETCHD_DAEMON_H_
#define FARFETCHD_DAEMON_H_

#include <memory>
#include <string>

#include <brillo/daemons/dbus_daemon.h>

#include "farfetchd/dbus_adaptors/org.chromium.Farfetchd.h"
#include "farfetchd/prefetch_helper.h"
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

 private:
  std::unique_ptr<farfetchd::PrefetchHelper> helper_;
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

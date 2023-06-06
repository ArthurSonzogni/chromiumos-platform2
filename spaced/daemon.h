// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_DAEMON_H_
#define SPACED_DAEMON_H_

#include <algorithm>
#include <memory>
#include <string>

#include <base/task/task_runner.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/blkdev_utils/lvm.h>
#include <spaced/proto_bindings/spaced.pb.h>

#include "spaced/calculator/stateful_free_space_calculator.h"
#include "spaced/dbus_adaptors/org.chromium.Spaced.h"
#include "spaced/disk_usage.h"

namespace spaced {

class DBusAdaptor : public org::chromium::SpacedInterface,
                    public org::chromium::SpacedAdaptor {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus);
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  ~DBusAdaptor() override = default;

  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  int64_t GetFreeDiskSpace(const std::string& path) override;
  int64_t GetTotalDiskSpace(const std::string& path) override;
  int64_t GetRootDeviceSize() override;

  bool IsQuotaSupported(const std::string& path) override;
  int64_t GetQuotaCurrentSpaceForUid(const std::string& path,
                                     uint32_t uid) override;
  int64_t GetQuotaCurrentSpaceForGid(const std::string& path,
                                     uint32_t gid) override;
  int64_t GetQuotaCurrentSpaceForProjectId(const std::string& path,
                                           uint32_t project_id) override;

  void StatefulDiskSpaceUpdateCallback(const StatefulDiskSpaceUpdate& state);

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
  std::unique_ptr<DiskUsageUtil> disk_usage_util_;

  // Async. task runner. The calculations are offloaded from the D-Bus thread so
  // that slow disk usage calculations do not DoS D-Bus requests into spaced.
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  std::unique_ptr<StatefulFreeSpaceCalculator> stateful_free_space_calculator_;
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

}  // namespace spaced

#endif  // SPACED_DAEMON_H_

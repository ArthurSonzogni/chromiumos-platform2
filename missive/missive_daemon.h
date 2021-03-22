// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_MISSIVE_DAEMON_H_
#define MISSIVE_MISSIVE_DAEMON_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/threading/thread.h>
#include <brillo/daemons/dbus_daemon.h>

#include "missive/proto/interface.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module_interface.h"

#include "dbus_adaptors/org.chromium.Missived.h"

namespace reporting {

class MissiveDaemon : public brillo::DBusServiceDaemon,
                      public org::chromium::MissivedAdaptor,
                      public org::chromium::MissivedInterface {
 public:
  MissiveDaemon();
  MissiveDaemon(const MissiveDaemon&) = delete;
  MissiveDaemon& operator=(const MissiveDaemon&) = delete;
  virtual ~MissiveDaemon();

 private:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Forward org::chromium::MissivedInterface
  void EnqueueRecords(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          reporting::EnqueueRecordResponse>> response,
      const reporting::EnqueueRecordRequest& in_request) override;

  void FlushPriority(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          reporting::FlushPriorityResponse>> response,
      const reporting::FlushPriorityRequest& in_request) override;

  void HandleFlushResponse(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          reporting::FlushPriorityResponse>> response,
      Status status) const;

  void ConfirmRecordUpload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          reporting::ConfirmRecordUploadResponse>> response,
      const reporting::ConfirmRecordUploadRequest& in_request) override;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  scoped_refptr<StorageModuleInterface> storage_module_;
  Scheduler scheduler_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<MissiveDaemon> weak_factory_{this};
};

}  // namespace reporting

#endif  // MISSIVE_MISSIVE_DAEMON_H_

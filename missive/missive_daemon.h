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

#include "missive/dbus/upload_client.h"
#include "missive/proto/interface.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

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
  void OnStorageModuleConfigured(
      StatusOr<scoped_refptr<StorageModuleInterface>> storage_module_result);

  void AsyncStartUpload(
      bool need_encryption_key,
      UploaderInterface::UploaderInterfaceResultCb uploader_result_cb);

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Forward org::chromium::MissivedInterface
  void EnqueueRecord(
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

  void UpdateEncryptionKey(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          reporting::UpdateEncryptionKeyResponse>> response,
      const reporting::UpdateEncryptionKeyRequest& in_request) override;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  std::atomic<bool> daemon_is_ready_{false};

  scoped_refptr<UploadClient> upload_client_;
  scoped_refptr<StorageModuleInterface> storage_module_;
  Scheduler scheduler_;
};

}  // namespace reporting

#endif  // MISSIVE_MISSIVE_DAEMON_H_

// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_MISSIVE_MISSIVE_IMPL_H_
#define MISSIVE_MISSIVE_MISSIVE_IMPL_H_

#include <memory>
#include <string>

#include <base/threading/thread.h>

#include "missive/analytics/registry.h"
#include "missive/dbus/upload_client.h"
#include "missive/missive/missive_args.h"
#include "missive/missive/missive_service.h"
#include "missive/proto/interface.pb.h"
#include "missive/resources/enqueuing_record_tallier.h"
#include "missive/resources/resource_interface.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

class MissiveImpl : public MissiveService {
 public:
  explicit MissiveImpl(std::unique_ptr<MissiveArgs> args);
  MissiveImpl(const MissiveImpl&) = delete;
  MissiveImpl& operator=(const MissiveImpl&) = delete;
  ~MissiveImpl() override;

  void StartUp(base::OnceCallback<void(Status)> cb) override;
  Status ShutDown() override;

  void AsyncStartUpload(
      UploaderInterface::UploadReason reason,
      UploaderInterface::UploaderInterfaceResultCb uploader_result_cb) override;

  void EnqueueRecord(const EnqueueRecordRequest& in_request,
                     std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                         EnqueueRecordResponse>> out_response) override;

  void FlushPriority(const FlushPriorityRequest& in_request,
                     std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                         FlushPriorityResponse>> out_response) override;

  void ConfirmRecordUpload(
      const ConfirmRecordUploadRequest& in_request,
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<ConfirmRecordUploadResponse>>
          out_response) override;

  void UpdateEncryptionKey(
      const UpdateEncryptionKeyRequest& in_request,
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<UpdateEncryptionKeyResponse>>
          out_response) override;

 private:
  void OnStorageModuleConfigured(
      base::OnceCallback<void(Status)> cb,
      StatusOr<scoped_refptr<StorageModuleInterface>> storage_module_result);

  void HandleFlushResponse(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          FlushPriorityResponse>> out_response,
      Status status) const;

  const std::unique_ptr<MissiveArgs> args_;
  scoped_refptr<UploadClient> upload_client_;
  scoped_refptr<StorageModuleInterface> storage_module_;
  scoped_refptr<const ResourceInterface> disk_space_resource_;
  std::unique_ptr<EnqueuingRecordTallier> enqueuing_record_tallier_;
  Scheduler scheduler_;
  analytics::Registry analytics_registry_{};
};

}  // namespace reporting

#endif  // MISSIVE_MISSIVE_MISSIVE_IMPL_H_

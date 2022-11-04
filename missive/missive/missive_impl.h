// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_MISSIVE_MISSIVE_IMPL_H_
#define MISSIVE_MISSIVE_MISSIVE_IMPL_H_

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <base/task/bind_post_task.h>
#include <base/threading/thread.h>
#include <dbus/bus.h>

#include "missive/analytics/registry.h"
#include "missive/dbus/upload_client.h"
#include "missive/missive/missive_args.h"
#include "missive/missive/missive_service.h"
#include "missive/proto/interface.pb.h"
#include "missive/resources/enqueuing_record_tallier.h"
#include "missive/resources/resource_interface.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

class MissiveImpl : public MissiveService {
 public:
  // MissiveImpl constructor features `upload_client_factory` and
  // `create_storage_factory` parameters to allow tests to mock them.
  // Default values provided are intended for production.
  explicit MissiveImpl(
      std::unique_ptr<MissiveArgs> args,
      base::OnceCallback<
          void(scoped_refptr<dbus::Bus> bus,
               base::OnceCallback<void(StatusOr<scoped_refptr<UploadClient>>)>
                   callback)> upload_client_factory =
          base::BindOnce(&UploadClient::Create),
      base::OnceCallback<
          void(MissiveImpl* self,
               StorageOptions storage_options,
               base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
                   callback)> create_storage_factory =
          base::BindOnce(&MissiveImpl::CreateStorage));
  MissiveImpl(const MissiveImpl&) = delete;
  MissiveImpl& operator=(const MissiveImpl&) = delete;
  ~MissiveImpl() override;

  void StartUp(scoped_refptr<dbus::Bus> bus,
               base::OnceCallback<void(Status)> cb) override;

  Status ShutDown() override;

  static void AsyncStartUpload(
      base::WeakPtr<MissiveImpl> missive,
      UploaderInterface::UploadReason reason,
      UploaderInterface::UploaderInterfaceResultCb uploader_result_cb);

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

  base::WeakPtr<MissiveImpl> GetWeakPtr();

 private:
  void CreateStorage(
      StorageOptions storage_options,
      base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
          callback);

  void OnUploadClientCreated(
      base::OnceCallback<void(Status)> cb,
      StatusOr<scoped_refptr<UploadClient>> upload_client_result);

  void OnStorageModuleConfigured(
      base::OnceCallback<void(Status)> cb,
      StatusOr<scoped_refptr<StorageModule>> storage_module_result);

  void AsyncStartUploadInternal(
      UploaderInterface::UploadReason reason,
      UploaderInterface::UploaderInterfaceResultCb uploader_result_cb);

  const std::unique_ptr<MissiveArgs> args_;
  base::OnceCallback<void(
      scoped_refptr<dbus::Bus> bus,
      base::OnceCallback<void(StatusOr<scoped_refptr<UploadClient>>)> callback)>
      upload_client_factory_;
  base::OnceCallback<void(
      MissiveImpl* self,
      StorageOptions storage_options,
      base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
          callback)>
      create_storage_factory_;

  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);

  scoped_refptr<UploadClient> upload_client_
      GUARDED_BY_CONTEXT(sequence_checker_);
  scoped_refptr<StorageModule> storage_module_
      GUARDED_BY_CONTEXT(sequence_checker_);
  scoped_refptr<const ResourceInterface> disk_space_resource_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<EnqueuingRecordTallier> enqueuing_record_tallier_
      GUARDED_BY_CONTEXT(sequence_checker_);

  Scheduler scheduler_;
  analytics::Registry analytics_registry_
      GUARDED_BY_CONTEXT(sequence_checker_){};

  base::WeakPtrFactory<MissiveImpl> weak_ptr_factory_{this};
};

}  // namespace reporting

#endif  // MISSIVE_MISSIVE_MISSIVE_IMPL_H_

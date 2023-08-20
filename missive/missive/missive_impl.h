// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_MISSIVE_MISSIVE_IMPL_H_
#define MISSIVE_MISSIVE_MISSIVE_IMPL_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/functional/callback_forward.h>
#include <base/gtest_prod_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/task/bind_post_task.h>
#include <base/threading/thread.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>

#include "missive/analytics/registry.h"
#include "missive/compression/compression_module.h"
#include "missive/dbus/upload_client.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/encryption/verification.h"
#include "missive/health/health_module.h"
#include "missive/missive/missive_args.h"
#include "missive/missive/missive_service.h"
#include "missive/proto/interface.pb.h"
#include "missive/resources/enqueuing_record_tallier.h"
#include "missive/resources/resource_manager.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

class MissiveImpl : public MissiveService {
 public:
  // `MissiveImpl` constructor features `..._factory` members to allow tests to
  // mock them. Default values provided are intended for production.
  MissiveImpl();
  MissiveImpl(const MissiveImpl&) = delete;
  MissiveImpl& operator=(const MissiveImpl&) = delete;
  ~MissiveImpl() override;

  // Factory setters. If needed, must be called before calling `StartUp`.
  MissiveImpl& SetUploadClientFactory(
      base::OnceCallback<
          void(scoped_refptr<dbus::Bus> bus,
               base::OnceCallback<void(StatusOr<scoped_refptr<UploadClient>>)>
                   callback)> upload_client_factory);
  MissiveImpl& SetCompressionModuleFactory(
      base::OnceCallback<scoped_refptr<CompressionModule>(
          const MissiveArgs::StorageParameters& parameters)>
          compression_module_factory);
  MissiveImpl& SetEncryptionModuleFactory(
      base::OnceCallback<scoped_refptr<EncryptionModuleInterface>(
          const MissiveArgs::StorageParameters& parameters)>
          encryption_module_factory);
  MissiveImpl& SetHealthModuleFactory(
      base::OnceCallback<scoped_refptr<HealthModule>(
          const base::FilePath& file_path)> health_module_factory);
  MissiveImpl& SetStorageModuleFactory(
      base::OnceCallback<
          void(MissiveImpl* self,
               StorageOptions storage_options,
               MissiveArgs::StorageParameters parameters,
               base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
                   callback)> create_storage_factory);

  // Asynchronous StartUp called once `bus` and `feature_lib` are available.
  // Once finished, invokes `cb` passing OK or error status.
  void StartUp(scoped_refptr<dbus::Bus> bus,
               feature::PlatformFeaturesInterface* feature_lib,
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

  void UpdateConfigInMissive(
      const UpdateConfigInMissiveRequest& in_request,
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<UpdateConfigInMissiveResponse>>
          out_response) override;

  void UpdateEncryptionKey(
      const UpdateEncryptionKeyRequest& in_request,
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<UpdateEncryptionKeyResponse>>
          out_response) override;

  base::WeakPtr<MissiveImpl> GetWeakPtr();

 private:
  FRIEND_TEST_ALL_PREFIXES(MissiveImplTest, DisabledReportingTest);

  void CreateStorage(
      StorageOptions storage_options,
      MissiveArgs::StorageParameters parameters,
      base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
          callback);

  static scoped_refptr<HealthModule> CreateHealthModule(
      const base::FilePath& file_path);
  static scoped_refptr<CompressionModule> CreateCompressionModule(
      const MissiveArgs::StorageParameters& parameters);

  static scoped_refptr<EncryptionModuleInterface> CreateEncryptionModule(
      const MissiveArgs::StorageParameters& parameters);

  void OnUploadClientCreated(
      base::OnceCallback<void(Status)> cb,
      StatusOr<scoped_refptr<UploadClient>> upload_client_result);

  void OnCollectionParameters(
      base::OnceCallback<void(Status)> cb,
      StatusOr<MissiveArgs::CollectionParameters> collection_parameters_result);

  void OnStorageParameters(
      base::OnceCallback<void(Status)> cb,
      StorageOptions storage_options,
      StatusOr<MissiveArgs::StorageParameters> storage_parameters_result);

  void OnStorageModuleConfigured(
      base::OnceCallback<void(Status)> cb,
      StatusOr<scoped_refptr<StorageModule>> storage_module_result);

  void AsyncStartUploadInternal(
      UploaderInterface::UploadReason reason,
      UploaderInterface::UploaderInterfaceResultCb uploader_result_cb);

  void CreateUploadJob(
      std::optional<ERPHealthData> health_data,
      UploaderInterface::UploadReason reason,
      UploaderInterface::UploaderInterfaceResultCb uploader_result_cb);

  void HandleUploadResponse(
      StatusOr<UploadEncryptedRecordResponse> upload_response);

  void SetEnabled(bool is_enabled);

  void OnStorageParametersUpdate(
      MissiveArgs::StorageParameters storage_parameters);

  // Component factories called no more than once during `MissiveImpl::StartUp`
  base::OnceCallback<void(
      scoped_refptr<dbus::Bus> bus,
      base::OnceCallback<void(StatusOr<scoped_refptr<UploadClient>>)> callback)>
      upload_client_factory_ = base::BindOnce(&UploadClient::Create);
  base::OnceCallback<scoped_refptr<CompressionModule>(
      const MissiveArgs::StorageParameters& parameters)>
      compression_module_factory_ =
          base::BindOnce(&MissiveImpl::CreateCompressionModule);
  base::OnceCallback<scoped_refptr<EncryptionModuleInterface>(
      const MissiveArgs::StorageParameters& parameters)>
      encryption_module_factory_ =
          base::BindOnce(&MissiveImpl::CreateEncryptionModule);
  base::OnceCallback<scoped_refptr<HealthModule>(
      const base::FilePath& file_path)>
      health_module_factory_ = base::BindOnce(&MissiveImpl::CreateHealthModule);
  base::OnceCallback<void(
      MissiveImpl* self,
      StorageOptions storage_options,
      MissiveArgs::StorageParameters parameters,
      base::OnceCallback<void(StatusOr<scoped_refptr<StorageModule>>)>
          callback)>
      create_storage_factory_ = base::BindOnce(&MissiveImpl::CreateStorage);

  SEQUENCE_CHECKER(sequence_checker_);

  base::FilePath reporting_storage_dir_ GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<SequencedMissiveArgs> args_
      GUARDED_BY_CONTEXT(sequence_checker_);
  scoped_refptr<UploadClient> upload_client_
      GUARDED_BY_CONTEXT(sequence_checker_);
  scoped_refptr<StorageModule> storage_module_
      GUARDED_BY_CONTEXT(sequence_checker_);

  scoped_refptr<HealthModule> health_module_
      GUARDED_BY_CONTEXT(sequence_checker_);

  scoped_refptr<const ResourceManager> disk_space_resource_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<EnqueuingRecordTallier> enqueuing_record_tallier_
      GUARDED_BY_CONTEXT(sequence_checker_);

  Scheduler scheduler_;
  analytics::Registry analytics_registry_
      GUARDED_BY_CONTEXT(sequence_checker_){};

  base::RepeatingCallback<void()> storage_upload_success_cb_;

  // References to `Storage` components for dynamic parameters update.
  // Set up once by `StorageCreate` method.
  scoped_refptr<QueuesContainer> queues_container_
      GUARDED_BY_CONTEXT(sequence_checker_);
  scoped_refptr<CompressionModule> compression_module_
      GUARDED_BY_CONTEXT(sequence_checker_);
  scoped_refptr<EncryptionModuleInterface> encryption_module_
      GUARDED_BY_CONTEXT(sequence_checker_);
  scoped_refptr<SignatureVerificationDevFlag> signature_verification_dev_flag_
      GUARDED_BY_CONTEXT(sequence_checker_);

  bool is_enabled_ GUARDED_BY_CONTEXT(sequence_checker_) = true;

  base::WeakPtrFactory<MissiveImpl> weak_ptr_factory_{this};
};

}  // namespace reporting

#endif  // MISSIVE_MISSIVE_MISSIVE_IMPL_H_

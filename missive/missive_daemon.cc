// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive_daemon.h"

#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <type_traits>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>

#include "missive/analytics/resource_collector_cpu.h"
#include "missive/analytics/resource_collector_memory.h"
#include "missive/analytics/resource_collector_storage.h"
#include "missive/compression/compression_module.h"
#include "missive/dbus/upload_client.h"
#include "missive/encryption/encryption_module.h"
#include "missive/encryption/verification.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/scheduler/enqueue_job.h"
#include "missive/scheduler/scheduler.h"
#include "missive/scheduler/upload_job.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_module.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

namespace {

constexpr char kReportingDirectory[] = "/var/cache/reporting";
constexpr CompressionInformation::CompressionAlgorithm kCompressionType =
    CompressionInformation::COMPRESSION_SNAPPY;
constexpr size_t kCompressionThreshold = 512U;

}  // namespace

MissiveDaemon::MissiveDaemon()
    : brillo::DBusServiceDaemon(::missive::kMissiveServiceName),
      org::chromium::MissivedAdaptor(this),
      upload_client_(UploadClient::Create()),
      enqueuing_record_tallier_{base::Minutes(3)} {
  analytics_registry_.Add(
      "Storage", std::make_unique<analytics::ResourceCollectorStorage>(
                     base::Minutes(10), base::FilePath(kReportingDirectory)));
  analytics_registry_.Add(
      "CPU",
      std::make_unique<analytics::ResourceCollectorCpu>(base::Minutes(10)));
}

MissiveDaemon::~MissiveDaemon() = default;

void MissiveDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      /*object_manager=*/nullptr, bus_,
      org::chromium::MissivedAdaptor::GetObjectPath());
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(
      sequencer->GetHandler(/*descriptive_message=*/"RegisterAsync failed.",
                            /*failure_is_fatal=*/true));

  base::FilePath reporting_path(kReportingDirectory);
  StorageOptions storage_options;
  storage_options.set_directory(reporting_path)
      .set_signature_verification_public_key(
          SignatureVerifier::VerificationKey());
  auto memory_resource = storage_options.memory_resource();
  disk_space_resource_ = storage_options.disk_space_resource();
  StorageModule::Create(
      std::move(storage_options),
      base::BindRepeating(&MissiveDaemon::AsyncStartUpload,
                          base::Unretained(this)),
      EncryptionModule::Create(),
      CompressionModule::Create(kCompressionThreshold, kCompressionType),
      base::BindOnce(&MissiveDaemon::OnStorageModuleConfigured,
                     base::Unretained(this)));

  analytics_registry_.Add("Memory",
                          std::make_unique<analytics::ResourceCollectorMemory>(
                              base::Minutes(10), std::move(memory_resource)));
}

void MissiveDaemon::OnStorageModuleConfigured(
    StatusOr<scoped_refptr<StorageModuleInterface>> storage_module_result) {
  if (!storage_module_result.ok()) {
    LOG(ERROR) << "Unable to start Missive daemon status: "
               << storage_module_result.status();
    return;
  }
  storage_module_ = std::move(storage_module_result.ValueOrDie());
  daemon_is_ready_ = true;
}

void MissiveDaemon::AsyncStartUpload(
    UploaderInterface::UploadReason reason,
    UploaderInterface::UploaderInterfaceResultCb uploader_result_cb) {
  DCHECK(uploader_result_cb);
  DCHECK(storage_module_);
  auto upload_job_result = UploadJob::Create(
      upload_client_,
      /*need_encryption_key=*/
      (EncryptionModuleInterface::is_enabled() &&
       reason == UploaderInterface::UploadReason::KEY_DELIVERY),
      /*remaining_storage_capacity=*/disk_space_resource_->GetTotal() -
          disk_space_resource_->GetUsed(),
      /*new_events_rate=*/enqueuing_record_tallier_.GetAverage(),
      std::move(uploader_result_cb));
  if (!upload_job_result.ok()) {
    // In the event that UploadJob::Create fails, it will call
    // |uploader_result_cb| with a failure status.
    LOG(ERROR) << "Was unable to create UploadJob, status:"
               << upload_job_result.status();
    return;
  }
  scheduler_.EnqueueJob(std::move(upload_job_result.ValueOrDie()));
}

void MissiveDaemon::EnqueueRecord(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::EnqueueRecordResponse>> response,
    const reporting::EnqueueRecordRequest& in_request) {
  scoped_refptr<base::SequencedTaskRunner> task_runner =
      base::SequencedTaskRunnerHandle::Get();
  if (!daemon_is_ready_) {
    reporting::EnqueueRecordResponse response_body;
    auto* status = response_body.mutable_status();
    status->set_code(error::UNAVAILABLE);
    status->set_error_message("The daemon is still starting.");
    response->Return(response_body);
    return;
  }
  if (!in_request.has_record()) {
    reporting::EnqueueRecordResponse response_body;
    auto* status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no Record");
    response->Return(response_body);
    return;
  }
  if (!in_request.has_priority()) {
    reporting::EnqueueRecordResponse response_body;
    auto* status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no Priority");
    response->Return(response_body);
    return;
  }

  // Tally the enqueuing record
  if (in_request.has_record()) {
    enqueuing_record_tallier_.Tally(in_request.record());
  }

  scheduler_.EnqueueJob(
      EnqueueJob::Create(storage_module_, in_request,
                         std::make_unique<EnqueueJob::EnqueueResponseDelegate>(
                             std::move(response))));
}

void MissiveDaemon::FlushPriority(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::FlushPriorityResponse>> response,
    const reporting::FlushPriorityRequest& in_request) {
  if (!daemon_is_ready_) {
    reporting::FlushPriorityResponse response_body;
    auto* status = response_body.mutable_status();
    status->set_code(error::UNAVAILABLE);
    status->set_error_message("The daemon is still starting.");
    response->Return(response_body);
    return;
  }
  storage_module_->Flush(
      in_request.priority(),
      base::BindOnce(&MissiveDaemon::HandleFlushResponse,
                     base::Unretained(this), std::move(response)));
}

void MissiveDaemon::HandleFlushResponse(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::FlushPriorityResponse>> response,
    Status status) const {
  reporting::FlushPriorityResponse response_body;
  status.SaveTo(response_body.mutable_status());
  response->Return(response_body);
}

void MissiveDaemon::ConfirmRecordUpload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::ConfirmRecordUploadResponse>> response,
    const reporting::ConfirmRecordUploadRequest& in_request) {
  ConfirmRecordUploadResponse response_body;
  if (!daemon_is_ready_) {
    auto* status = response_body.mutable_status();
    status->set_code(error::UNAVAILABLE);
    status->set_error_message("The daemon is still starting.");
    response->Return(response_body);
    return;
  }
  if (!in_request.has_sequence_information()) {
    auto* status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no SequenceInformation");
    response->Return(response_body);
    return;
  }

  storage_module_->ReportSuccess(in_request.sequence_information(),
                                 in_request.force_confirm());

  response->Return(response_body);
}

void MissiveDaemon::UpdateEncryptionKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::UpdateEncryptionKeyResponse>> response,
    const reporting::UpdateEncryptionKeyRequest& in_request) {
  reporting::UpdateEncryptionKeyResponse response_body;
  if (!daemon_is_ready_) {
    auto* status = response_body.mutable_status();
    status->set_code(error::UNAVAILABLE);
    status->set_error_message("The daemon is still starting.");
    response->Return(response_body);
    return;
  }
  if (!in_request.has_signed_encryption_info()) {
    auto status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no SignedEncryptionInfo");
    response->Return(response_body);
    return;
  }

  storage_module_->UpdateEncryptionKey(in_request.signed_encryption_info());
  response->Return(response_body);
}

}  // namespace reporting

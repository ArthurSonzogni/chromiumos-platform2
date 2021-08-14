// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive_daemon.h"

#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <type_traits>
#include <utility>
#include <vector>

#include <chromeos/dbus/service_constants.h>

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

constexpr const char kReportingDirectory[] = "/var/cache/reporting";
const CompressionInformation::CompressionAlgorithm kCompressionType =
    CompressionInformation::COMPRESSION_SNAPPY;
constexpr const size_t kCompressionThreshold = 512;

}  // namespace

MissiveDaemon::MissiveDaemon()
    : brillo::DBusServiceDaemon(::missive::kMissiveServiceName),
      org::chromium::MissivedAdaptor(this),
      upload_client_(UploadClient::Create()) {}

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
  StorageModule::Create(
      StorageOptions()
          .set_directory(reporting_path)
          .set_signature_verification_public_key(
              SignatureVerifier::VerificationKey()),
      base::BindRepeating(&MissiveDaemon::AsyncStartUpload,
                          base::Unretained(this)),
      EncryptionModule::Create(),
      CompressionModule::Create(kCompressionThreshold, kCompressionType),
      base::BindOnce(&MissiveDaemon::OnStorageModuleConfigured,
                     base::Unretained(this)));
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
  auto upload_job_result = UploadJob::Create(
      upload_client_,
      /*need_encryption_key=*/reason == UploaderInterface::KEY_DELIVERY,
      std::move(uploader_result_cb));
  if (!upload_job_result.ok()) {
    // In the event that UploadJob::Create fails, it will call
    // |uploader_result_cb| with a failure status.
    LOG(ERROR) << "UploadJob was unable to create status:"
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
  if (!in_request.has_sequencing_information()) {
    auto* status = response_body.mutable_status();
    status->set_code(error::INVALID_ARGUMENT);
    status->set_error_message("Request had no SequencingInformation");
    response->Return(response_body);
    return;
  }

  storage_module_->ReportSuccess(in_request.sequencing_information(),
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

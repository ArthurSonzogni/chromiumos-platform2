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

#include "missive/proto/interface.pb.h"
#include "missive/scheduler/enqueue_job.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module_interface.h"

namespace reporting {

namespace {

// In order to get things compiled while we wait for StorageModule to be
// available.
class FakeStorageModule : public StorageModuleInterface {
 public:
  static scoped_refptr<FakeStorageModule> Create() {
    return base::WrapRefCounted(new FakeStorageModule);
  }

  void AddRecord(Priority priority,
                 Record record,
                 base::OnceCallback<void(Status)> callback) override {
    std::move(callback).Run(Status::StatusOK());
  }

  void Flush(Priority priority,
             base::OnceCallback<void(Status)> callback) override {
    std::move(callback).Run(Status::StatusOK());
  }

  void ReportSuccess(SequencingInformation sequencing_information,
                     bool force) override {}

  void UpdateEncryptionKey(
      SignedEncryptionInfo signed_encryption_key) override {}

 private:
  FakeStorageModule() = default;
};

}  // namespace

MissiveDaemon::MissiveDaemon()
    : brillo::DBusServiceDaemon(::missive::kMissiveServiceName),
      org::chromium::MissivedAdaptor(this),
      storage_module_(FakeStorageModule::Create()) {}

MissiveDaemon::~MissiveDaemon() = default;

void MissiveDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      /*object_manager=*/nullptr, bus_,
      org::chromium::MissivedAdaptor::GetObjectPath());
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync failed." /* descriptive_message */,
                            true /* failure_is_fatal */));
}

void MissiveDaemon::EnqueueRecords(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::EnqueueRecordResponse>> response,
    const reporting::EnqueueRecordRequest& in_request) {
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

  scheduler_.EnqueueJob(std::make_unique<EnqueueJob>(
      storage_module_, in_request,
      std::make_unique<EnqueueJob::EnqueueResponseDelegate>(
          std::move(response))));
}

void MissiveDaemon::FlushPriority(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::FlushPriorityResponse>> response,
    const reporting::FlushPriorityRequest& in_request) {
  storage_module_->Flush(
      in_request.priority(),
      base::BindOnce(&MissiveDaemon::HandleFlushResponse,
                     weak_factory_.GetWeakPtr(), std::move(response)));
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

}  // namespace reporting

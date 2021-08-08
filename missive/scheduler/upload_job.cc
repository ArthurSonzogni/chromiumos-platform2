// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/upload_job.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/bind_post_task.h>
#include <base/callback.h>
#include <base/callback_helpers.h>
#include <base/memory/ptr_util.h>
#include <base/sequenced_task_runner.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/threading/sequenced_task_runner_handle.h>

#include "missive/dbus/upload_client.h"
#include "missive/proto/record.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {
namespace {

// This is a fuzzy max, some functions may go over it but most requests should
// be limited to kMaxUploadSize.
const constexpr size_t kMaxUploadSize = 10 * 1024 * 1024;  // 10MiB

}  // namespace

UploadJob::UploadDelegate::UploadDelegate(
    scoped_refptr<UploadClient> upload_client, bool need_encryption_key)
    : upload_client_(upload_client),
      need_encryption_key_(need_encryption_key) {}

UploadJob::UploadDelegate::~UploadDelegate() = default;
UploadJob::SetRecordsCb UploadJob::UploadDelegate::GetSetRecordsCb() {
  return base::BindOnce(&UploadDelegate::SetRecords, base::Unretained(this));
}

Status UploadJob::UploadDelegate::Complete() {
  upload_client_->SendEncryptedRecords(
      std::move(records_), need_encryption_key_,
      // For now the response doesn't contain anything interesting, so we don't
      // handle it. In the future this could change. If it does, UploadClient
      // should be updated to use CallMethodAndBlock rather than CallMethod.
      base::DoNothing());
  return Status::StatusOK();
}

Status UploadJob::UploadDelegate::Cancel(Status status) {
  // UploadJob has nothing to do in the event of cancellation.
  return Status::StatusOK();
}

void UploadJob::UploadDelegate::SetRecords(Records records) {
  records_ = std::move(records);
}

UploadJob::RecordProcessor::RecordProcessor(DoneCb done_cb)
    : done_cb_(std::move(done_cb)),
      records_(std::make_unique<std::vector<EncryptedRecord>>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  DCHECK(done_cb_);
}

UploadJob::RecordProcessor::~RecordProcessor() = default;

void UploadJob::RecordProcessor::ProcessRecord(
    EncryptedRecord record, base::OnceCallback<void(bool)> processed_cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);  // Guaranteed by storage
  size_t record_size = record.ByteSizeLong();
  // We have to allow a single record through even if it is too large.
  // Otherwise the whole system will backup.
  if (current_size_ != 0 && record_size + current_size_ > kMaxUploadSize) {
    std::move(processed_cb).Run(false);
    return;
  }
  records_->push_back(std::move(record));
  current_size_ += record_size;
  std::move(processed_cb).Run(current_size_ < kMaxUploadSize);
}

void UploadJob::RecordProcessor::ProcessGap(
    SequencingInformation start,
    uint64_t count,
    base::OnceCallback<void(bool)> processed_cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);  // Guaranteed by storage
  // We'll process the whole gap request, even if it goes over our max.
  for (uint64_t i = 0; i < count; ++i) {
    records_->emplace_back();
    *records_->rbegin()->mutable_sequencing_information() = start;
    start.set_sequencing_id(start.sequencing_id() + 1);
    current_size_ += records_->rbegin()->ByteSizeLong();
  }
  std::move(processed_cb).Run(current_size_ < kMaxUploadSize);
}

void UploadJob::RecordProcessor::Completed(Status final_status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);  // Guaranteed by storage
  DCHECK(done_cb_);
  if (!final_status.ok()) {
    // Destroy the records to regain system memory now.
    records_.reset();
    std::move(done_cb_).Run(final_status);
    return;
  }
  std::move(done_cb_).Run(std::move(records_));
}

// static
StatusOr<Scheduler::Job::SmartPtr<UploadJob>> UploadJob::Create(
    scoped_refptr<UploadClient> upload_client,
    bool need_encryption_key,
    UploaderInterface::UploaderInterfaceResultCb start_cb) {
  if (upload_client == nullptr) {
    Status status(error::INVALID_ARGUMENT,
                  "Unable to create UploadJob, invalid upload_client");
    std::move(start_cb).Run(status);
    return status;
  }

  auto upload_delegate =
      std::make_unique<UploadDelegate>(upload_client, need_encryption_key);
  SetRecordsCb set_records_callback = upload_delegate->GetSetRecordsCb();

  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::BEST_EFFORT, base::MayBlock()});
  return std::unique_ptr<UploadJob, base::OnTaskRunnerDeleter>(
      new UploadJob(std::move(upload_delegate), sequenced_task_runner,
                    std::move(set_records_callback), std::move(start_cb)),
      base::OnTaskRunnerDeleter(sequenced_task_runner));
}

void UploadJob::StartImpl() {
  DCHECK(base::SequencedTaskRunnerHandle::IsSet());
  std::move(start_cb_).Run(std::make_unique<RecordProcessor>(base::BindPostTask(
      sequenced_task_runner(),
      base::BindOnce(&UploadJob::Done, weak_ptr_factory_.GetWeakPtr()))));
}

void UploadJob::Done(StatusOr<Records> records_result) {
  CheckValidSequence();
  if (!records_result.ok()) {
    Finish(records_result.status());
    return;
  }
  std::move(set_records_cb_).Run(std::move(records_result.ValueOrDie()));
  Finish(Status::StatusOK());
}

UploadJob::UploadJob(
    std::unique_ptr<UploadDelegate> upload_delegate,
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
    SetRecordsCb set_records_cb,
    UploaderInterface::UploaderInterfaceResultCb start_cb)
    : Job(std::move(upload_delegate), sequenced_task_runner),
      set_records_cb_(std::move(set_records_cb)),
      start_cb_(std::move(start_cb)) {}

}  // namespace reporting

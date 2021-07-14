// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_SCHEDULER_UPLOAD_JOB_H_
#define MISSIVE_SCHEDULER_UPLOAD_JOB_H_

#include <memory>
#include <vector>

#include <base/callback.h>
#include <base/sequence_checker.h>
#include <base/sequenced_task_runner.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>

#include "missive/dbus/upload_client.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

class UploadJob : public Scheduler::Job {
 public:
  using Records = std::unique_ptr<std::vector<EncryptedRecord>>;
  using SetRecordsCb = base::OnceCallback<void(Records)>;
  using DoneCb = base::OnceCallback<void(StatusOr<Records>)>;

  class UploadDelegate : public Job::JobDelegate {
   public:
    UploadDelegate(scoped_refptr<UploadClient> upload_client,
                   bool need_encryption_key);
    UploadDelegate(const UploadDelegate& other) = delete;
    UploadDelegate& operator=(const UploadDelegate& other) = delete;
    ~UploadDelegate() override;

    SetRecordsCb GetSetRecordsCb();

   private:
    friend Scheduler::Job;

    Status Complete() override;
    Status Cancel(Status status) override;

    void SetRecords(Records records);

    const scoped_refptr<UploadClient> upload_client_;
    const bool need_encryption_key_;
    Records records_;
  };

  class RecordProcessor : public UploaderInterface {
   public:
    RecordProcessor(
        scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
        DoneCb done_cb,
        const base::WeakPtr<UploadJob>& job);
    RecordProcessor(const RecordProcessor& other) = delete;
    RecordProcessor& operator=(const RecordProcessor& other) = delete;
    ~RecordProcessor() override;

    void ProcessRecord(EncryptedRecord record,
                       base::OnceCallback<void(bool)> processed_cb) override;

    void ProcessGap(SequencingInformation start,
                    uint64_t count,
                    base::OnceCallback<void(bool)> processed_cb) override;

    void Completed(Status final_status) override;

   private:
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
    DoneCb done_cb_;

    Records records_;

    size_t current_size_{0};

    SEQUENCE_CHECKER(sequence_checker_);
    const base::WeakPtr<UploadJob> job_;
  };

  ~UploadJob() override;

  static StatusOr<std::unique_ptr<UploadJob>> Create(
      scoped_refptr<UploadClient> upload_client,
      bool need_encryption_key,
      UploaderInterface::UploaderInterfaceResultCb start_cb);

 protected:
  void StartImpl() override;
  void Done(StatusOr<Records> record_result);

 private:
  UploadJob(std::unique_ptr<UploadDelegate> upload_delegate,
            SetRecordsCb set_records_cb_,
            UploaderInterface::UploaderInterfaceResultCb start_cb);

  SetRecordsCb set_records_cb_;
  UploaderInterface::UploaderInterfaceResultCb start_cb_;

  std::unique_ptr<UploadDelegate> upload_delegate_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<UploadJob> weak_ptr_factory_{this};
};

}  // namespace reporting

#endif  // MISSIVE_SCHEDULER_UPLOAD_JOB_H_

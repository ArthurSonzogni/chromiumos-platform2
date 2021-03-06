// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/enqueue_job.h"

#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>

#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>

#include "missive/proto/interface.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/util/status.h"

namespace reporting {
namespace {

class ManagedFileDescriptor {
 public:
  static StatusOr<std::unique_ptr<ManagedFileDescriptor>> Create(
      const std::string& path) {
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
      return Status(error::INTERNAL, "Unable to open record file for reading.");
    }
    return base::WrapUnique(new ManagedFileDescriptor(fd));
  }

  ~ManagedFileDescriptor() {
    if (IsFileOpen()) {
      if (close(fd_) == -1) {
        LOG(ERROR) << "Unable to close file";
      }
    }
  }

  bool IsFileOpen() const { return fd_ >= 0; }

  int fd() const { return fd_; }

 private:
  explicit ManagedFileDescriptor(int fd) : fd_(fd) {}

  const int fd_;
};

}  // namespace

EnqueueJob::EnqueueResponseDelegate::EnqueueResponseDelegate(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<EnqueueRecordResponse>> response)
    : response_(std::move(response)) {
  DCHECK(response_);
}

Status EnqueueJob::EnqueueResponseDelegate::Complete() {
  return SendResponse(Status::StatusOK());
}

Status EnqueueJob::EnqueueResponseDelegate::Cancel(Status status) {
  return SendResponse(status);
}

Status EnqueueJob::EnqueueResponseDelegate::SendResponse(Status status) {
  if (response_->IsResponseSent()) {
    return Status(error::INTERNAL, "Response has already been sent");
  }
  EnqueueRecordResponse response_body;
  status.SaveTo(response_body.mutable_status());
  response_->Return(response_body);
  return Status::StatusOK();
}

EnqueueJob::EnqueueJob(scoped_refptr<StorageModuleInterface> storage_module,
                       EnqueueRecordRequest request,
                       std::unique_ptr<EnqueueResponseDelegate> delegate)
    : Job(std::move(delegate)),
      storage_module_(storage_module),
      request_(std::move(request)) {}

void EnqueueJob::StartImpl() {
  auto fd_result = ManagedFileDescriptor::Create(
      base::StrCat({"/proc/", base::NumberToString(request_.pid()), "/fd/",
                    base::NumberToString(request_.record_fd())}));
  if (!fd_result.ok()) {
    Finish(fd_result.status());
    return;
  }

  Record record;
  if (!record.ParseFromFileDescriptor(fd_result.ValueOrDie()->fd())) {
    Finish(Status(error::INVALID_ARGUMENT,
                  "Unable to parse record from provided file descriptor."));
    return;
  }

  storage_module_->AddRecord(
      request_.priority(), std::move(record),
      base::BindOnce(&EnqueueJob::Finish, base::Unretained(this)));
}

}  // namespace reporting

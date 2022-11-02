// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/message_sender.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "missive/client/report_queue.h"
#include "missive/client/report_queue_configuration.h"
#include "missive/client/report_queue_factory.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "missive/util/status.h"
#include "re2/re2.h"

namespace secagentd {

namespace {

void EnqueueCallback(reporting::Destination destination,
                     ::reporting::Status status) {
  if (!status.ok()) {
    LOG(ERROR) << destination << ", status=" << status;
  }
}

}  // namespace

namespace pb = cros_xdr::reporting;

MessageSender::MessageSender(const base::FilePath& root_path)
    : root_path_(root_path) {}

MessageSender::MessageSender() : MessageSender(base::FilePath("/")) {}

void MessageSender::UpdateDeviceTz(const base::FilePath& timezone_symlink,
                                   bool error) {
  static constexpr char kTimezoneFilesDir[] = "usr/share/zoneinfo/";
  if (error) {
    LOG(ERROR) << "TZ symlink watch was aborted due to a system error.";
    return;
  }
  const base::FilePath zoneinfo_dir = root_path_.Append(kTimezoneFilesDir);
  base::FilePath timezone_file;
  if (!base::ReadSymbolicLink(timezone_symlink, &timezone_file)) {
    LOG(ERROR) << "Failed to resolve symlink at " << timezone_symlink.value();
    return;
  }
  base::FilePath relpath;
  if (!zoneinfo_dir.AppendRelativePath(timezone_file, &relpath)) {
    LOG(ERROR) << "Failed to find relative zoneinfo path of "
               << timezone_file.value();
    return;
  }
  base::AutoLock lock(common_lock_);
  common_.set_local_timezone(relpath.value());
  LOG(INFO) << "Device timezone set to " << relpath.value();
}

void MessageSender::InitializeDeviceBtime() {
  static constexpr char kProcStatFile[] = "proc/stat";
  static constexpr char kProcStatBtimePattern[] = R"(\nbtime (\d+)\n)";
  static const re2::LazyRE2 kStatBtimeRe = {kProcStatBtimePattern};

  const base::FilePath proc_stat_file(kProcStatFile);
  // Base doesn't have a ReadLine-ey helper. /proc/stat does scale with the
  // number of CPU threads but shouldn't be too bad on a mobile/desktop.
  // Around 1.5K on a 8-thread CPU. This is a one-time parse at startup.
  std::string proc_stat_contents;
  int64_t btime;
  if ((!base::ReadFileToString(root_path_.Append(kProcStatFile),
                               &proc_stat_contents)) ||
      (!RE2::PartialMatch(proc_stat_contents, *kStatBtimeRe, &btime))) {
    LOG(ERROR) << "Failed to parse boot time from " << kProcStatFile;
    return;
  }
  base::AutoLock lock(common_lock_);
  common_.set_device_boot_time(btime);
  LOG(INFO) << "Set device boot time to " << btime;
}

void MessageSender::InitializeAndWatchDeviceTz() {
  static constexpr char kTimezoneSymlink[] = "var/lib/timezone/localtime";
  const base::FilePath timezone_symlink = root_path_.Append(kTimezoneSymlink);
  UpdateDeviceTz(base::FilePath(timezone_symlink), false);
  if (!common_file_watcher_.Watch(
          timezone_symlink, base::FilePathWatcher::Type::kNonRecursive,
          base::BindRepeating(&MessageSender::UpdateDeviceTz,
                              base::Unretained(this)))) {
    LOG(ERROR) << "Failed to add a file watch on " << timezone_symlink.value();
  }
}

absl::Status MessageSender::InitializeQueues() {
  // Array of possible destinations.
  const reporting::Destination kDestinations[] = {
      reporting::CROS_SECURITY_PROCESS, reporting::CROS_SECURITY_AGENT};

  for (auto destination : kDestinations) {
    auto report_queue_result =
        reporting::ReportQueueFactory::CreateSpeculativeReportQueue(
            reporting::EventType::kDevice, destination);

    if (report_queue_result == nullptr) {
      return absl::InternalError(
          "InitializeQueues: Report queue failed to create");
    }
    queue_map_.insert(
        std::make_pair(destination, std::move(report_queue_result)));
  }
  return absl::OkStatus();
}

absl::Status MessageSender::Initialize() {
  // We don't care as much about common fields as we do about errors in creating
  // the message queues.
  InitializeDeviceBtime();
  InitializeAndWatchDeviceTz();
  return InitializeQueues();
}

absl::Status MessageSender::SendMessage(
    reporting::Destination destination,
    pb::CommonEventDataFields* mutable_common,
    std::unique_ptr<google::protobuf::MessageLite> message) {
  auto it = queue_map_.find(destination);
  CHECK(it != queue_map_.end());

  if (mutable_common) {
    base::AutoLock lock(common_lock_);
    mutable_common->CopyFrom(common_);
  }
  it->second.get()->Enqueue(std::move(message), ::reporting::SECURITY,
                            base::BindOnce(&EnqueueCallback, destination));
  return absl::OkStatus();
}

}  // namespace secagentd

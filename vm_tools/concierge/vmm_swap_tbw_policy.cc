// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_tbw_policy.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/containers/ring_buffer.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "vm_concierge/vmm_swap_policy.pb.h"
#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/vmm_swap_history_file.h"

namespace vm_tools::concierge {

namespace {
constexpr char kOldHistoryFileName[] = "tbw_history";
static constexpr base::TimeDelta WEEK = base::Days(7);
static constexpr char kMetricsTotalBytesWrittenInAWeek[] =
    "Memory.VmmSwap.TotalBytesWrittenInAWeek";
// 192 MiB which corresponds to ~.5% of total disk durability budget for device
// with a 32GiB drive.
static constexpr int kTotalBytesWrittenMin = 192;
// 20 GiB which corresponds to more than daily whole swap out (guest memory is
// less than 2 GiB).
static constexpr int kTotalBytesWrittenMax = GiB(20) / MiB(1);
// The bucket for 2 GiB has 200 MiB size and the last bucket has less than 2 GiB
// size.
static constexpr int kTotalBytesWrittenNumBuckets = 50;
}  // namespace

VmmSwapTbwPolicy::VmmSwapTbwPolicy(
    const raw_ref<MetricsLibraryInterface> metrics,
    std::unique_ptr<base::RepeatingTimer> report_timer)
    : metrics_(metrics), report_timer_(std::move(report_timer)) {
  // Push a sentinel. VmmSwapTbwPolicy::AppendEntry() checks the latest entry by
  // `tbw_history_.MutableReadBuffer()` which fails if current index is 0.
  tbw_history_.SaveToBuffer(
      BytesWritten{.started_at = base::Time(), .size = 0});
}

void VmmSwapTbwPolicy::SetTargetTbwPerDay(uint64_t target_tbw_per_day) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  target_tbw_per_day_ = target_tbw_per_day;
}

uint64_t VmmSwapTbwPolicy::GetTargetTbwPerDay() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return target_tbw_per_day_;
}

bool VmmSwapTbwPolicy::Init(base::FilePath path, base::Time now) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (history_file_.has_value()) {
    LOG(ERROR) << "Tbw history file is already loaded";
    return false;
  }
  history_file_path_ = path;

  // The old format file was named "tbw_history". If there is the file, load the
  // history from the old file and recreate a new file from the history.
  // TODO(b/289975202): Remove this 2 milestones (M119) after since we care
  // about the last 28 days history.
  base::FilePath old_file_path =
      history_file_path_.DirName().Append(kOldHistoryFileName);
  if (base::PathExists(old_file_path)) {
    LOG(INFO) << "Old tbw history file is found. Recreate a new history file.";
    base::File old_file = base::File(
        old_file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
    base::SetCloseOnExec(old_file.GetPlatformFile());
    // Remove the old file whether it succeeds to load history or not.
    brillo::DeleteFile(old_file_path);
    if (old_file.IsValid()) {
      if (LoadFromOldFormattedFile(old_file, now)) {
        // The file was old formatted. Replace the old history file with the new
        // file format.
        return RotateHistoryFile(now);
      }
    } else {
      LOG(ERROR) << "Failed to open old tbw history file: "
                 << old_file.ErrorToString(old_file.error_details());
    }
  }

  base::File file =
      base::File(path, base::File::FLAG_CREATE | base::File::FLAG_READ |
                           base::File::FLAG_WRITE);
  if (file.IsValid()) {
    LOG(INFO) << "Tbw history file is created at: " << path;
    base::SetCloseOnExec(file.GetPlatformFile());
    history_file_ = std::move(file);
  } else if (file.error_details() == base::File::FILE_ERROR_EXISTS) {
    LOG(INFO) << "Load tbw history from: " << path;
    file = base::File(history_file_path_, base::File::FLAG_OPEN |
                                              base::File::FLAG_READ |
                                              base::File::FLAG_WRITE);
    base::SetCloseOnExec(file.GetPlatformFile());
    if (LoadFromFile(file, now)) {
      history_file_ = std::move(file);
      // Resume reporting only when the previous tbw policy has started
      // reporting.
      if (last_reported_at_.has_value()) {
        auto delay = (last_reported_at_.value() + WEEK) - now;
        if (delay.is_negative()) {
          ReportTbwOfWeek();
        } else {
          report_timer_->Start(FROM_HERE, delay, this,
                               &VmmSwapTbwPolicy::ReportTbwOfWeek);
        }
      }
      return true;
    } else {
      DeleteFile();
    }
  } else {
    LOG(ERROR) << "Failed to create tbw history file: "
               << file.ErrorToString(file.error_details());
  }

  // Add pessimistic entries as if there were max disk writes in last 28days.
  // This prevent it from causing damage if the history file is removed (e.g. a
  // user factory resets their device).
  for (int i = 0; i < kTbwHistoryLength; i++) {
    base::Time time = now - base::Days(kTbwHistoryLength - i - 1);
    AppendEntry(target_tbw_per_day_, time);
    WriteBytesWrittenEntry(target_tbw_per_day_, time, /* try_rotate */ false);
  }
  // Mark metrics reporting history as uninitialized. It is initialized when it
  // is newly Record()ed and starts weekly timer for periodical reporting.
  last_reported_at_ = std::nullopt;
  return history_file_.has_value();
}

void VmmSwapTbwPolicy::Record(uint64_t bytes_written, base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  AppendEntry(bytes_written, time);

  if (!last_reported_at_.has_value()) {
    // Now that swap has run once, emit an reported entry so that the TBW metric
    // will be monitored going forward. This also ensure that the pessimistic
    // entries inserted by Init() are never included in the metric.
    MarkReported(time);
  }

  WriteBytesWrittenEntry(bytes_written, time);
}

bool VmmSwapTbwPolicy::CanSwapOut(base::Time time) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  uint64_t tbw_28days = 0, tbw_7days = 0, tbw_1day = 0;
  for (auto iter = tbw_history_.Begin(); iter; ++iter) {
    if ((time - iter->started_at) < base::Days(28)) {
      tbw_28days += iter->size;
    }
    if ((time - iter->started_at) < base::Days(7)) {
      tbw_7days += iter->size;
    }
    if ((time - iter->started_at) < base::Days(1)) {
      tbw_1day += iter->size;
    }
  }

  // The targets for recent time ranges are eased using scale factor.
  // target_tbw_per_day_ * <num_days> * <scale_factor>
  uint64_t target_28days = target_tbw_per_day_ * 28 * 1;
  uint64_t target_7days = target_tbw_per_day_ * 7 * 2;
  uint64_t target_1day = target_tbw_per_day_ * 1 * 4;
  return tbw_28days < target_28days && tbw_7days < target_7days &&
         tbw_1day < target_1day;
}

// Rotates the file if there are too many entries.
bool VmmSwapTbwPolicy::TryRotateFile(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if ((history_file_->GetLength() >= kMaxFileSize - kMaxEntrySize) &&
      !RotateHistoryFile(time)) {
    LOG(ERROR) << "Failed to rotate tbw history to file";
    DeleteFile();
    return false;
  }
  return true;
}

bool VmmSwapTbwPolicy::WriteEntry(TbwHistoryEntry entry,
                                  base::Time time,
                                  bool try_rotate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!history_file_.has_value()) {
    return false;
  } else if (try_rotate) {
    if (!TryRotateFile(time)) {
      return false;
    }
  }

  if (!VmmSwapWriteEntry<TbwHistoryEntryContainer>(history_file_.value(),
                                                   std::move(entry))) {
    LOG(ERROR) << "Failed to write tbw history to file";
    DeleteFile();
    return false;
  }
  return true;
}

bool VmmSwapTbwPolicy::WriteBytesWrittenEntry(uint64_t bytes_written,
                                              base::Time time,
                                              bool try_rotate) {
  TbwHistoryEntry entry;
  entry.set_time_us(time.ToDeltaSinceWindowsEpoch().InMicroseconds());
  entry.set_size(bytes_written);
  return WriteEntry(std::move(entry), time, try_rotate);
}

bool VmmSwapTbwPolicy::WriteReportEntry(base::Time time, bool try_rotate) {
  TbwHistoryEntry entry;
  entry.set_time_us(time.ToDeltaSinceWindowsEpoch().InMicroseconds());
  entry.set_reported_fence(true);
  return WriteEntry(std::move(entry), time, try_rotate);
}

bool VmmSwapTbwPolicy::LoadFromFile(base::File& file, base::Time now) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!file.IsValid()) {
    LOG(ERROR) << "tbw history file is invalid to load";
    return false;
  }

  int64_t file_size = file.GetLength();
  if (file_size < 0) {
    LOG(ERROR) << "Failed to get length of history file: "
               << file.ErrorToString(file.GetLastFileError());
    return false;
  } else if (file_size > kMaxFileSize) {
    // Validates the file size because this loads all entries at once.
    LOG(ERROR) << "tbw history file: " << base::NumberToString(file_size)
               << " is bigger than " << base::NumberToString(kMaxFileSize);
    return false;
  }

  TbwHistoryEntryContainer container;
  if (!container.ParseFromFileDescriptor(file.GetPlatformFile())) {
    LOG(ERROR) << "Failed to parse tbw history";
    return false;
  } else if (static_cast<int64_t>(container.ByteSizeLong()) != file_size) {
    LOG(ERROR) << "Failed to parse tbw history size";
    return false;
  }

  base::Time previous_time;
  for (auto entry : container.entries()) {
    base::Time time = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(entry.time_us()));
    if ((now - time).is_negative()) {
      LOG(ERROR) << "tbw history file has invalid time (too new)";
      return false;
    } else if ((time - previous_time).is_negative()) {
      LOG(ERROR) << "tbw history file has invalid time (old than lastest)";
      return false;
    }
    if (entry.reported_fence()) {
      last_reported_at_ = time;
    }
    AppendEntry(entry.size(), time);

    previous_time = time;
  }

  return true;
}

bool VmmSwapTbwPolicy::LoadFromOldFormattedFile(base::File& file,
                                                base::Time now) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!file.IsValid()) {
    LOG(ERROR) << "tbw history file is invalid to load";
    return false;
  }

  google::protobuf::io::FileInputStream input_stream(file.GetPlatformFile());
  TbwHistoryEntry entry;
  base::Time previous_time;
  while (true) {
    // Load message size.
    uint8_t* message_size;
    int size;
    if (input_stream.Next((const void**)&message_size, &size)) {
      DCHECK_GT(size, 0);
      // TbwHistoryEntry message is less than 127 bytes. The MSB is reserved
      // for future extensibility.
      if (*message_size > 127) {
        LOG(ERROR) << "tbw history message size is invalid: "
                   << base::NumberToString(*message_size);
        return false;
      }
      // Consume 1 byte for message size field.
      input_stream.BackUp(size - 1);
    } else if (input_stream.GetErrno()) {
      LOG(ERROR) << "parse tbw history message size: errno: "
                 << base::NumberToString(input_stream.GetErrno());
      return false;
    } else {
      // EOF
      break;
    }

    if (!entry.ParseFromBoundedZeroCopyStream(&input_stream, *message_size)) {
      LOG(ERROR) << "parse tbw history entry";
      return false;
    }
    base::Time time = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(entry.time_us()));
    if ((now - time).is_negative()) {
      LOG(ERROR) << "tbw history file has invalid time (too new)";
      return false;
    } else if ((time - previous_time).is_negative()) {
      LOG(ERROR) << "tbw history file has invalid time (old than lastest)";
      return false;
    }
    // Old file format does not support `reported_fence`.
    AppendEntry(entry.size(), time);
    previous_time = time;
  }
  return true;
}

void VmmSwapTbwPolicy::AppendEntry(uint64_t bytes_written, base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto latest_entry =
      tbw_history_.MutableReadBuffer(tbw_history_.BufferSize() - 1);

  if ((time - latest_entry->started_at) >= base::Hours(24)) {
    tbw_history_.SaveToBuffer(
        BytesWritten{.started_at = time, .size = bytes_written});
  } else {
    latest_entry->size += bytes_written;
  }
}

bool VmmSwapTbwPolicy::RotateHistoryFile(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::FilePath tmp_file_path = history_file_path_.AddExtension("tmp");

  history_file_ = base::File(tmp_file_path, base::File::FLAG_CREATE_ALWAYS |
                                                base::File::FLAG_READ |
                                                base::File::FLAG_WRITE);
  base::SetCloseOnExec(history_file_->GetPlatformFile());
  if (!history_file_->IsValid()) {
    LOG(ERROR) << "Failed to create new tbw history file: "
               << history_file_->ErrorToString(history_file_->error_details());
    DeleteFile();
    return false;
  }
  bool success = true;

  bool store_last_report_log = last_reported_at_.has_value();
  for (auto iter = tbw_history_.Begin(); success && iter; ++iter) {
    if (store_last_report_log && iter->started_at > last_reported_at_.value()) {
      store_last_report_log = false;
      if (!WriteReportEntry(last_reported_at_.value(),
                            /* try_rotate */ false)) {
        LOG(ERROR) << "Failed to write entries to new tbw history file";
        success = false;
      }
    }
    if ((time - iter->started_at) < base::Days(28)) {
      if (!WriteBytesWrittenEntry(iter->size, iter->started_at,
                                  /* try_rotate */ false)) {
        LOG(ERROR) << "Failed to write entries to new tbw history file";
        success = false;
      }
    }
  }
  if (success && store_last_report_log) {
    if (!WriteReportEntry(last_reported_at_.value(), /* try_rotate */ false)) {
      LOG(ERROR) << "Failed to write entries to new tbw history file";
      success = false;
    }
  }

  base::File::Error error;
  if (success &&
      !base::ReplaceFile(tmp_file_path, history_file_path_, &error)) {
    LOG(ERROR) << "Failed to replace history file: "
               << history_file_->ErrorToString(error);
    success = false;
  }

  if (!success) {
    // If Write*Entry() method fails to write an entry, it deletes the original
    // file and closes the temporary file descriptor.
    // Remove the remaining temporary file here.
    if (!brillo::DeleteFile(tmp_file_path)) {
      LOG(ERROR) << "Failed to delete tmp history file";
    }
    return false;
  }

  LOG(INFO) << "Tbw history file is rotated";

  return true;
}

void VmmSwapTbwPolicy::DeleteFile() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!brillo::DeleteFile(history_file_path_)) {
    LOG(ERROR) << "Failed to delete history file.";
  }
  // Stop writing entries to the file and close the file.
  history_file_ = std::nullopt;
}

void VmmSwapTbwPolicy::MarkReported(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  WriteReportEntry(time);
  last_reported_at_ = time;
  if (!report_timer_->IsRunning() || report_timer_->GetCurrentDelay() != WEEK) {
    report_timer_->Start(FROM_HERE, WEEK, this,
                         &VmmSwapTbwPolicy::ReportTbwOfWeek);
  }
}

void VmmSwapTbwPolicy::ReportTbwOfWeek() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::Time now = base::Time::Now();
  CHECK(last_reported_at_.has_value());
  int weeks_to_report = (now - last_reported_at_.value()).IntDiv(WEEK);
  if (weeks_to_report >= 4) {
    // The ring buffered tbw history may have dropped history more than 4 weeks
    // old. The last_reported_at_ can be too long time ago if the device has
    // been powered off for a long time.
    last_reported_at_ = now - (4 * WEEK);
    weeks_to_report = 4;
  }
  for (int i = 0; i < weeks_to_report; i++) {
    auto start = last_reported_at_.value() + (i * WEEK);
    auto end = start + WEEK;
    int64_t sum = 0;
    for (auto iter = tbw_history_.Begin(); iter; ++iter) {
      if (start <= iter->started_at && iter->started_at < end) {
        sum += static_cast<int64_t>(iter->size);
      }
    }
    if (!metrics_->SendToUMA(kMetricsTotalBytesWrittenInAWeek,
                             static_cast<int>(sum / MiB(1)),
                             kTotalBytesWrittenMin, kTotalBytesWrittenMax,
                             kTotalBytesWrittenNumBuckets)) {
      LOG(ERROR) << "Failed to send total bytes written metrics";
    }
  }

  MarkReported(last_reported_at_.value() + (weeks_to_report * WEEK));
}

}  // namespace vm_tools::concierge

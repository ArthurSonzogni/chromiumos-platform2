// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_usage_policy.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include <base/containers/span.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/sequence_checker.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <brillo/files/file_util.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "vm_concierge/vmm_swap_policy.pb.h"
#include "vm_tools/concierge/vmm_swap_history_file.h"

namespace vm_tools::concierge {

namespace {
constexpr base::TimeDelta WEEK = base::Days(7);
}  // namespace

bool VmmSwapUsagePolicy::Init(base::FilePath path, base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (history_file_.has_value()) {
    LOG(ERROR) << "Usage history file is already loaded";
    return false;
  }
  history_file_path_ = path;

  base::File file =
      base::File(path, base::File::FLAG_CREATE | base::File::FLAG_READ |
                           base::File::FLAG_WRITE);
  if (file.IsValid()) {
    LOG(INFO) << "Usage history file is created at: " << path;
    base::SetCloseOnExec(file.GetPlatformFile());
    history_file_ = std::move(file);
    return true;
  }

  if (file.error_details() != base::File::FILE_ERROR_EXISTS) {
    LOG(ERROR) << "Failed to create usage history file: "
               << file.ErrorToString(file.error_details());
    return false;
  }

  LOG(INFO) << "Load usage history from: " << path;
  file = base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                              base::File::FLAG_WRITE);
  if (!file.IsValid()) {
    LOG(ERROR) << "Failed to open usage history file: "
               << file.ErrorToString(file.error_details());
    return false;
  }
  base::SetCloseOnExec(file.GetPlatformFile());

  // Load entries in the file and move the file offset to the tail
  if (LoadFromFile(file, time)) {
    history_file_ = std::move(file);
    return true;
  } else {
    DeleteFile();
    usage_history_.Clear();
    return false;
  }
}

void VmmSwapUsagePolicy::OnEnabled(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_enabled_) {
    return;
  }
  is_enabled_ = true;

  if (usage_history_.CurrentIndex() == 0 ||
      usage_history_.ReadBuffer(usage_history_.BufferSize() - 1).start <=
          time - base::Hours(1)) {
    struct SwapPeriod entry;
    entry.start = time;
    entry.duration.reset();
    usage_history_.SaveToBuffer(entry);
  }
}

void VmmSwapUsagePolicy::OnDisabled(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AddEnableRecordIfMissing(time);

  if (!is_enabled_) {
    return;
  }
  is_enabled_ = false;

  auto latest_entry =
      usage_history_.MutableReadBuffer(usage_history_.BufferSize() - 1);
  if (latest_entry->start > time) {
    LOG(WARNING) << "Time mismatch: (enabled) " << latest_entry->start
                 << " > (disabled) " << time;
    return;
  } else if (latest_entry->duration.has_value()) {
    return;
  }
  latest_entry->duration = time - latest_entry->start;

  WriteEnabledDurationEntry(latest_entry->start,
                            latest_entry->duration.value());
}

void VmmSwapUsagePolicy::OnDestroy(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!(is_enabled_ && history_file_.has_value())) {
    return;
  }

  base::Time start_time;
  auto latest_entry =
      usage_history_.ReadBuffer(usage_history_.BufferSize() - 1);
  // Check the latest entry is in file or not. The shutdown entry must have
  // later timestamp than the latest entry in the file.
  if (!latest_entry.duration.has_value()) {
    start_time = latest_entry.start;
  } else if ((time - latest_entry.start) >= base::Hours(1)) {
    start_time = latest_entry.start + base::Hours(1);
  } else {
    start_time = time;
  }
  WriteShutdownEntry(start_time);
}

base::TimeDelta VmmSwapUsagePolicy::PredictDuration(base::Time now) {
  // Predict when vmm-swap is disabled by averaging the last 4 weeks log.
  // If this has less than 1 week log, this estimates to be disabled after the
  // double length of the latest enabled duration.
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AddEnableRecordIfMissing(now);

  if (usage_history_.CurrentIndex() == 0) {
    // There are no data.
    return base::TimeDelta();
  }

  base::TimeDelta sum = base::TimeDelta();
  int num_weeks_to_count = (now - usage_history_.Begin()->start).IntDiv(WEEK);
  if (num_weeks_to_count > kUsageHistoryNumWeeks) {
    num_weeks_to_count = kUsageHistoryNumWeeks;
  }
  if (num_weeks_to_count == 0) {
    // There is less than 1 week data.
    auto latest_entry =
        usage_history_.ReadBuffer(usage_history_.BufferSize() - 1);
    return latest_entry.duration.value_or(now - latest_entry.start) * 2;
  }
  for (auto iter = usage_history_.Begin(); iter; ++iter) {
    base::TimeDelta duration = iter->duration.value_or(now - iter->start);

    int64_t start_weeks_ago = std::min((now - iter->start).IntDiv(WEEK),
                                       (int64_t)kUsageHistoryNumWeeks);
    int64_t end_weeks_ago = (now - (iter->start + duration)).IntDiv(WEEK);

    // The record which is across the projected time of the week is used for the
    // prediction.
    if (end_weeks_ago < kUsageHistoryNumWeeks &&
        start_weeks_ago != end_weeks_ago) {
      base::Time projected_time = now - WEEK * start_weeks_ago;
      base::TimeDelta duration_of_week =
          duration + iter->start - projected_time;
      sum += duration_of_week;
      while (duration_of_week > WEEK) {
        duration_of_week -= WEEK;
        sum += duration_of_week;
      }
    }
  }

  return sum / num_weeks_to_count;
}

// Enable record can be skipped if it is enabled again within 1 hour. However if
// it is disabled after more than 1 hour, a new record should be added to the
// history. The time enabled is between `latest_entry->start` and 1 hour later.
// We use `latest_entry->start` + 1 hour pessimistically as the enabled time of
// the new record.
void VmmSwapUsagePolicy::AddEnableRecordIfMissing(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_enabled_) {
    return;
  }
  auto latest_entry =
      usage_history_.ReadBuffer(usage_history_.BufferSize() - 1);
  if (latest_entry.duration.has_value() &&
      (time - latest_entry.start) >= base::Hours(1)) {
    struct SwapPeriod entry;
    entry.start = latest_entry.start + base::Hours(1);
    entry.duration.reset();
    usage_history_.SaveToBuffer(entry);
  }
}

// Rotates the file if the file size is too big.
bool VmmSwapUsagePolicy::TryRotateFile(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if ((history_file_->GetLength() >= kMaxFileSize - kMaxEntrySize) &&
      !RotateHistoryFile(time)) {
    LOG(ERROR) << "Failed to rotate usage history to file";
    DeleteFile();
    return false;
  }
  return true;
}

bool VmmSwapUsagePolicy::WriteEntry(UsageHistoryEntry entry,
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

  if (!VmmSwapWriteEntry<UsageHistoryEntryContainer>(history_file_.value(),
                                                     std::move(entry))) {
    LOG(ERROR) << "Failed to write usage history to file";
    DeleteFile();
    return false;
  }
  return true;
}

bool VmmSwapUsagePolicy::WriteEnabledDurationEntry(base::Time time,
                                                   base::TimeDelta duration,
                                                   bool try_rotate) {
  UsageHistoryEntry entry;
  entry.set_start_time_us(time.ToDeltaSinceWindowsEpoch().InMicroseconds());
  entry.set_duration_us(duration.InMicroseconds());
  entry.set_is_shutdown(false);
  return WriteEntry(std::move(entry), time, try_rotate);
}

bool VmmSwapUsagePolicy::WriteShutdownEntry(base::Time time) {
  UsageHistoryEntry entry;
  entry.set_start_time_us(time.ToDeltaSinceWindowsEpoch().InMicroseconds());
  entry.set_is_shutdown(true);
  return WriteEntry(std::move(entry), time, true);
}

bool VmmSwapUsagePolicy::LoadFromFile(base::File& file, base::Time now) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!file.IsValid()) {
    LOG(ERROR) << "usage history file is invalid to load";
    return false;
  }

  int64_t file_size = file.GetLength();
  if (file_size < 0) {
    LOG(ERROR) << "get length of history file: "
               << file.ErrorToString(file.GetLastFileError());
    return false;
  } else if (file_size > kMaxFileSize) {
    // Validates the file size because this loads all entries at once.
    LOG(ERROR) << "usage history file: " << base::NumberToString(file_size)
               << " is bigger than " << base::NumberToString(kMaxFileSize);
    return false;
  }

  UsageHistoryEntryContainer container;
  if (!container.ParseFromFileDescriptor(file.GetPlatformFile())) {
    LOG(ERROR) << "parse usage history";
    return false;
  } else if (static_cast<int64_t>(container.ByteSizeLong()) != file_size) {
    LOG(ERROR) << "parse usage history size";
    return false;
  }

  base::Time previous_time;
  std::optional<base::Time> shutdown_time;
  for (auto entry : container.entries()) {
    base::Time time = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(entry.start_time_us()));
    base::TimeDelta duration = base::Microseconds(entry.duration_us());
    if ((now - time).is_negative()) {
      LOG(ERROR) << "usage history file has invalid time (too new)";
      return false;
    } else if ((time - previous_time).is_negative()) {
      LOG(ERROR) << "usage history file has invalid time (old than lastest)";
      return false;
    }

    if (entry.is_shutdown()) {
      shutdown_time = time;
    } else {
      if (duration.is_negative()) {
        LOG(ERROR) << "usage history file has invalid duration (negative)";
        return false;
      }
      if (time + duration > now - kUsageHistoryNumWeeks * WEEK) {
        struct SwapPeriod period_entry;
        period_entry.start = time;
        period_entry.duration = duration;
        usage_history_.SaveToBuffer(period_entry);
      }
      shutdown_time.reset();
    }

    previous_time = time;
  }

  // If the last entry is OnShutdown entry, it means that the VM was shutdown
  // while vmm-swap is enabled. We treat the duration while the device is
  // powered off as the VM was idle (i.e. vmm-swap was enabled). The
  // start_time_us of OnShutdown entry is the last time when vmm-swap was
  // enabled.
  if (shutdown_time.has_value()) {
    is_enabled_ = true;
    if (usage_history_.CurrentIndex() == 0 ||
        usage_history_.ReadBuffer(usage_history_.BufferSize() - 1).start +
                base::Hours(1) <=
            shutdown_time.value()) {
      struct SwapPeriod entry;
      entry.start = shutdown_time.value();
      entry.duration.reset();
      usage_history_.SaveToBuffer(entry);
    }
  }

  return true;
}

bool VmmSwapUsagePolicy::RotateHistoryFile(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::FilePath tmp_file_path = history_file_path_.AddExtension("tmp");

  history_file_ = base::File(tmp_file_path, base::File::FLAG_CREATE_ALWAYS |
                                                base::File::FLAG_READ |
                                                base::File::FLAG_WRITE);
  if (!history_file_->IsValid()) {
    LOG(ERROR) << "Failed to create new usage history file: "
               << history_file_->ErrorToString(history_file_->error_details());
    DeleteFile();
    return false;
  }
  base::SetCloseOnExec(history_file_->GetPlatformFile());
  bool success = true;

  for (auto iter = usage_history_.Begin(); success && iter; ++iter) {
    if (iter->duration.has_value() && (iter->start + iter->duration.value()) >
                                          time - kUsageHistoryNumWeeks * WEEK) {
      if (!WriteEnabledDurationEntry(iter->start, iter->duration.value(),
                                     /* try_rotate */ false)) {
        LOG(ERROR) << "Failed to add a new usage history to file";
        success = false;
      }
    }
  }

  base::File::Error error;
  if (success &&
      !base::ReplaceFile(tmp_file_path, history_file_path_, &error)) {
    LOG(ERROR) << "Failed to replace usage history file: "
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

  LOG(INFO) << "Usage history file is rotated";

  return true;
}

void VmmSwapUsagePolicy::DeleteFile() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!brillo::DeleteFile(history_file_path_)) {
    LOG(ERROR) << "Failed to delete usage history file.";
  }
  // Stop writing entries to the file and close the file.
  history_file_ = std::nullopt;
}

}  // namespace vm_tools::concierge

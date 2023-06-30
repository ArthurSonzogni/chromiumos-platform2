// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_tbw_policy.h"

#include <algorithm>
#include <cstdint>
#include <memory>
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

namespace vm_tools::concierge {

namespace {
constexpr char kOldHistoryFileName[] = "tbw_history";
base::expected<size_t, std::string> WriteEntry(base::File& file,
                                               uint64_t bytes_written,
                                               base::Time time) {
  // Consecutively serialized bytes from multiple TbwHistoryEntryContainers
  // can be deserialized as single merged TbwHistoryEntryContainer.
  TbwHistoryEntryContainer container;
  TbwHistoryEntry* entry = container.add_entries();
  entry->set_time_us(time.ToDeltaSinceWindowsEpoch().InMicroseconds());
  entry->set_size(bytes_written);
  if (container.SerializeToFileDescriptor(file.GetPlatformFile())) {
    return base::ok(container.GetCachedSize());
  } else {
    return base::unexpected("failed to write tbw history");
  }
}
}  // namespace

VmmSwapTbwPolicy::VmmSwapTbwPolicy() {
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
  if (history_file_.IsValid()) {
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
    // Remove the old file whether it succeeds to load history or not.
    brillo::DeleteFile(old_file_path);
    if (old_file.IsValid()) {
      auto result = LoadFromOldFormattedFile(old_file, now);
      if (result.has_value()) {
        // The file was old formatted. Replace the old history file with the new
        // file format.
        return RotateHistoryFile(now);
      } else {
        LOG(ERROR) << "Failed to load old tbw history file: " << result.error();
      }
    } else {
      LOG(ERROR) << "Failed to open old tbw history file: "
                 << old_file.ErrorToString(old_file.error_details());
    }
  }

  history_file_ =
      base::File(path, base::File::FLAG_CREATE | base::File::FLAG_READ |
                           base::File::FLAG_WRITE);
  if (history_file_.IsValid()) {
    LOG(INFO) << "Tbw history file is created at: " << path;

    // Add pessimistic entries as if there were max disk writes in last 28days.
    // This prevent it from causing damage if the history file is removed (e.g.
    // a user factory resets their device).
    for (int i = 0; i < kTbwHistoryLength; i++) {
      Record(target_tbw_per_day_, now - base::Days(kTbwHistoryLength - i - 1));
    }
    return true;
  }

  if (history_file_.error_details() == base::File::FILE_ERROR_EXISTS) {
    LOG(INFO) << "Load tbw history from: " << path;
    history_file_ = base::File(history_file_path_, base::File::FLAG_OPEN |
                                                       base::File::FLAG_READ |
                                                       base::File::FLAG_WRITE);
    auto file_size = LoadFromFile(history_file_, now);
    if (file_size.has_value()) {
      history_file_size_ = file_size.value();
      return true;
    } else {
      LOG(ERROR) << "Failed to load tbw history file: " << file_size.error();
    }
  } else {
    LOG(ERROR) << "Failed to create tbw history file: "
               << history_file_.error_details();
  }

  DeleteFile();

  // Initialize pessimistic entries as fallback.
  for (int i = 0; i < kTbwHistoryLength; i++) {
    AppendEntry(target_tbw_per_day_,
                now - base::Days(kTbwHistoryLength - i - 1));
  }
  return false;
}

void VmmSwapTbwPolicy::Record(uint64_t bytes_written, base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  AppendEntry(bytes_written, time);

  if (history_file_.IsValid()) {
    if (history_file_size_ >= kMaxFileSize - kMaxEntrySize &&
        !RotateHistoryFile(time)) {
      LOG(ERROR) << "Failed to rotate tbw file";
      // Stop writing a new entry to the history file.
      DeleteFile();
      return;
    }
    auto entry_size = WriteEntry(history_file_, bytes_written, time);
    if (entry_size.has_value()) {
      history_file_size_ += entry_size.value();
    } else {
      LOG(ERROR) << "Failed to write tbw entry to file";
      // Delete the history file since the file content is now broken.
      DeleteFile();
    }
  }
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

base::expected<size_t, std::string> VmmSwapTbwPolicy::LoadFromFile(
    base::File& file, base::Time now) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!file.IsValid()) {
    return base::unexpected("tbw history file is invalid to load");
  }

  int64_t file_size = file.GetLength();
  if (file_size < 0) {
    return base::unexpected("get length of history file: " +
                            file.ErrorToString(file.GetLastFileError()));
  } else if (file_size > kMaxFileSize) {
    // Validates the file size because this loads all entries at once.
    return base::unexpected(
        base::StrCat({"tbw history file: ", base::NumberToString(file_size),
                      " is bigger than ", base::NumberToString(kMaxFileSize)}));
  }

  TbwHistoryEntryContainer container;
  if (!container.ParseFromFileDescriptor(file.GetPlatformFile())) {
    return base::unexpected("parse tbw history");
  }

  base::Time previous_time;
  for (auto entry : container.entries()) {
    base::Time time = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(entry.time_us()));
    if ((now - time).is_negative()) {
      return base::unexpected("tbw history file has invalid time (too new)");
    } else if ((time - previous_time).is_negative()) {
      return base::unexpected(
          "tbw history file has invalid time (old than lastest)");
    }
    AppendEntry(entry.size(), time);

    previous_time = time;
  }

  return file_size;
}

base::expected<void, std::string> VmmSwapTbwPolicy::LoadFromOldFormattedFile(
    base::File& file, base::Time now) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!file.IsValid()) {
    return base::unexpected("tbw history file is invalid to load");
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
        return base::unexpected("tbw history message size is invalid: " +
                                base::NumberToString(*message_size));
      }
      // Consume 1 byte for message size field.
      input_stream.BackUp(size - 1);
    } else if (input_stream.GetErrno()) {
      return base::unexpected("parse tbw history message size: errno: " +
                              base::NumberToString(input_stream.GetErrno()));
    } else {
      // EOF
      break;
    }

    if (!entry.ParseFromBoundedZeroCopyStream(&input_stream, *message_size)) {
      return base::unexpected("parse tbw history entry");
    }
    base::Time time = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(entry.time_us()));
    if ((now - time).is_negative()) {
      return base::unexpected("tbw history file has invalid time (too new)");
    } else if ((time - previous_time).is_negative()) {
      return base::unexpected(
          "tbw history file has invalid time (old than lastest)");
    }
    AppendEntry(entry.size(), time);
    previous_time = time;
  }
  return base::ok();
}

void VmmSwapTbwPolicy::AppendEntry(uint64_t bytes_written, base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto latest_entry =
      tbw_history_.MutableReadBuffer(tbw_history_.BufferSize() - 1);

  if ((time - latest_entry->started_at) > base::Hours(24)) {
    tbw_history_.SaveToBuffer(
        BytesWritten{.started_at = time, .size = bytes_written});
  } else {
    latest_entry->size += bytes_written;
  }
}

bool VmmSwapTbwPolicy::RotateHistoryFile(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::FilePath tmp_file_path = history_file_path_.AddExtension("tmp");
  uint32_t flags = base::File::Flags::FLAG_CREATE_ALWAYS |
                   base::File::Flags::FLAG_READ | base::File::Flags::FLAG_WRITE;
  base::File tmp_file = base::File(tmp_file_path, flags);
  if (!tmp_file.IsValid()) {
    LOG(ERROR) << "Failed to create new tbw history file: "
               << history_file_.error_details();
    return false;
  }

  history_file_size_ = 0;
  for (auto iter = tbw_history_.Begin(); iter; ++iter) {
    if ((time - iter->started_at) < base::Days(28)) {
      auto entry_size = WriteEntry(tmp_file, iter->size, iter->started_at);
      if (entry_size.has_value()) {
        history_file_size_ += entry_size.value();
      } else {
        LOG(ERROR) << "Failed to write entries to new tbw history file";
        return false;
      }
    }
  }

  base::File::Error error;
  if (!base::ReplaceFile(tmp_file_path, history_file_path_, &error)) {
    LOG(ERROR) << "Failed to replace history file: " << error;
    if (!brillo::DeleteFile(tmp_file_path)) {
      LOG(ERROR) << "Failed to delete tmp history file";
    }
    DeleteFile();
    return false;
  }

  // The obsolete history file is closed. The file is automatically disposed
  // since the file is already unlinked by rename(2).
  history_file_ = std::move(tmp_file);

  return true;
}

void VmmSwapTbwPolicy::DeleteFile() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!brillo::DeleteFile(history_file_path_)) {
    LOG(ERROR) << "Failed to delete history file.";
  }
  // Stop writing entries to the file.
  history_file_.Close();
  history_file_size_ = 0;
}

}  // namespace vm_tools::concierge

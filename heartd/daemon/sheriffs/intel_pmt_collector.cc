// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/sheriffs/intel_pmt_collector.h"

#include <fcntl.h>

#include <algorithm>
#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/files/file_util.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <libpmt/pmt.h>

namespace heartd {

IntelPMTCollector::IntelPMTCollector(const base::FilePath& root_dir,
                                     pmt::PmtCollector* collector,
                                     pmt::Snapshot* snapshot)
    : root_dir_(root_dir) {
  if (collector == nullptr) {
    collector_ = std::make_unique<pmt::PmtCollector>();
    auto guids = collector_->DetectDevices();
    if (guids.empty()) {
      return;
    }
    if (collector_->SetUpCollection(guids) < 0) {
      LOG(ERROR) << "Failed to set up Intel PMT collector guid collections";
      return;
    }
  } else {
    collector_.reset(collector);
  }

  // Set up the pointer to the snapshot data. This pointer won't change between
  // PmtCollector::SetUpCollection() and PmtCollector::CleanUpCollection().
  if (snapshot == nullptr) {
    snapshot_ = collector_->GetData();
  } else {
    snapshot_ = snapshot;
  }

  // Exit early if there is no config file.
  if (!base::PathExists(root_dir_.Append(kIntelPMTConfigPath))) {
    return;
  }

  // If fail to read the config file, we just use default setting.
  std::string content;
  if (base::ReadFileToString(root_dir_.Append(kIntelPMTConfigPath), &content)) {
    config_ = base::JSONReader::ReadDict(content).value_or(base::Value::Dict{});
  }

  // Open the log file.
  auto log_path = root_dir_.Append(kIntelPMTLogPath);
  log_fd_ = open(log_path.MaybeAsASCII().c_str(), O_RDWR | O_CREAT, 0664);
  if (log_fd_ == -1) {
    LOG(ERROR) << "Failed to open " << log_path;
  }

  // Open the counter file.
  auto counter_path = root_dir_.Append(kIntelPMTCounterPath);
  counter_fd_ =
      open(counter_path.MaybeAsASCII().c_str(), O_RDWR | O_CREAT, 0660);
  if (counter_fd_ == -1) {
    LOG(ERROR) << "Failed to open " << counter_path;
  }

  // Set up counter.
  if (ReadFileToString(counter_path, &content)) {
    if (!base::StringToInt(content, &counter_)) {
      LOG(ERROR) << "Failed to parse counter file, set counter to 0";
      counter_ = 0;
    }
  }
}

IntelPMTCollector::~IntelPMTCollector() {
  if (log_fd_ != -1) {
    close(log_fd_);
  }

  if (counter_fd_ != -1) {
    close(counter_fd_);
  }
}

void IntelPMTCollector::OneShotWork() {
  // If there is no shift work, it means we don't need to check the log header.
  if (!HasShiftWork()) {
    return;
  }

  auto fis = std::make_unique<google::protobuf::io::FileInputStream>(log_fd_);
  auto is = std::make_unique<google::protobuf::io::CodedInputStream>(fis.get());
  pmt::LogHeader header;
  // We have to set this otherwise `ByteSizeLong()` returns 0.
  header.set_snapshot_size(1);
  header_size_ = header.ByteSizeLong();

  auto limit = is->PushLimit(header_size_);
  if (!header.ParseFromCodedStream(is.get()) || !is->ConsumedEntireMessage()) {
    LOG(INFO) << "Failed to parse the header, clean up logs";
    CleanUpLogsAndSetNewHeader();
    return;
  }
  is->PopLimit(limit);

  LOG(INFO) << "PMT snapshot_size_ = " << snapshot_->ByteSizeLong();
  LOG(INFO) << "PMT old_snapshot_size = " << header.snapshot_size();
  if (header.snapshot_size() != snapshot_->ByteSizeLong()) {
    LOG(INFO) << "PMT snapshot size changes, clean up logs";
    CleanUpLogsAndSetNewHeader();
    return;
  }
}

void IntelPMTCollector::CleanUpLogsAndSetNewHeader() {
  if (ftruncate(log_fd_, 0) != 0) {
    LOG(ERROR) << "Failed to clean up PMT log file";
  }

  lseek(log_fd_, 0, SEEK_SET);
  pmt::LogHeader header;
  header.set_snapshot_size(snapshot_->ByteSizeLong());
  header.SerializeToFileDescriptor(log_fd_);
  counter_ = 0;
}

bool IntelPMTCollector::HasShiftWork() {
  return snapshot_ != nullptr && log_fd_ != -1 && counter_fd_ != -1;
}

void IntelPMTCollector::AdjustSchedule() {
  int freq = config_.FindInt(kIntelPMTConfigSampleFrequency).value_or(10);
  schedule_ = base::Seconds(freq);
}

void IntelPMTCollector::ShiftWork() {
  int res = collector_->TakeSnapshot();
  if (res) {
    LOG(ERROR) << "Intel PMT collector fails to take snapshot, error: " << res;
    return;
  }

  WriteSnapshot();
}

void IntelPMTCollector::WriteSnapshot() {
  // Update the counter, so we can locate to the correct position after restart.
  counter_ = (counter_ + 1) % 8640;
  if (counter_ == 0) {
    lseek(log_fd_, header_size_, SEEK_SET);
  }

  if (!snapshot_->SerializeToFileDescriptor(log_fd_)) {
    LOG(ERROR) << "Failed to serialize Intel PMT data snapshot";
  }

  lseek(counter_fd_, 0, SEEK_SET);
  if (!base::WriteFileDescriptor(counter_fd_, std::to_string(counter_))) {
    LOG(ERROR) << "Failed to update PMT records counter";
  }
}

void IntelPMTCollector::CleanUp() {
  // Since we maintain a circular queue inside the `kIntelPMTLogPath`, so we
  // don't need to clean up the records as long as `HasShiftWork()` returns
  // true. If it returns false, we simply remove the file.
  if (!HasShiftWork()) {
    if (log_fd_ != -1) {
      close(log_fd_);
    }
    brillo::DeleteFile(root_dir_.Append(kIntelPMTLogPath));
    return;
  }
}

}  // namespace heartd

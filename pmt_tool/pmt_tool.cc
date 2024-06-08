// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <fcntl.h>
#include <string>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

#include "pmt_tool/pmt_tool.h"

namespace pmt_tool {

FileSource::~FileSource() {
  is_.release();
  if (fd_ != -1)
    close(fd_);
  fis_.release();
}

bool FileSource::SetUp(const Options& options) {
  fd_ = open(options.sampling.input_file.value().c_str(), O_RDONLY | O_CLOEXEC);
  if (fd_ == -1) {
    PLOG(ERROR) << "Failed to open input file " << options.sampling.input_file;
    return false;
  }
  fis_ = std::make_unique<google::protobuf::io::FileInputStream>(fd_);
  is_ = std::make_unique<google::protobuf::io::CodedInputStream>(fis_.get());
  // Determine snapshot size by reading the log header first.
  pmt::LogHeader header;
  // Set the size field to get the right size.
  header.set_snapshot_size(1);
  size_t size = header.ByteSizeLong();
  // Read the header and extract the snapshot size.
  auto limit = is_->PushLimit(size);
  if (!header.ParseFromCodedStream(is_.get()) || !is_->ConsumedEntireMessage())
    return false;
  is_->PopLimit(limit);
  size_ = header.snapshot_size();

  return true;
}

std::optional<const pmt::Snapshot*> FileSource::TakeSnapshot() {
  // This limit has to be pushed and popped in each sampling. Otherwise
  // CodedInputStream won't advance.
  auto limit = is_->PushLimit(size_);
  bool result =
      snapshot_.ParseFromCodedStream(is_.get()) && is_->ConsumedEntireMessage();
  is_->PopLimit(limit);
  if (!result || !snapshot_.timestamp())
    return std::optional<const pmt::Snapshot*>();
  return &snapshot_;
}

size_t FileSource::GetSnapshotSize() {
  return size_;
}

bool LibPmtSource::SetUp(const Options& options) {
  auto guids = collector_.DetectDevices();

  LOG_DBG << "Detected the following GUIDs:";
  for (auto guid : guids) {
    LOG_DBG << " 0x" << std::hex << guid;
  }
  int res = collector_.SetUpCollection(guids);
  if (res < 0) {
    LOG(ERROR) << "Failed to setup collection for all GUIDs: "
               << strerror(-res);
    return false;
  }
  return true;
}

std::optional<const pmt::Snapshot*> LibPmtSource::TakeSnapshot() {
  int res = collector_.TakeSnapshot();
  if (res) {
    LOG(ERROR) << "Error taking PMT snapshot: " << strerror(-res);
    return std::optional<const pmt::Snapshot*>();
  }
  return collector_.GetData();
}

size_t LibPmtSource::GetSnapshotSize() {
  return collector_.GetData()->ByteSizeLong();
}

bool RawFormatter::SetUp(const Options& opts, int fd, size_t snapshot_size) {
  fd_ = fd;
  // Write the snapshot size first. Snapshot size will not change on a single
  // device.
  pmt::LogHeader header;
  header.set_snapshot_size(snapshot_size);
  header.SerializeToFileDescriptor(fd_);
  return true;
}

void RawFormatter::Format(const pmt::Snapshot& snapshot) {
  // Note that we can use SerializeWithCachedSizes() because
  // snapshot.ByteSizeLong() was called prior to Setup() and calculated the
  // message size (which will not change).
  snapshot.SerializeToFileDescriptor(fd_);
}

bool DbgFormatter::SetUp(const Options& opts, int fd, size_t snapshot_size) {
  fd_ = fd;

  pmt::LogHeader header;
  header.set_snapshot_size(snapshot_size);
  if (!base::WriteFileDescriptor(fd_, header.DebugString())) {
    PLOG(ERROR) << "Failed to write to output file";
    return false;
  }
  return true;
}

void DbgFormatter::Format(const pmt::Snapshot& snapshot) {
  if (!base::WriteFileDescriptor(fd_, snapshot.DebugString()))
    PLOG(ERROR) << "Failed to write to output file";
}

bool CsvFormatter::SetUp(const Options& opts, int fd, size_t snapshot_size) {
  return false;
}

void CsvFormatter::Format(const pmt::Snapshot& snapshot) {
  LOG(FATAL) << "NOT IMPLEMENTED";
}

int do_run(Options& opts, Source& source, Formatter& formatter) {
  // Set up the source.
  if (!source.SetUp(opts))
    return 2;
  // Set up formatter for a given output.
  if (!formatter.SetUp(opts, STDOUT_FILENO, source.GetSnapshotSize()))
    return 3;
  // Collect and format data.
  int i = opts.sampling.duration_samples;
  std::optional<const pmt::Snapshot*> snapshot = nullptr;
  while (true) {
    snapshot = source.TakeSnapshot();
    // If there's no more data left, finish.
    if (!snapshot)
      break;
    formatter.Format(**snapshot);
    // Unless we're in continuous dump mode, increment the sample count.
    if (i && --i <= 0)
      break;
    if (opts.sampling.interval_us)
      source.Sleep(opts.sampling.interval_us);
  }
  return 0;
}

}  // namespace pmt_tool

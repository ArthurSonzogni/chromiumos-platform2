// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pmt_tool/pmt_tool.h"

#include <fcntl.h>

#include <cstring>
#include <string>

#include <absl/strings/str_format.h>
#include <absl/time/time.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <libpmt/bits/pmt_data_interface.h>
#include <libpmt/bits/pmt_metadata.h>

namespace pmt_tool {

// Shortcut for frequently used symbols.
using absl::SNPrintF, base::WriteFileDescriptor;

namespace {

// Interface for writing data columns to a given output buffer.
class ColumnWriter {
 public:
  virtual ~ColumnWriter() = default;
  // Writes out a single column for a given value and meta. Returns the number
  // of bytes that would have been written if a full write succeeded or a
  // negative error code (result of a SNPrintF() call).
  virtual int operator()(std::vector<char>& buffer,
                         const pmt::SampleValue& value,
                         const pmt::SampleMetadata& meta,
                         size_t& offset,
                         size_t& left) = 0;
};

bool PrintCsvRow(const pmt::DecodingResult& result,
                 std::vector<char>& buffer,
                 int fd,
                 ColumnWriter& writer,
                 std::string first_column) {
  size_t offset = 0;
  size_t left = buffer.size();
  int would_write = SNPrintF(buffer.data() + offset, left, "%s,", first_column);
  if (UNLIKELY(would_write < 0)) {
    PLOG(ERROR) << "Failed to format 1st column '" << first_column << "'";
    return false;
  }
  // Safety check for an unusually long first column.
  CHECK(would_write < buffer.size() / 2);
  // Shift the buffer after the first column.
  left -= would_write;
  offset += would_write;

  for (int i = 0; i < result.values_.size(); i++) {
    auto& meta = result.meta_[i];
    auto& value = result.values_[i];
    would_write = writer(buffer, value, meta, offset, left);
    if (UNLIKELY(would_write < 0)) {
      PLOG(ERROR) << "Failed to format column for 0x" << std::hex << meta.guid_
                  << "/" << meta.group_ << "/" << meta.name_;
      return false;
    }
    // Buffer filled out, commit and repeat the column.
    if (would_write >= left) {
      if (!WriteFileDescriptor(
              fd, std::string_view(buffer.data(), buffer.size() - left))) {
        PLOG(ERROR) << "Failed to write to output file";
        return false;
      }
      left = buffer.size();
      offset = 0;
      i--;
    } else {  // Or just shift.
      left -= would_write;
      offset += would_write;
    }
  }
  // Flush the rest of the buffer out.
  if (offset > 0) {
    if (!WriteFileDescriptor(
            fd, std::string_view(buffer.data(), buffer.size() - left))) {
      PLOG(ERROR) << "Failed to write to output file";
      return false;
    }
  }
  if (!WriteFileDescriptor(fd, "\n")) {
    PLOG(ERROR) << "Failed to write to output file";
    return false;
  }
  return true;
}

template <typename T,
          const T pmt::SampleMetadata::*Member,
          bool SkipSame = true>
class HeaderWriter : public ColumnWriter {
 public:
  constexpr HeaderWriter() : current_value_() {}
  int operator()(std::vector<char>& buffer,
                 const pmt::SampleValue& value,
                 const pmt::SampleMetadata& meta,
                 size_t& offset,
                 size_t& left) final {
    if (!SkipSame || meta.*Member != current_value_) {
      current_value_ = meta.*Member;
      // Choose the formatter via constexpr for brevity.
      constexpr const char* fmt =
          std::is_same<T, pmt::Guid>::value ? "0x%x," : "\"%s\",";
      return SNPrintF(buffer.data() + offset, left, fmt, current_value_);
    } else {  // Using SNPrintF just for consistency, it's not needed per-se.
      return SNPrintF(buffer.data() + offset, left, ",");
    }
  }

 private:
  T current_value_;
};

class ValueWriter : public ColumnWriter {
 public:
  ValueWriter() = default;
  int operator()(std::vector<char>& buffer,
                 const pmt::SampleValue& value,
                 const pmt::SampleMetadata& meta,
                 size_t& offset,
                 size_t& left) final {
    int would_write = 0;
    std::string column;
    if (meta.type_ == pmt::DataType::FLOAT)
      would_write = SNPrintF(buffer.data() + offset, left, "%f,", value.f_);
    else if (meta.type_ == pmt::DataType::SINT)
      would_write = SNPrintF(buffer.data() + offset, left, "%lld,", value.i64_);
    else
      would_write = SNPrintF(buffer.data() + offset, left, "%llu,", value.u64_);
    return would_write;
  }
};

}  // namespace

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

bool RawFormatter::Format(const pmt::Snapshot& snapshot) {
  // Note that we can use SerializeWithCachedSizes() because
  // snapshot.ByteSizeLong() was called prior to Setup() and calculated the
  // message size (which will not change).
  return snapshot.SerializeToFileDescriptor(fd_);
}

bool DbgFormatter::SetUp(const Options& opts, int fd, size_t snapshot_size) {
  fd_ = fd;

  pmt::LogHeader header;
  header.set_snapshot_size(snapshot_size);
  if (!WriteFileDescriptor(fd_, header.DebugString())) {
    PLOG(ERROR) << "Failed to write to output file";
    return false;
  }
  return true;
}

bool DbgFormatter::Format(const pmt::Snapshot& snapshot) {
  if (!WriteFileDescriptor(fd_, snapshot.DebugString())) {
    PLOG(ERROR) << "Failed to write to output file";
    return false;
  }
  return true;
}

bool CsvFormatter::SetUp(const Options& opts, int fd, size_t snapshot_size) {
  auto guids = decoder_.DetectMetadata();
  LOG_DBG << "Medatada detected for GUIDs:";
  for (auto guid : guids) {
    LOG_DBG << "0x" << std::hex << guid;
  }
  int result = decoder_.SetUpDecoding(guids);
  if (result)
    LOG(ERROR) << "Failed to set up decoding";
  fd_ = fd;
  return true;
}

bool CsvFormatter::Format(const pmt::Snapshot& snapshot) {
  auto result = decoder_.Decode(&snapshot);
  if (!result) {
    LOG(ERROR) << "Failed to decode the PMT snapshot.";
    return false;
  }
  HeaderWriter<pmt::Guid, &pmt::SampleMetadata::guid_> guid_writer;
  HeaderWriter<std::string, &pmt::SampleMetadata::group_> subgroup_writer;
  HeaderWriter<std::string, &pmt::SampleMetadata::name_, false> name_writer;
  HeaderWriter<std::string, &pmt::SampleMetadata::description_, false>
      desc_writer;
  ValueWriter value_writer;

  if (UNLIKELY(print_header_)) {
    if (!PrintCsvRow(*result, buffer_, fd_, guid_writer, "Guid"))
      return false;
    if (!PrintCsvRow(*result, buffer_, fd_, subgroup_writer, "Group"))
      return false;
    if (!PrintCsvRow(*result, buffer_, fd_, desc_writer, "Description"))
      return false;
    if (!PrintCsvRow(*result, buffer_, fd_, name_writer, "Timestamp\\Sample"))
      return false;
    print_header_ = false;
  }
  auto ts = absl::FromUnixMillis(snapshot.timestamp());
  auto tz = absl::UTCTimeZone();
  auto ts_str = absl::FormatTime(ts, tz);
  return PrintCsvRow(*result, buffer_, fd_, value_writer, ts_str);
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
    if (!formatter.Format(**snapshot))
      return 4;
    // Unless we're in continuous dump mode, increment the sample count.
    if (i && --i <= 0)
      break;
    if (opts.sampling.interval_us)
      source.Sleep(opts.sampling.interval_us);
  }
  return 0;
}

}  // namespace pmt_tool

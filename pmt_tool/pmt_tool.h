// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PMT_TOOL_PMT_TOOL_H_
#define PMT_TOOL_PMT_TOOL_H_

#include <memory>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <libpmt/bits/pmt_data.pb.h>
#include <libpmt/pmt.h>

#include "pmt_tool/utils.h"

namespace pmt_tool {

// Interface handling the differences in the input sources.
struct Source {
  virtual ~Source() = default;
  virtual std::optional<const pmt::Snapshot*> TakeSnapshot() = 0;
  virtual size_t GetSnapshotSize() = 0;
  virtual bool SetUp(const Options& options) = 0;
  virtual void Sleep(uint64_t interval_us) { usleep(interval_us); }
};

// Interface handling the PMT data formatting.
struct Formatter {
  virtual ~Formatter() = default;
  virtual bool SetUp(const Options& opts, int fd, size_t snapshot_size) = 0;
  virtual void Format(const pmt::Snapshot& snapshot) = 0;
};

// A PMT data log as a source.
class FileSource : public Source {
 public:
  FileSource() = default;
  ~FileSource();
  bool SetUp(const Options& options) final;
  std::optional<const pmt::Snapshot*> TakeSnapshot() final;
  size_t GetSnapshotSize() final;

 private:
  pmt::Snapshot snapshot_;
  std::unique_ptr<google::protobuf::io::FileInputStream> fis_;
  std::unique_ptr<google::protobuf::io::CodedInputStream> is_;
  int fd_;
  size_t size_;
};

// PMT data sampled using libpmt.
class LibPmtSource : public Source {
 public:
  LibPmtSource() = default;
  bool SetUp(const Options& options) final;
  std::optional<const pmt::Snapshot*> TakeSnapshot() final;
  size_t GetSnapshotSize() final;

 private:
  pmt::PmtCollector collector_;
};

// This formatter output a raw binary log that should be produced by any other
// collector software. Especially important is the LogHeader message that is
// written at the beginning of the log file which allows readers to decode
// each message in series.
class RawFormatter : public Formatter {
 public:
  RawFormatter() = default;
  bool SetUp(const Options& opts, int fd, size_t snapshot_size) final;
  void Format(const pmt::Snapshot& snapshot) final;

 private:
  int fd_;
};

class DbgFormatter : public Formatter {
 public:
  DbgFormatter() = default;
  bool SetUp(const Options& opts, int fd, size_t snapshot_size) final;
  void Format(const pmt::Snapshot& snapshot) final;

 private:
  int fd_;
};

class CsvFormatter : public Formatter {
 public:
  CsvFormatter() = default;
  bool SetUp(const Options& opts, int fd, size_t snapshot_size) final;
  void Format(const pmt::Snapshot& snapshot) final;
};

// Main business logic of pmt_tool, exported for testing.
int do_run(Options& opts, Source& source, Formatter& formatter);

}  // namespace pmt_tool

#endif  // PMT_TOOL_PMT_TOOL_H_

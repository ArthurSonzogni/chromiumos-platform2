// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PMT_TOOL_UTILS_H_
#define PMT_TOOL_UTILS_H_

// Data structures and helper functions used by pmt_tool.

#include <string>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <libpmt/bits/pmt_data.pb.h>

#define PMT_TOOL_LOG_DBG 1
#define LOG_DBG VLOG(PMT_TOOL_LOG_DBG)

namespace pmt_tool {

// Supported output formats.
enum Format {
  // -f=raw: Output the raw binary PMT data as produced by pmt::PmtCollector.
  RAW = 0,
  // -f=csv: Decode into a CSV.
  CSV,
  // -f=dbg: Dump raw data as a protobuf debug string.
  DBG
};

// Run options.
struct Options {
  struct {
    // -i: Converted sampling interval in micro-seconds.
    uint64_t interval_us = 1 * base::Time::kMicrosecondsPerSecond;
    // -n: Sampling duration in number of samples.
    int duration_samples = 0;
    // -t: Sampling duration in number of seconds. Will be rounded up to the
    // next multiple of the interval.
    int duration_seconds = 0;
    // File to read the PMT data from.
    base::FilePath input_file;
  } sampling;
  struct {
    // -f: Output format.
    Format format = Format::RAW;
  } decoding;
};

// Convert string into output format.
inline std::optional<Format> FormatFromString(const std::string& str) {
  std::optional<Format> result;
  if (str == "csv")
    result = Format::CSV;
  if (str == "dbg")
    result = Format::DBG;
  else if (str == "raw")
    result = Format::RAW;
  return result;
}

// Provide a printable representation of the output format.
inline const char* const FormatToString(const Format fmt) {
  switch (fmt) {
    case Format::CSV:
      return "csv";
    case Format::DBG:
      return "dbg";
    case Format::RAW:
      return "raw";
    default:
      return "";
  }
}

// Parse the command line into run options and initialize logging.
//
// Both logging and command line parsing have to be done simultaneously because
// verbose logging initialization depends on the command line being parsed
// already. At the same time it's necessary to initialize the logging to stderr
// as soon as possible, because default logging is to a debug.log file in the
// application directory.
bool ParseCommandLineAndInitLogging(int argc, const char** argv, Options& opts);

}  // namespace pmt_tool

#endif  // PMT_TOOL_UTILS_H_

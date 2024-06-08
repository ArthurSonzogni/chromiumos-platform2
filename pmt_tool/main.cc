// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>

#include "pmt_tool/pmt_tool.h"

int main(int argc, const char* argv[]) {
  // Handle command-line arguments and logging.
  pmt_tool::Options opts;
  if (!ParseCommandLineAndInitLogging(argc, argv, opts)) {
    LOG(ERROR) << "Invalid usage, see --help.";
    return 1;
  }

  bool file_input = !opts.sampling.input_file.empty();

  // Set up data source.
  std::unique_ptr<pmt_tool::Source> source;
  if (file_input) {
    auto fsource = std::make_unique<pmt_tool::FileSource>();
    source.reset(fsource.release());
  } else {
    auto libpmt_source = std::make_unique<pmt_tool::LibPmtSource>();
    source.reset(libpmt_source.release());
  }
  // Set up data formatter.
  std::unique_ptr<pmt_tool::Formatter> formatter;
  switch (opts.decoding.format) {
    case pmt_tool::Format::RAW:
      formatter = std::make_unique<pmt_tool::RawFormatter>();
      break;
    case pmt_tool::Format::DBG:
      formatter = std::make_unique<pmt_tool::DbgFormatter>();
      break;
    case pmt_tool::Format::CSV:
      formatter = std::make_unique<pmt_tool::CsvFormatter>();
      break;
  }
  return do_run(opts, *source, *formatter);
}

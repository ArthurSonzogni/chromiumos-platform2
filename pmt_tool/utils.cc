// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// FIXME: needed because base/files/file.h does a typedef on `struct stat` and
// does not include its definition.
#include "pmt_tool/utils.h"

#include <sys/stat.h>

#include <absl/time/time.h>
#include <base/files/file.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"

namespace pmt_tool {

bool ParseCommandLineAndInitLogging(int argc,
                                    const char** argv,
                                    Options& opts) {
  // Initialize with defaults.
  Options new_opts;

  DEFINE_double(i, 1, "Seconds to wait between samples");
  DEFINE_uint32(n, 0, "Number of samples to take");
  DEFINE_uint32(t, 0, "Sample for the specified number of seconds");
  DEFINE_string(f, "raw",
                "output format: raw - raw binary format; dbg - debug string; "
                "csv - decoded as CSV from raw binary");
  DEFINE_string(
      m, "",
      "Optional path to the PMT metadata directory where pmt.xml is located");

  auto help_usage = std::string(argv[0]);
  help_usage.append(
      " [OPTIONS] [-- [FILE]]\n"
      "Sample and decode Intel PMT telemetry to stdout.\n"
      "By default samples will be gathered continuously every -i seconds.\n"
      "If FILE path is provided, all samples are read from it.\n"
      "Note that -t and -n flags are mutually exclusive.\n"
      "OPTIONS:");

  bool result = brillo::FlagHelper::Init(
      argc, argv, help_usage, brillo::FlagHelper::InitFuncType::kReturn,
      nullptr);
  // Setup logging now that command line was parsed and brillo can process
  // verbosity flags.
  brillo::InitLog(brillo::kLogToStderr);

  // Exit early if parsing failed.
  if (!result)
    return result;

  // Validate flags.
  if (FLAGS_t > 0 && FLAGS_n) {
    LOG(ERROR) << "-t and -n are mutually exclusive";
    return false;
  }
  if (FLAGS_t && FLAGS_t < FLAGS_i) {
    LOG(ERROR) << "-t cannot be lower than -i";
    return false;
  }
  auto val = FormatFromString(FLAGS_f);
  if (!val.has_value()) {
    LOG(ERROR) << "Unknown format: " << FLAGS_f;
    return false;
  }
  auto metadata_path = base::FilePath(FLAGS_m);
  if (!FLAGS_m.empty() && !base::PathExists(metadata_path)) {
    LOG(ERROR) << "Metadata directory " << FLAGS_m << " not found";
    return false;
  }
  // Flags are valid, set them.
  new_opts.sampling.interval_us =
      absl::ToInt64Microseconds(absl::Seconds(FLAGS_i));
  new_opts.sampling.duration_seconds = std::ceil(FLAGS_t / FLAGS_i) * FLAGS_i;
  if (FLAGS_t != 0)
    new_opts.sampling.duration_samples =
        1 + new_opts.sampling.duration_seconds / FLAGS_i;
  else
    new_opts.sampling.duration_samples = FLAGS_n;

  new_opts.decoding.format = *val;
  new_opts.decoding.metadata_path = metadata_path;

  // Out of the rest of arguments treat the fist one as a path to the pmt.log.
  auto cl = base::CommandLine::ForCurrentProcess();
  if (!cl->GetArgs().empty()) {
    if (cl->GetArgs().size() > 1) {
      LOG(ERROR) << "Only a single input file is supported.";
      return false;
    }
    new_opts.sampling.input_file = base::FilePath(cl->GetArgs()[0]);
    base::stat_wrapper_t stat;
    errno = 0;
    if (base::File::Stat(new_opts.sampling.input_file, &stat)) {
      LOG(ERROR) << "Failed to open input file: " << strerror(errno);
      return false;
    }
  }

  // Values are good so set the output variable.
  opts = new_opts;
  return true;
}

}  // namespace pmt_tool

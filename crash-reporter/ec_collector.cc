// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/ec_collector.h"

#include <memory>
#include <string>
#include <vector>

#include <base/base64.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>
#include <libec/ec_command.h>
#include <libec/ec_panicinfo.h>
#include <metrics/metrics_library.h>
#include <re2/re2.h>

#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/crash_collector_names.h"
#include "crash-reporter/util.h"

using base::FilePath;
using base::StringPrintf;

using brillo::ProcessImpl;

namespace {

const char kECDebugFSPath[] = "/sys/kernel/debug/cros_ec/";
const char kECPanicInfo[] = "panicinfo";
const char kECExecName[] = "embedded-controller";
const char kECLibFSPath[] = "/var/spool/cros_ec/";
const char kECCoredump[] = "coredump";

}  // namespace

ECCollector::ECCollector(
    const scoped_refptr<
        base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
        metrics_lib)
    : CrashCollector(CrashReporterCollector::kEC, metrics_lib),
      debugfs_path_(kECDebugFSPath),
      libfs_path_(kECLibFSPath) {}

ECCollector::~ECCollector() {}

bool ECCollector::Collect(bool use_saved_lsb) {
  SetUseSavedLsb(use_saved_lsb);

  FilePath panicinfo_path = debugfs_path_.Append(kECPanicInfo);
  if (!base::PathExists(panicinfo_path)) {
    return false;
  }

  char panicinfo_data[1024];
  int panicinfo_len =
      base::ReadFile(panicinfo_path, panicinfo_data, sizeof(panicinfo_data));

  if (panicinfo_len < 0) {
    PLOG(ERROR) << "Unable to open " << panicinfo_path.value();
    return false;
  }

  if (panicinfo_len <= PANIC_DATA_FLAGS_BYTE) {
    LOG(ERROR) << "EC panicinfo is too short (" << panicinfo_len << " bytes).";
    return false;
  }

  // Check if the EC crash has already been fetched before, in a previous AP
  // boot (EC sets this flag when the AP fetches the panic information).
  if (panicinfo_data[PANIC_DATA_FLAGS_BYTE] & PANIC_DATA_FLAG_OLD_HOSTCMD) {
    LOG(INFO) << "Stale EC crash: already fetched, not reporting.";
    return false;
  }

  LOG(INFO) << "Received crash notification from EC (handling)";
  FilePath root_crash_directory;
  if (!IsSuccessCode(
          GetCreatedCrashDirectoryByEuid(0, &root_crash_directory, nullptr))) {
    return true;
  }

  base::span<uint8_t> panicinfo_sdata =
      base::span(reinterpret_cast<uint8_t*>(panicinfo_data),
                 static_cast<size_t>(panicinfo_len));
  auto result = ec::ParsePanicInfo(panicinfo_sdata);
  std::string output;

  if (!result.has_value()) {
    LOG(ERROR) << "Failed to get valid eccrash. Error=" << result.error();
    return false;
  } else {
    output = result.value();
  }

  std::string dump_basename = FormatDumpBasename(kECExecName, time(nullptr), 0);
  FilePath ec_crash_path = root_crash_directory.Append(
      StringPrintf("%s.eccrash", dump_basename.c_str()));
  FilePath log_path = root_crash_directory.Append(
      StringPrintf("%s.log", dump_basename.c_str()));
  FilePath coredump_gz_path = root_crash_directory.Append(
      StringPrintf("%s.coredump.gz", dump_basename.c_str()));
  FilePath panic_log_path = root_crash_directory.Append(
      StringPrintf("%s.panic.log", dump_basename.c_str()));

  // We must use WriteNewFile instead of base::WriteFile as we
  // do not want to write with root access to a symlink that an attacker
  // might have created.
  if (WriteNewFile(ec_crash_path, output) != static_cast<int>(output.size())) {
    PLOG(ERROR) << "Failed to write EC register dump to "
                << ec_crash_path.value().c_str();
    return true;
  }

  // Check for associated EC coredump
  FilePath coredump_path = libfs_path_.Append(kECCoredump);
  FilePath coredump_panicinfo_path = libfs_path_.Append(kECPanicInfo);
  std::string coredump_data;
  std::string coredump_panicinfo_data;
  if (base::PathExists(coredump_path) &&
      base::PathExists(coredump_panicinfo_path) &&
      base::ReadFileToString(coredump_panicinfo_path,
                             &coredump_panicinfo_data) &&
      base::ReadFileToString(coredump_path, &coredump_data)) {
    // Compare the coredump panicinfo with the recent crash panicinfo. Ignore
    // PANIC_DATA_FLAGS_BYTE since the flags will differ depending on when
    // the panicinfo is read. If they do not match then the coredump is not
    // associated with the most recent EC crash and should be ignored.
    if (coredump_panicinfo_data.length() != panicinfo_len ||
        memcmp(coredump_panicinfo_data.data(), panicinfo_data,
               PANIC_DATA_FLAGS_BYTE) ||
        memcmp(coredump_panicinfo_data.data() + PANIC_DATA_FLAGS_BYTE + 1,
               panicinfo_data + PANIC_DATA_FLAGS_BYTE + 1,
               panicinfo_len - PANIC_DATA_FLAGS_BYTE - 1)) {
      LOG(WARNING) << "Coredump panicinfo does not match recent crash panicinfo"
                      ", ignoring coredump.";
    } else {
      if (WriteNewCompressedFile(coredump_gz_path, coredump_data.c_str(),
                                 coredump_data.size())) {
        AddCrashMetaUploadFile("coredump", coredump_gz_path.BaseName().value());
      } else {
        LOG(ERROR) << "Failed to write EC coredump to "
                   << coredump_path.value().c_str();
      }
    }
  }

  // Fetch EC panic log and append to report if it exists
  std::string panic_log_dump;
  if (FetchEcPanicLog(panic_log_dump)) {
    if (WriteNewFile(panic_log_path, panic_log_dump)) {
      LOG(INFO) << "EC panic log written to " << panic_log_path.value();
      AddCrashMetaUploadFile("panic_log", panic_log_path.BaseName().value());
    } else {
      LOG(ERROR) << "Failed to write EC panic log to "
                 << panic_log_path.value().c_str();
    }
  }

  std::string signature = StringPrintf(
      "%s-%08X", kECExecName,
      util::HashString(std::string_view(panicinfo_data, panicinfo_len)));

  AddCrashMetaData("sig", signature);
  // Add EC info and AP version into log file.
  if (IsSuccessCode(GetLogContents(log_config_path_, kECExecName, log_path))) {
    AddCrashMetaUploadFile("log", log_path.BaseName().value());
  }
  FinishCrash(root_crash_directory.Append(
                  StringPrintf("%s.meta", dump_basename.c_str())),
              kECExecName, ec_crash_path.BaseName().value());

  LOG(INFO) << "Stored EC crash to " << ec_crash_path.value();

  return true;
}

CrashCollector::ComputedCrashSeverity ECCollector::ComputeSeverity(
    const std::string& exec_name) {
  return ComputedCrashSeverity{
      .crash_severity = CrashSeverity::kFatal,
      .product_group = Product::kPlatform,
  };
}

int ECCollector::RunEctoolCmd(const std::vector<std::string>& args,
                              std::string* output) {
  ProcessImpl ectool_cmd;
  std::string null_output;

  ectool_cmd.AddArg("/usr/sbin/ectool");
  for (std::string arg : args) {
    ectool_cmd.AddArg(arg);
  }
  if (output == nullptr) {
    output = &null_output;
  }
  return util::RunAndCaptureOutput(&ectool_cmd, STDOUT_FILENO, output);
}

bool ECCollector::FetchEcPanicLog(std::string& panic_log_dump) {
  bool success = false;
  std::string panic_log_info_output;
  std::string panic_log_length_str;
  int panic_log_length;

  LOG(INFO) << "Fetching EC panic log";

  if (RunEctoolCmd({"paniclog", "info"}, &panic_log_info_output) != 0) {
    LOG(INFO) << "EC Panic Log is not supported";
    return false;
  }

  if (!RE2::PartialMatch(panic_log_info_output, "(?im)^Frozen:\\s*1$")) {
    LOG(INFO) << "EC Panic Log is not available";
    return false;
  }

  if (!RE2::PartialMatch(panic_log_info_output, "(?im)^Valid:\\s*1$")) {
    LOG(INFO) << "EC Panic Log is not valid";
    goto reset_panic_log;
  }

  if (!RE2::PartialMatch(panic_log_info_output, "(?im)^Length:\\s*(\\d+)$",
                         &panic_log_length_str)) {
    LOG(ERROR) << "Failed to parse EC panic log length";
    goto reset_panic_log;
  }
  panic_log_length = std::stoi(panic_log_length_str);
  if (!panic_log_length) {
    LOG(INFO) << "EC Panic Log is empty";
    goto reset_panic_log;
  }

  LOG(INFO) << "Dumping EC panic log";
  if (RunEctoolCmd({"paniclog", "dump"}, &panic_log_dump) != 0) {
    LOG(ERROR) << "Failed to dump EC panic log";
    goto reset_panic_log;
  }
  success = true;

reset_panic_log:

  /* Panic log should be reset after dumping or when invalid */

  LOG(INFO) << "Resetting EC panic log";

  if (RunEctoolCmd({"paniclog", "reset"}, nullptr) != 0) {
    LOG(INFO) << "Failed to reset EC panic log";
  }

  if (RunEctoolCmd({"paniclog", "unfreeze"}, nullptr) != 0) {
    LOG(INFO) << "Failed to unfreeze EC panic log";
  }

  return success;
}

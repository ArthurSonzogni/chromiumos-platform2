// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/ec_collector.h"

#include <string>

#include <base/base64.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>

#include "crash-reporter/util.h"

using base::FilePath;
using base::StringPiece;
using base::StringPrintf;

using brillo::ProcessImpl;

namespace {

const char kECDebugFSPath[] = "/sys/kernel/debug/cros_ec/";
const char kECPanicInfo[] = "panicinfo";
const char kECPanicInfoParser[] = "/usr/sbin/ec_parse_panicinfo";
const char kECExecName[] = "embedded-controller";

}  // namespace

ECCollector::ECCollector()
    : CrashCollector("ec"), debugfs_path_(kECDebugFSPath) {}

ECCollector::~ECCollector() {}

bool ECCollector::Collect() {
  char data[1024];
  int len;
  FilePath panicinfo_path = debugfs_path_.Append(kECPanicInfo);
  FilePath root_crash_directory;

  if (!base::PathExists(panicinfo_path)) {
    return false;
  }

  len = base::ReadFile(panicinfo_path, data, sizeof(data));

  if (len < 0) {
    PLOG(ERROR) << "Unable to open " << panicinfo_path.value();
    return false;
  }

  if (len <= PANIC_DATA_FLAGS_BYTE) {
    LOG(ERROR) << "EC panicinfo is too short (" << len << " bytes).";
    return false;
  }

  // Check if the EC crash has already been fetched before, in a previous AP
  // boot (EC sets this flag when the AP fetches the panic information).
  if (data[PANIC_DATA_FLAGS_BYTE] & PANIC_DATA_FLAG_OLD_HOSTCMD) {
    LOG(INFO) << "Stale EC crash: already fetched, not reporting.";
    return false;
  }

  LOG(INFO) << "Received crash notification from EC (handling)";

  if (!GetCreatedCrashDirectoryByEuid(0, &root_crash_directory, nullptr)) {
    return true;
  }

  ProcessImpl panicinfo_parser;
  panicinfo_parser.AddArg(kECPanicInfoParser);
  panicinfo_parser.RedirectInput(panicinfo_path.value());
  // Combine stdout and stderr.
  panicinfo_parser.RedirectOutputToMemory(true);

  std::string output;
  const int result = panicinfo_parser.Run();
  output = panicinfo_parser.GetOutputString(STDOUT_FILENO);
  if (result != 0) {
    PLOG(ERROR) << "Failed to run ec_parse_panicinfo. Error=" << result;
    // Append error code and raw crash on error.
    output = base::StrCat(
        {output,
         base::StringPrintf(
             "\nERROR: ec_parse_panicinfo: Non-zero return value (%d).\n",
             result),
         "Raw data: ",
         base::Base64Encode(
             base::make_span(reinterpret_cast<uint8_t*>(data), len)),
         "\n"});
  }

  std::string dump_basename = FormatDumpBasename(kECExecName, time(nullptr), 0);
  FilePath ec_crash_path = root_crash_directory.Append(
      StringPrintf("%s.eccrash", dump_basename.c_str()));
  FilePath log_path = root_crash_directory.Append(
      StringPrintf("%s.log", dump_basename.c_str()));

  // We must use WriteNewFile instead of base::WriteFile as we
  // do not want to write with root access to a symlink that an attacker
  // might have created.
  if (WriteNewFile(ec_crash_path, output) != static_cast<int>(output.size())) {
    PLOG(ERROR) << "Failed to write EC dump to "
                << ec_crash_path.value().c_str();
    return true;
  }

  std::string signature = StringPrintf(
      "%s-%08X", kECExecName, util::HashString(StringPiece(data, len)));

  AddCrashMetaData("sig", signature);
  // Add EC info and AP version into log file.
  if (GetLogContents(log_config_path_, kECExecName, log_path)) {
    AddCrashMetaUploadFile("log", log_path.BaseName().value());
  }
  FinishCrash(root_crash_directory.Append(
                  StringPrintf("%s.meta", dump_basename.c_str())),
              kECExecName, ec_crash_path.BaseName().value());

  LOG(INFO) << "Stored EC crash to " << ec_crash_path.value();

  return true;
}

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

namespace {

// Path to the Intel pmc_core driver sysfs interface, if it doesn't exist,
// either the kernel is old w/o it, or it is not configured.
constexpr const char kPmcCorePath[] = "/sys/kernel/debug/pmc_core";

}  // namespace

void SetLtrIgnore(const std::string& ip_index) {
  base::FilePath pmc_core_file_path(kPmcCorePath);
  base::FilePath ltr_ignore_file_path = pmc_core_file_path.Append("ltr_ignore");
  PCHECK(base::PathExists(ltr_ignore_file_path))
      << "No interface to ignore ltr, couldn't find "
      << ltr_ignore_file_path.value();

  if (!base::WriteFile(ltr_ignore_file_path, ip_index.c_str())) {
    PLOG(ERROR) << "Failed to write " << ip_index << " to "
                << ltr_ignore_file_path;
  }
}

int main(int argc, char** argv) {
  DEFINE_string(ltr_ignore, "", "The ip ltr would be ignored.");
  brillo::FlagHelper::Init(
      argc, argv, "Execute command before/after suspend for Intel SoCs");

  brillo::InitLog(brillo::kLogToStderr);

  if (!FLAGS_ltr_ignore.empty())
    SetLtrIgnore(FLAGS_ltr_ignore);

  return 0;
}

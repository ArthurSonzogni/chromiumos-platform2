// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

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

bool is_sighting_alert(void) {
  base::FilePath pmc_core_file_path(kPmcCorePath);
  base::FilePath substate_sts_path;

  // (b/271527450): Intel sighting alert 772439
  substate_sts_path = pmc_core_file_path.Append("substate_status_registers");
  if (base::PathExists(substate_sts_path)) {
    std::ifstream file(substate_sts_path.value());
    std::string_view lpm_sts_0;
    std::string line;

    /*********************************************************************
     * Search 'PMC0:LPM_STATUS_0' and get the register value for checking.
     * EX: "PMC0:LPM_STATUS_0:   0xf57c0074", check 0xf57c0074
     *********************************************************************
     */
    while (std::getline(file, line)) {
      if (line.find("PMC0:LPM_STATUS_0") == std::string::npos) {
        continue;
      }

      size_t pos = line.find("0x");
      if (pos != std::string::npos) {
        lpm_sts_0 = line.substr(pos);
      }
      break;
    }

    if (lpm_sts_0 == "0xf57c0074" || lpm_sts_0 == "0xf57c00f4") {
      printf("CNVi Sighting Alert 772439!\n");
      return true;
    }
  }

  return false;
}

void SetLtrIgnore(const std::string_view ip_index) {
  base::FilePath pmc_core_file_path(kPmcCorePath);
  base::FilePath ltr_ignore_file_path = pmc_core_file_path.Append("ltr_ignore");
  PCHECK(base::PathExists(ltr_ignore_file_path))
      << "No interface to ignore ltr, couldn't find "
      << ltr_ignore_file_path.value();

  if (!base::WriteFile(ltr_ignore_file_path, std::string(ip_index))) {
    PLOG(ERROR) << "Failed to write " << ip_index << " to "
                << ltr_ignore_file_path;
  }
}

void exe_boardwa(const std::string_view brd) {
  // Ignore CNVi LTR, it's cross-platform case.
  SetLtrIgnore("10");

  if (brd == "ovis") {
    // Ignore LAN
    SetLtrIgnore("1");
    SetLtrIgnore("40");
  }
}

int main(int argc, char** argv) {
  DEFINE_string(ltr_ignore, "", "The ip ltr would be ignored.");
  DEFINE_string(boardwa, "", "Execute board projects related workaround.");
  DEFINE_bool(sighting_check, false, "Check if it is any known sighting case");
  brillo::FlagHelper::Init(
      argc, argv, "Execute command before/after suspend for Intel SoCs");

  brillo::InitLog(brillo::kLogToStderr);

  if (!FLAGS_ltr_ignore.empty()) {
    SetLtrIgnore(FLAGS_ltr_ignore);
  }

  if (!FLAGS_boardwa.empty()) {
    exe_boardwa(FLAGS_boardwa);
  }

  if (FLAGS_sighting_check) {
    return !is_sighting_alert();
  }

  return 0;
}

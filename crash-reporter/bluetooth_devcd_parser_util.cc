// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/bluetooth_devcd_parser_util.h"

#include <vector>

#include <base/containers/span.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "crash-reporter/udev_bluetooth_util.h"
#include "crash-reporter/util.h"

namespace {

constexpr char kCoredumpMetaHeader[] = "Bluetooth devcoredump";
constexpr char kCoredumpDataHeader[] = "--- Start dump ---";
constexpr char kCoredumpDefaultPC[] = "00000000";
const std::vector<std::string> kCoredumpState = {
    "Devcoredump Idle",  "Devcoredump Active",  "Devcoredump Complete",
    "Devcoredump Abort", "Devcoredump Timeout",
};

std::string CreateDumpEntry(const std::string& key, const std::string& value) {
  return base::StrCat({key, "=", value, "\n"});
}

int64_t GetDumpPos(base::File& file) {
  return file.Seek(base::File::FROM_CURRENT, 0);
}

bool ReportDefaultPC(base::File& file, std::string* pc) {
  *pc = kCoredumpDefaultPC;
  std::string line = CreateDumpEntry("PC", kCoredumpDefaultPC);
  if (!file.WriteAtCurrentPosAndCheck(base::as_bytes(base::make_span(line)))) {
    return false;
  }
  return true;
}

// Cannot use base::file_util::CopyFile() here as it copies the entire file,
// whereas SaveDumpData() needs to copy only the part of the file.
bool SaveDumpData(const base::FilePath& coredump_path,
                  const base::FilePath& target_path,
                  int64_t dump_start) {
  // Overwrite if the output file already exists. It makes more sense for the
  // parser binary as a standalone tool to overwrite than to fail when a file
  // exists.
  base::File target_file(
      target_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  std::string coredump_content;
  if (!base::ReadFileToString(coredump_path, &coredump_content)) {
    PLOG(ERROR) << "Error reading coredump file " << coredump_path;
    return false;
  }

  if (!target_file.WriteAtCurrentPosAndCheck(base::as_bytes(base::make_span(
          coredump_content.substr(dump_start, std::string::npos))))) {
    PLOG(ERROR) << "Error writing to target file " << target_path;
    return false;
  }

  LOG(INFO) << "Binary devcoredump data: " << target_path;

  return true;
}

bool ParseDumpHeader(const base::FilePath& coredump_path,
                     const base::FilePath& target_path,
                     int64_t* data_pos,
                     std::string* driver_name,
                     std::string* vendor_name,
                     std::string* controller_name) {
  base::File dump_file(coredump_path,
                       base::File::FLAG_OPEN | base::File::FLAG_READ);
  // Overwrite if the output file already exists. It makes more sense for the
  // parser binary as a standalone tool to overwrite than to fail when a file
  // exists.
  base::File target_file(
      target_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  std::string line;

  if (!dump_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << coredump_path << " Error: "
               << base::File::ErrorToString(dump_file.error_details());
    return false;
  }

  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  while (util::GetNextLine(dump_file, line)) {
    if (line[0] == '\0') {
      // After updating the devcoredump state, the Bluetooth HCI Devcoredump
      // API adds a '\0' at the end. Remove it before splitting the line.
      line.erase(0, 1);
    }
    if (line == kCoredumpMetaHeader) {
      // Skip the header
      continue;
    }
    if (line == kCoredumpDataHeader) {
      // End of devcoredump header fields
      break;
    }

    std::vector<std::string> fields = SplitString(
        line, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (fields.size() < 2) {
      LOG(ERROR) << "Invalid bluetooth devcoredump header line: " << line;
      return false;
    }

    std::string& key = fields[0];
    std::string& value = fields[1];

    if (key == "State") {
      int state;
      if (base::StringToInt(value, &state) && state >= 0 &&
          state < kCoredumpState.size()) {
        value = kCoredumpState[state];
      }
    } else if (key == "Driver") {
      *driver_name = value;
    } else if (key == "Vendor") {
      *vendor_name = value;
    } else if (key == "Controller Name") {
      *controller_name = value;
    }

    if (!target_file.WriteAtCurrentPosAndCheck(
            base::as_bytes(base::make_span(CreateDumpEntry(key, value))))) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
  }

  *data_pos = GetDumpPos(dump_file);

  if (driver_name->empty() || vendor_name->empty() ||
      controller_name->empty()) {
    // If any of the required fields are missing, close the target file and
    // delete it.
    target_file.Close();
    if (!base::DeleteFile(target_path)) {
      LOG(ERROR) << "Error deleting file " << target_path;
    }
    return false;
  }

  return true;
}

bool ParseDumpData(const base::FilePath& coredump_path,
                   const base::FilePath& target_path,
                   const int64_t dump_start,
                   const std::string& vendor_name,
                   std::string* pc,
                   const bool save_dump_data) {
  if (save_dump_data) {
    // Save a copy of dump data on developer image. This is not attached with
    // the crash report, used only for development purpose.
    if (!SaveDumpData(coredump_path, target_path.ReplaceExtension("data"),
                      dump_start)) {
      LOG(ERROR) << "Error saving bluetooth devcoredump data";
    }
  }

  // TODO(b/154866851): Implement vendor specific devcoredump parser

  LOG(WARNING) << "Unsupported bluetooth devcoredump vendor - " << vendor_name;

  // Since no supported vendor found, use the default value for PC and
  // return true to report the crash event.
  base::File target_file(target_path,
                         base::File::FLAG_OPEN | base::File::FLAG_APPEND);
  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  if (!ReportDefaultPC(target_file, pc)) {
    PLOG(ERROR) << "Error writing to target file " << target_path;
    return false;
  }

  return true;
}

}  // namespace

namespace bluetooth_util {

bool ParseBluetoothCoredump(const base::FilePath& coredump_path,
                            const base::FilePath& output_dir,
                            const bool save_dump_data,
                            std::string* crash_sig) {
  std::string driver_name;
  std::string vendor_name;
  std::string controller_name;
  int64_t data_pos;
  std::string pc;

  LOG(INFO) << "Input coredump path: " << coredump_path;

  base::FilePath target_path = coredump_path.ReplaceExtension("txt");
  if (!output_dir.empty()) {
    LOG(INFO) << "Output dir: " << output_dir;
    target_path = output_dir.Append(target_path.BaseName());
  }
  LOG(INFO) << "Parsed coredump path: " << target_path;

  if (!ParseDumpHeader(coredump_path, target_path, &data_pos, &driver_name,
                       &vendor_name, &controller_name)) {
    LOG(ERROR) << "Error parsing bluetooth devcoredump header";
    return false;
  }

  if (!ParseDumpData(coredump_path, target_path, data_pos, vendor_name, &pc,
                     save_dump_data)) {
    LOG(ERROR) << "Error parsing bluetooth devcoredump data";
    return false;
  }

  *crash_sig = bluetooth_util::CreateCrashSig(driver_name, vendor_name,
                                              controller_name, pc);

  return true;
}

}  // namespace bluetooth_util

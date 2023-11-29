// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/mmc_error_parser.h"

#include <sstream>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_number_conversions.h>

#include "metrics/debugd_reader.h"

namespace chromeos_metrics {

MmcErrorParser::MmcErrorParser(const base::FilePath& persistent_dir,
                               const base::FilePath& runtime_dir,
                               std::unique_ptr<DebugdReader> reader,
                               std::string_view name)
    : reader_(std::move(reader)), name_(name) {
  base::FilePath dir = persistent_dir.Append(name);

  // Should be created in the factory method.
  CHECK(base::DirectoryExists(dir));
  data_timeouts_ =
      std::make_unique<PersistentInteger>(dir.Append(kDataTimeoutName));
  cmd_timeouts_ =
      std::make_unique<PersistentInteger>(dir.Append(kCmdTimeoutName));
  data_crcs_ = std::make_unique<PersistentInteger>(dir.Append(kDataCRCName));
  cmd_crcs_ = std::make_unique<PersistentInteger>(dir.Append(kCmdCRCName));

  dir = runtime_dir.Append(name);

  // Should be created in the factory method.
  CHECK(base::DirectoryExists(dir));
  data_timeouts_since_boot_ =
      std::make_unique<PersistentInteger>(dir.Append(kDataTimeoutName));
  cmd_timeouts_since_boot_ =
      std::make_unique<PersistentInteger>(dir.Append(kCmdTimeoutName));
  data_crcs_since_boot_ =
      std::make_unique<PersistentInteger>(dir.Append(kDataCRCName));
  cmd_crcs_since_boot_ =
      std::make_unique<PersistentInteger>(dir.Append(kCmdCRCName));
}

std::unique_ptr<MmcErrorParser> MmcErrorParser::Create(
    const base::FilePath& persistent_dir,
    const base::FilePath& runtime_dir,
    std::unique_ptr<DebugdReader> reader,
    std::string_view name) {
  base::FilePath dir = persistent_dir.Append(name);
  if (!base::CreateDirectory(dir)) {
    PLOG(ERROR) << "Failed to create " << dir;
    return nullptr;
  }

  dir = runtime_dir.Append(name);
  if (!base::CreateDirectory(dir)) {
    PLOG(ERROR) << "Failed to create " << dir;
    return nullptr;
  }

  return std::unique_ptr<MmcErrorParser>(
      new MmcErrorParser(persistent_dir, runtime_dir, std::move(reader), name));
}

void MmcErrorParser::Update() {
  std::optional<std::istringstream> input(reader_->Read());
  if (!input) {
    return;
  }

  // Log returned from debugd contains counters for all controllers present
  // in the system. The one we're interested in will have a "header" with its
  // name included. Move the stream forward until the line that follows the
  // "header".
  for (std::string line; std::getline(*input, line);) {
    if (line.find(name_) != std::string::npos) {
      break;
    }
  }

  for (std::string line; std::getline(*input, line);) {
    // Debugd separates entries for different controllers with an empty line.
    if (line.empty()) {
      break;
    }

    // Each counter is expected to on a separate line - "Error name: 123"
    std::vector<std::string> tokens = base::SplitString(
        line, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (tokens.size() != 2) {
      DLOG(WARNING) << "Unexpected format in line: " << line;
      continue;
    }

    int val;
    if (!base::StringToInt(tokens[1], &val)) {
      DLOG(WARNING) << "Failed to parse: " << tokens[1];
      continue;
    }

    // Calculate how many previously unseen errors we have for each tracked
    // error. To do that subtract the amount we've seen in this boot from the
    // value reported by the kernel.
    if (tokens[0].find("Command Timeout Occurred") != std::string::npos) {
      // Make sure we don't overflow the counter. This shouldn't happen, but
      // better to be on the safe side.
      if (val > cmd_timeouts_since_boot_->Get()) {
        cmd_timeouts_->Add(val - cmd_timeouts_since_boot_->Get());
      }
      cmd_timeouts_since_boot_->Set(val);
    } else if (tokens[0].find("Command CRC Errors Occurred") !=
               std::string::npos) {
      if (val > cmd_crcs_since_boot_->Get()) {
        cmd_crcs_->Add(val - cmd_crcs_since_boot_->Get());
      }
      cmd_crcs_since_boot_->Set(val);
    } else if (tokens[0].find("Data Timeout Occurred") != std::string::npos) {
      if (val > data_timeouts_since_boot_->Get()) {
        data_timeouts_->Add(val - data_timeouts_since_boot_->Get());
      }
      data_timeouts_since_boot_->Set(val);
    } else if (tokens[0].find("Data CRC Errors Occurred") !=
               std::string::npos) {
      if (val > data_crcs_since_boot_->Get()) {
        data_crcs_->Add(val - data_crcs_since_boot_->Get());
      }
      data_crcs_since_boot_->Set(val);
    }
  }
}

MmcErrorRecord MmcErrorParser::GetAndClear() {
  MmcErrorRecord record;

  record.cmd_timeouts = cmd_timeouts_->GetAndClear();
  record.data_timeouts = data_timeouts_->GetAndClear();
  record.cmd_crcs = cmd_crcs_->GetAndClear();
  record.data_crcs = data_crcs_->GetAndClear();

  return record;
}
}  // namespace chromeos_metrics

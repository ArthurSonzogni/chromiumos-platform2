// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm/tpm_clear.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/files/file_util.h>
#include <libcrossystem/crossystem.h>

namespace {

#if USE_TPM2_SIMULATOR

constexpr char kNVChipPath[] =
    "/mnt/stateful_partition/unencrypted/tpm2-simulator/NVChip";

#elif USE_TPM_DYNAMIC

constexpr char kTpmPpiPath[] = "/sys/class/tpm/tpm0/ppi/request";
constexpr char kTpmTcgOpPath[] = "/sys/class/tpm/tpm0/ppi/tcg_operations";
constexpr int kTpmPpiNothingId = 0;
constexpr int kTpmPpiClearId = 22;

bool WriteStringToFile(const base::FilePath& filename,
                       const std::string& data) {
  int result = base::WriteFile(filename, data.data(), data.size());
  return (result != -1 && static_cast<size_t>(result) == data.size());
}

std::map<int, int> GetTpmTcgOpMap() {
  std::string data;
  if (!base::ReadFileToString(base::FilePath(kTpmTcgOpPath), &data)) {
    return {};
  }

  std::map<int, int> result;
  std::vector<std::string> lines = base::SplitString(
      data, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (const std::string& line : lines) {
    int id = 0;
    int value = 0;
    if (sscanf(line.c_str(), "%d %d: ", &id, &value) != 2) {
      continue;
    }
    result.insert({id, value});
  }

  return result;
}

#endif

}  // namespace

namespace hwsec_foundation::tpm {

#if USE_TPM2_SIMULATOR

bool SupportClearRequest() {
  return true;
}

bool SupportClearWithoutPrompt() {
  return true;
}

bool SetClearTpmRequest(bool value) {
  // We don't support to set "Clear TPM Request" back to false on VM.
  if (value == false) {
    return false;
  }

  return brillo::DeleteFile(base::FilePath(kNVChipPath));
}

bool SetClearTpmRequestAllowPrompt(bool value) {
  return SetClearTpmRequest(value);
}

std::optional<bool> GetClearTpmRequest() {
  return !base::PathExists(base::FilePath(kNVChipPath));
}

#elif USE_TPM_DYNAMIC

bool SupportClearRequest() {
  std::map<int, int> op_map = GetTpmTcgOpMap();
  int op_value = op_map[kTpmPpiClearId];
  return op_value == 3 || op_value == 4;
}

bool SupportClearWithoutPrompt() {
  std::map<int, int> op_map = GetTpmTcgOpMap();
  int op_value = op_map[kTpmPpiClearId];
  return op_value == 4;
}

bool SetClearTpmRequest(bool value) {
  if (value == true && !SupportClearWithoutPrompt()) {
    return false;
  }

  return SetClearTpmRequestAllowPrompt(value);
}

bool SetClearTpmRequestAllowPrompt(bool value) {
  std::string ppi_id =
      value ? std::to_string(kTpmPpiClearId) : std::to_string(kTpmPpiNothingId);

  if (!WriteStringToFile(base::FilePath(kTpmPpiPath), ppi_id)) {
    return false;
  }

  return true;
}

std::optional<bool> GetClearTpmRequest() {
  if (!SupportClearWithoutPrompt()) {
    return std::nullopt;
  }

  std::string data;
  if (!base::ReadFileToString(base::FilePath(kTpmPpiPath), &data)) {
    return std::nullopt;
  }

  auto request = base::TrimWhitespaceASCII(data, base::TRIM_ALL);

  if (request == std::to_string(kTpmPpiClearId)) {
    return true;
  }

  if (request == std::to_string(kTpmPpiNothingId)) {
    return false;
  }

  return std::nullopt;
}

#else

bool SupportClearRequest() {
  return true;
}

bool SupportClearWithoutPrompt() {
  return true;
}

bool SetClearTpmRequest(bool value) {
  auto crossystem = std::make_unique<crossystem::Crossystem>();
  return crossystem->VbSetSystemPropertyInt(
      crossystem::Crossystem::kClearTpmOwnerRequest, static_cast<int>(value));
}

bool SetClearTpmRequestAllowPrompt(bool value) {
  return SetClearTpmRequest(value);
}

std::optional<bool> GetClearTpmRequest() {
  auto crossystem = std::make_unique<crossystem::Crossystem>();
  std::optional<int> result = crossystem->VbGetSystemPropertyInt(
      crossystem::Crossystem::kClearTpmOwnerRequest);
  if (!result.has_value()) {
    return std::nullopt;
  }
  return *result;
}

#endif

}  // namespace hwsec_foundation::tpm

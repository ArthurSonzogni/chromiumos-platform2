// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/runtime_hwid_utils/runtime_hwid_utils_impl.h"

#include <memory>
#include <optional>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/hash/sha1.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <brillo/files/file_util.h>
#include <libcrossystem/crossystem.h>

namespace hardware_verifier {

namespace {

constexpr char kCrosSystemHWIDKey[] = "hwid";
constexpr char kRuntimeHWIDFilePath[] =
    "var/cache/hardware_verifier/runtime_hwid";

std::string CalculateChecksum(std::string_view runtime_hwid) {
  const auto& sha1_hash = base::SHA1HashString(runtime_hwid);
  return base::HexEncode(sha1_hash.data(), sha1_hash.size());
}

bool VerifyRuntimeHWID(std::string_view runtime_hwid,
                       std::string_view checksum,
                       std::string_view factory_hwid) {
  const auto expected_checksum = CalculateChecksum(runtime_hwid);
  if (checksum != expected_checksum) {
    VLOG(1) << "Runtime HWID verification failed: the checksum \"" << checksum
            << "\" doesn't match the expected value \"" << expected_checksum
            << "\"";
    return false;
  }

  const auto factory_hwid_split = base::SplitStringOnce(factory_hwid, ' ');
  const auto runtime_hwid_split = base::SplitStringOnce(runtime_hwid, ' ');
  const auto factory_hwid_model_rlz = factory_hwid_split->first;
  const auto runtime_hwid_model_rlz = runtime_hwid_split->first;

  if (runtime_hwid_model_rlz != factory_hwid_model_rlz) {
    VLOG(1)
        << "Runtime HWID verification failed: the model name and RLZ code \""
        << runtime_hwid_model_rlz << "\" doesn't match the expected value \""
        << factory_hwid_model_rlz << "\"";
    return false;
  }

  return true;
}

std::optional<std::string> GetRuntimeHWIDFromPath(
    const base::FilePath& runtime_hwid_file_path,
    std::string_view factory_hwid) {
  std::string runtime_hwid_content;
  if (!base::ReadFileToString(runtime_hwid_file_path, &runtime_hwid_content)) {
    VLOG(1) << "Failed to read Runtime HWID from " << runtime_hwid_file_path;
    return std::nullopt;
  }
  const auto lines =
      base::SplitString(runtime_hwid_content, "\n", base::TRIM_WHITESPACE,
                        base::SPLIT_WANT_NONEMPTY);

  if (lines.size() != 2) {
    VLOG(1) << "Invalid Runtime HWID file: expected 2 lines, but got "
            << lines.size() << " lines";
    return std::nullopt;
  }

  std::string runtime_hwid = lines[0];
  std::string checksum = lines[1];

  if (!VerifyRuntimeHWID(runtime_hwid, checksum, factory_hwid)) {
    return std::nullopt;
  }

  return runtime_hwid;
}

}  // namespace

RuntimeHWIDUtilsImpl::RuntimeHWIDUtilsImpl()
    : root_("/"), crossystem_(std::make_unique<crossystem::Crossystem>()) {}

std::optional<std::string> RuntimeHWIDUtilsImpl::GetRuntimeHWID() const {
  auto factory_hwid =
      crossystem_->VbGetSystemPropertyString(kCrosSystemHWIDKey);
  if (!factory_hwid.has_value()) {
    VLOG(1) << "Failed to read Factory HWID from crossystem";
    return std::nullopt;
  }

  const auto runtime_hwid_file_path = root_.Append(kRuntimeHWIDFilePath);
  const auto runtime_hwid =
      GetRuntimeHWIDFromPath(runtime_hwid_file_path, *factory_hwid);

  if (!runtime_hwid.has_value()) {
    return factory_hwid;
  }
  return runtime_hwid;
}

bool RuntimeHWIDUtilsImpl::DeleteRuntimeHWIDFromDevice() const {
  const auto runtime_hwid_path = root_.Append(kRuntimeHWIDFilePath);
  if (!base::PathExists(runtime_hwid_path)) {
    return true;
  }
  return brillo::DeleteFile(runtime_hwid_path);
}

}  // namespace hardware_verifier

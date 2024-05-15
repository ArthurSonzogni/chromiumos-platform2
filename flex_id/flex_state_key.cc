// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_id/flex_state_key.h"
#include "flex_id/utils.h"

#include <optional>

#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/data_encoding.h>

namespace flex_id {

namespace {

constexpr char kPreservedFlexStateKeyFile[] =
    "mnt/stateful_partition/unencrypted/preserve/flex/flex_state_key";
constexpr char kFlexStateKeyFile[] = "var/lib/flex_id/flex_state_key";
const int kFlexStateKeyLength = 64;

}  // namespace

FlexStateKeyGenerator::FlexStateKeyGenerator(const base::FilePath& base_path) {
  base_path_ = base_path;
}

std::optional<std::string> FlexStateKeyGenerator::ReadFlexStateKey() {
  std::optional<std::string> flex_state_key;
  const base::FilePath flex_state_key_path =
      base_path_.Append(kFlexStateKeyFile);

  if (!(flex_state_key = ReadAndTrimFile(flex_state_key_path))) {
    LOG(WARNING) << "Couldn't read flex_state_key file.";
    return std::nullopt;
  }
  if (flex_state_key.value().empty()) {
    LOG(WARNING) << "Read a blank flex_state_key file.";
    return std::nullopt;
  }

  return flex_state_key;
}

std::optional<std::string> FlexStateKeyGenerator::TryPreservedFlexStateKey() {
  std::optional<std::string> preserved_flex_state_key;
  const base::FilePath preserved_flex_state_key_path =
      base_path_.Append(kPreservedFlexStateKeyFile);

  if (!(preserved_flex_state_key =
            ReadAndTrimFile(preserved_flex_state_key_path))) {
    return std::nullopt;
  }
  if (preserved_flex_state_key.value().empty()) {
    return std::nullopt;
  }

  return preserved_flex_state_key;
}

std::optional<std::string> FlexStateKeyGenerator::GenerateFlexStateKey() {
  std::optional<std::string> flex_state_key;
  uint8_t flex_state_key_raw[kFlexStateKeyLength];
  std::string flex_state_key_hex;

  base::RandBytes(flex_state_key_raw);
  flex_state_key_hex = base::ToLowerASCII(base::HexEncode(flex_state_key_raw));

  flex_state_key = flex_state_key_hex;
  return flex_state_key;
}

bool FlexStateKeyGenerator::WriteFlexStateKey(
    const std::string& flex_state_key) {
  const base::FilePath flex_state_key_file_path =
      base_path_.Append(kFlexStateKeyFile);
  if (base::CreateDirectory(flex_state_key_file_path.DirName())) {
    return base::ImportantFileWriter::WriteFileAtomically(
        flex_state_key_file_path, flex_state_key + "\n");
  }
  return false;
}

std::optional<std::string>
FlexStateKeyGenerator::GenerateAndSaveFlexStateKey() {
  std::optional<std::string> flex_state_key;

  // Check for existing flex_state_key and exit early.
  if ((flex_state_key = ReadFlexStateKey())) {
    LOG(INFO) << "Found existing flex_state_key: " << flex_state_key.value();
    return flex_state_key;
  }

  // Generate a new flex_state_key.
  if ((flex_state_key = TryPreservedFlexStateKey())) {
    LOG(INFO) << "Using preserved flex_state_key for flex_state_key: "
              << flex_state_key.value();
  } else if ((flex_state_key = GenerateFlexStateKey())) {
    LOG(INFO) << "Generated a new flex_state_key: " << flex_state_key.value();
  } else {
    LOG(ERROR) << "Couldn't find or generate a flex_state_key";
    return std::nullopt;
  }

  // Write flex_state_key to file.
  if (WriteFlexStateKey(flex_state_key.value())) {
    LOG(INFO) << "Successfully wrote flex_state_key: "
              << flex_state_key.value();
    return flex_state_key;
  }

  return std::nullopt;
}

}  // namespace flex_id

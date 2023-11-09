// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/hwid_utils_impl.h>

#include <optional>
#include <string>
#include <vector>

#include <base/metrics/crc32.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace {

constexpr char kBase8Alphabet[] = "23456789";
constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
constexpr uint32_t kChecksumBitMask = 0xFF;
constexpr int kBase32BitWidth = 5;

}  // namespace

namespace rmad {

std::optional<std::string> HwidUtilsImpl::CalculateChecksum(
    const std::string& hwid) const {
  std::vector<std::string> parts =
      base::SplitString(hwid, " ", base::WhitespaceHandling::TRIM_WHITESPACE,
                        base::SplitResult::SPLIT_WANT_NONEMPTY);

  if (parts.size() != 2) {
    return std::nullopt;
  }

  base::RemoveChars(parts[1], "-", &parts[1]);

  std::string stripped =
      base::StringPrintf("%s %s", parts[0].c_str(), parts[1].c_str());

  uint32_t crc32 =
      ~base::Crc32(0xFFFFFFFF, stripped.c_str(), stripped.length()) &
      kChecksumBitMask;

  std::string checksum =
      base::StringPrintf("%c%c", kBase8Alphabet[crc32 >> kBase32BitWidth],
                         kBase32Alphabet[crc32 & ((1 << kBase32BitWidth) - 1)]);

  return checksum;
}

}  // namespace rmad

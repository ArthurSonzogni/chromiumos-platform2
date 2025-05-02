// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/hwid/hwid_utils.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/containers/fixed_flat_map.h>
#include <base/containers/span.h>
#include <base/metrics/crc32.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

namespace {

constexpr auto kBase8Map =
    base::MakeFixedFlatMap<char, std::string_view>({{'2', "000"},
                                                    {'3', "001"},
                                                    {'4', "010"},
                                                    {'5', "011"},
                                                    {'6', "100"},
                                                    {'7', "101"},
                                                    {'8', "110"},
                                                    {'9', "111"}});

constexpr auto kBase32Map = base::MakeFixedFlatMap<char, std::string_view>(
    {{'A', "00000"}, {'B', "00001"}, {'C', "00010"}, {'D', "00011"},
     {'E', "00100"}, {'F', "00101"}, {'G', "00110"}, {'H', "00111"},
     {'I', "01000"}, {'J', "01001"}, {'K', "01010"}, {'L', "01011"},
     {'M', "01100"}, {'N', "01101"}, {'O', "01110"}, {'P', "01111"},
     {'Q', "10000"}, {'R', "10001"}, {'S', "10010"}, {'T', "10011"},
     {'U', "10100"}, {'V', "10101"}, {'W', "10110"}, {'X', "10111"},
     {'Y', "11000"}, {'Z', "11001"}, {'2', "11010"}, {'3', "11011"},
     {'4', "11100"}, {'5', "11101"}, {'6', "11110"}, {'7', "11111"}});

// Size of the checksum used at the end of the HWID
constexpr size_t kHWIDChecksumBits = 8;

constexpr char kBase8Alphabet[] = "23456789";
constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
constexpr uint32_t kChecksumBitMask = 0xFF;
constexpr int kBase32BitWidth = 5;

template <typename MapType>
bool AppendDecodedBits(char key,
                       const MapType& decoded_bit_map,
                       std::string& decoded_string) {
  auto it = decoded_bit_map.find(key);
  if (it == decoded_bit_map.end()) {
    return false;
  }

  decoded_string.append(it->second);
  return true;
}

}  // namespace

namespace brillo {

std::optional<std::string> hwid::DecodeHWID(const std::string_view hwid) {
  // For instance, assume hwid = "SARIEN-MCOO 0-8-77-1D0 A2A-797" or
  // "REDRIX-ZZCR D3A-39F-27K-E6B".
  // After removing the MODEL-RLZ (e.g., "SARIEN-MCOO") and the optional
  // configless field (e.g., "0-8-77-1D0"), translate the component field (the
  // triplets of characters, e.g., "A2A-797") using the maps above. The middle
  // character uses a smaller map.
  //
  // Also, remove the trailer and checksum:
  // HWID format is as follow:
  // +---------------------------------------------------------+
  // |                         HWID                            |
  // +----------------+---+-----------------+------------------+
  // | payload        |EOS|   padding       | checksum (8bit)  |
  // +----------------+---+-----------------+------------------+
  // | XXXXXX         | 1 |    0...0        |     YYYY         |
  // +----------------+---+-----------------+------------------+
  // EOS is 1 bit, set to 1,
  // padding is 0 bits, so that HWID size is a multiple of 13.
  //
  // To remove the end, look for the last bit set to 1 in the whole string,
  // excluding the checksum.
  auto parts = base::RSplitStringOnce(
      base::TrimWhitespaceASCII(hwid, base::TrimPositions::TRIM_ALL), " ");
  if (!parts.has_value() || parts->second.empty()) {
    return std::nullopt;
  }

  std::string decoded_bit_string;
  for (const auto& key :
       base::SplitStringPiece(parts->second, "-", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (key.size() != 3) {
      return std::nullopt;
    }

    if (!AppendDecodedBits(key[0], kBase32Map, decoded_bit_string) ||
        !AppendDecodedBits(key[1], kBase8Map, decoded_bit_string) ||
        !AppendDecodedBits(key[2], kBase32Map, decoded_bit_string)) {
      return std::nullopt;
    }
  }
  if (decoded_bit_string.size() <= kHWIDChecksumBits) {
    return std::nullopt;
  }

  auto pos = decoded_bit_string.find_last_of(
      '1', decoded_bit_string.size() - kHWIDChecksumBits - 1);

  if (pos == std::string::npos) {
    return std::nullopt;
  }

  return decoded_bit_string.substr(0, pos);
}

std::optional<std::string> hwid::CalculateChecksum(
    const std::string_view hwid) {
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
      ~base::Crc32(0xFFFFFFFF, base::as_byte_span(stripped)) & kChecksumBitMask;

  std::string checksum =
      base::StringPrintf("%c%c", kBase8Alphabet[crc32 >> kBase32BitWidth],
                         kBase32Alphabet[crc32 & ((1 << kBase32BitWidth) - 1)]);

  return checksum;
}

}  // namespace brillo

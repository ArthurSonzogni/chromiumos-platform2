// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/hwid/hwid_utils.h"

#include <optional>
#include <string>
#include <string_view>

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
constexpr auto kReversedBase8Map =
    base::MakeFixedFlatMap<std::string_view, std::string_view>({{"000", "2"},
                                                                {"001", "3"},
                                                                {"010", "4"},
                                                                {"011", "5"},
                                                                {"100", "6"},
                                                                {"101", "7"},
                                                                {"110", "8"},
                                                                {"111", "9"}});

constexpr auto kBase32Map = base::MakeFixedFlatMap<char, std::string_view>(
    {{'A', "00000"}, {'B', "00001"}, {'C', "00010"}, {'D', "00011"},
     {'E', "00100"}, {'F', "00101"}, {'G', "00110"}, {'H', "00111"},
     {'I', "01000"}, {'J', "01001"}, {'K', "01010"}, {'L', "01011"},
     {'M', "01100"}, {'N', "01101"}, {'O', "01110"}, {'P', "01111"},
     {'Q', "10000"}, {'R', "10001"}, {'S', "10010"}, {'T', "10011"},
     {'U', "10100"}, {'V', "10101"}, {'W', "10110"}, {'X', "10111"},
     {'Y', "11000"}, {'Z', "11001"}, {'2', "11010"}, {'3', "11011"},
     {'4', "11100"}, {'5', "11101"}, {'6', "11110"}, {'7', "11111"}});
constexpr auto kReversedBase32Map =
    base::MakeFixedFlatMap<std::string_view, std::string_view>(
        {{"00000", "A"}, {"00001", "B"}, {"00010", "C"}, {"00011", "D"},
         {"00100", "E"}, {"00101", "F"}, {"00110", "G"}, {"00111", "H"},
         {"01000", "I"}, {"01001", "J"}, {"01010", "K"}, {"01011", "L"},
         {"01100", "M"}, {"01101", "N"}, {"01110", "O"}, {"01111", "P"},
         {"10000", "Q"}, {"10001", "R"}, {"10010", "S"}, {"10011", "T"},
         {"10100", "U"}, {"10101", "V"}, {"10110", "W"}, {"10111", "X"},
         {"11000", "Y"}, {"11001", "Z"}, {"11010", "2"}, {"11011", "3"},
         {"11100", "4"}, {"11101", "5"}, {"11110", "6"}, {"11111", "7"}});

// Size of the checksum used at the end of the HWID
constexpr size_t kHWIDChecksumBitWidth = 8;

constexpr char kBase8Alphabet[] = "23456789";
constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
constexpr uint32_t kChecksumBitMask = 0xFF;
constexpr size_t kBase32BitWidth = 5;
constexpr size_t kBase8BitWidth = 3;
constexpr size_t kBase8192BitWidth = 13;

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

template <typename MapType>
bool AppendEncodedStrAndUpdateBitPos(std::string_view binary_string,
                                     size_t& bit_pos,
                                     size_t bit_width,
                                     const MapType& encoded_str_map,
                                     std::string& encoded_string) {
  if (bit_pos + bit_width > binary_string.size()) {
    return false;
  }

  auto key = binary_string.substr(bit_pos, bit_width);
  auto it = encoded_str_map.find(key);
  if (it == encoded_str_map.end()) {
    return false;
  }

  encoded_string.append(it->second);
  bit_pos += bit_width;
  return true;
}

size_t GetPaddingLength(std::string_view binary_string) {
  return (kBase8192BitWidth - ((binary_string.size() + kHWIDChecksumBitWidth) %
                               kBase8192BitWidth)) %
         kBase8192BitWidth;
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
  if (decoded_bit_string.size() <= kHWIDChecksumBitWidth) {
    return std::nullopt;
  }

  auto pos = decoded_bit_string.find_last_of(
      '1', decoded_bit_string.size() - kHWIDChecksumBitWidth - 1);

  if (pos == std::string::npos) {
    return std::nullopt;
  }

  return decoded_bit_string.substr(0, pos);
}

std::optional<std::string> hwid::EncodeHWID(
    const std::string_view hwid_prefix, const std::string_view binary_payload) {
  auto invalid_pos = binary_payload.find_first_not_of("01");
  if (invalid_pos != std::string::npos) {
    return std::nullopt;
  }

  // Append EOS and padding.
  auto binary_hwid = std::string(binary_payload) + "1";
  binary_hwid.append(GetPaddingLength(binary_hwid), '0');

  std::string encoded_hwid;
  size_t bit_pos = 0;
  while (bit_pos + kBase8192BitWidth <=
         binary_hwid.length() - kBase32BitWidth) {
    if (!AppendEncodedStrAndUpdateBitPos(binary_hwid, bit_pos, kBase32BitWidth,
                                         kReversedBase32Map, encoded_hwid) ||
        !AppendEncodedStrAndUpdateBitPos(binary_hwid, bit_pos, kBase8BitWidth,
                                         kReversedBase8Map, encoded_hwid) ||
        !AppendEncodedStrAndUpdateBitPos(binary_hwid, bit_pos, kBase32BitWidth,
                                         kReversedBase32Map, encoded_hwid)) {
      return std::nullopt;
    }
    encoded_hwid.append("-");
  }
  // The last group is only 5-bit long.
  if (!AppendEncodedStrAndUpdateBitPos(binary_hwid, bit_pos, kBase32BitWidth,
                                       kReversedBase32Map, encoded_hwid)) {
    return std::nullopt;
  }

  auto hwid = base::JoinString({hwid_prefix, encoded_hwid}, " ");
  auto checksum = CalculateChecksum(hwid);
  if (!checksum.has_value()) {
    return std::nullopt;
  }

  return hwid + checksum.value();
}

std::optional<std::string> hwid::CalculateChecksum(
    const std::string_view hwid) {
  auto parts = base::SplitStringOnce(
      base::TrimWhitespaceASCII(hwid, base::TrimPositions::TRIM_ALL), " ");

  if (!parts.has_value() || parts->second.empty()) {
    return std::nullopt;
  }

  auto prefix = std::string(parts->first);
  auto encoded_string = std::string(parts->second);

  base::RemoveChars(encoded_string, "-", &encoded_string);

  std::string stripped =
      base::StringPrintf("%s %s", prefix.c_str(), encoded_string.c_str());

  uint32_t crc32 =
      ~base::Crc32(0xFFFFFFFF, base::as_byte_span(stripped)) & kChecksumBitMask;

  std::string checksum =
      base::StringPrintf("%c%c", kBase8Alphabet[crc32 >> kBase32BitWidth],
                         kBase32Alphabet[crc32 & ((1 << kBase32BitWidth) - 1)]);

  return checksum;
}

}  // namespace brillo

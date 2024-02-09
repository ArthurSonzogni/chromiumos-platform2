// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.

#include "verity/dm_verity_table.h"

#include <optional>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace verity {

constexpr char kNoSalt[] = "-";
constexpr char kVerityTarget[] = "verity";
constexpr char kVersion[] = "0";

std::optional<std::string> DmVerityTable::Print(Format format) {
  if (root_digest_.empty()) {
    LOG(ERROR) << "Missing root digest.";
    return std::nullopt;
  }
  const std::string root_digest(reinterpret_cast<char*>(root_digest_.data()));

  const std::string kDataStartSector{"0"};
  const auto& num_data_sectors = base::NumberToString(
      to_sector(data_dev_.block_count * data_dev_.block_size));

  std::vector<std::string> parts;
  switch (format) {
    case Format::VANILLA: {
      const auto& data_dev_block_end =
          base::NumberToString(data_dev_.block_count);
      std::string hash_dev_block_start;
      switch (hash_placement_) {
        case HashPlacement::COLOCATED:
          hash_dev_block_start = data_dev_block_end;
          break;
        case HashPlacement::SEPARATE:
          hash_dev_block_start = "0";
          break;
      }
      parts = {
          kDataStartSector,
          num_data_sectors,
          kVerityTarget,
          kVersion,
          data_dev_.dev,
          hash_dev_.dev,
          base::NumberToString(data_dev_.block_size),
          base::NumberToString(hash_dev_.block_size),
          data_dev_block_end,
          hash_dev_block_start,
          std::string(alg_),
          root_digest,
          salt_ ? std::string(salt_.value().data()) : kNoSalt,
      };
      break;
    }
    case Format::CROS: {
      std::string hashstart;
      switch (hash_placement_) {
        case HashPlacement::COLOCATED:
          hashstart = num_data_sectors;
          break;
        case HashPlacement::SEPARATE:
          hashstart = "0";
          break;
      }
      parts = {
          kDataStartSector,
          num_data_sectors,
          kVerityTarget,
          "payload=" + data_dev_.dev,
          "hashtree=" + hash_dev_.dev,
          "hashstart=" + hashstart,
          "alg=" + std::string(alg_),
          "root_hexdigest=" + root_digest,
      };
      if (salt_)
        parts.push_back("salt=" + std::string(salt_.value().data()));
      break;
    }
  }
  return base::JoinString(parts, " ");
}

}  // namespace verity

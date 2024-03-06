// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.

#include "verity/dm_verity_table.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "verity/dm-bht.h"

namespace verity {

namespace {

std::optional<DmVerityTable> ParseVanilla(
    const std::vector<std::string>& table_split) {
  if (table_split.size() < DmVerityTable::V_LAST_INDEX) {
    LOG(ERROR) << "Invalid table format.";
    return {};
  }

  const auto& root_digest_str = table_split[DmVerityTable::V_ROOT_DIGEST];
  if (root_digest_str.size() > DM_BHT_MAX_DIGEST_SIZE) {
    LOG(ERROR) << "Invalid root digest.";
    return {};
  }
  DmVerityTable::RootDigestType root_digest = {};
  std::copy(root_digest_str.begin(), root_digest_str.end(),
            root_digest.begin());

  const auto& salt_str = table_split[DmVerityTable::V_SALT];
  if (salt_str.size() > sizeof(DmVerityTable::SaltType)) {
    LOG(ERROR) << "Invalid salt.";
    return {};
  }
  DmVerityTable::SaltType salt = {};
  std::copy(salt_str.begin(), salt_str.end(), salt.begin());

  decltype(DmVerityTable::DevInfo::block_count) data_block_count;
  if (!base::StringToUint64(table_split[DmVerityTable::V_DATA_DEVICE_BLOCK_END],
                            &data_block_count)) {
    LOG(ERROR) << "Invalid data device block end.";
    return {};
  }

  decltype(DmVerityTable::DevInfo::block_size) data_block_size;
  if (!base::StringToUint64(
          table_split[DmVerityTable::V_DATA_DEVICE_BLOCK_SIZE],
          &data_block_size)) {
    LOG(ERROR) << "Invalid data device block size.";
    return {};
  }

  decltype(DmVerityTable::DevInfo::block_size) hash_block_size;
  if (!base::StringToUint64(
          table_split[DmVerityTable::V_HASH_DEVICE_BLOCK_SIZE],
          &hash_block_size)) {
    LOG(ERROR) << "Invalid hash device block size.";
    return {};
  }

  return DmVerityTable{table_split[DmVerityTable::V_ALGORITHM],
                       std::move(root_digest),
                       std::move(salt),
                       DmVerityTable::DevInfo{
                           .dev = table_split[DmVerityTable::V_DATA_DEVICE],
                           .block_size = data_block_size,
                           .block_count = data_block_count,
                       },
                       DmVerityTable::DevInfo{
                           .dev = table_split[DmVerityTable::V_HASH_DEVICE],
                           .block_size = hash_block_size,
                           // Can safely ignore block_count.
                       },
                       DmVerityTable::HashPlacement::COLOCATED};
}

std::optional<DmVerityTable> ParseCrOS(
    const std::vector<std::string>& table_split) {
  base::StringPairs alg_pairs;
  if (!base::SplitStringIntoKeyValuePairs(
          table_split[DmVerityTable::C_ALGORITHM], '=', '\n', &alg_pairs) ||
      alg_pairs.size() != 1) {
    LOG(ERROR) << "Invalid algorithm.";
    return {};
  }
  const auto& alg = alg_pairs[0].second;

  base::StringPairs payload_pairs;
  if (!base::SplitStringIntoKeyValuePairs(table_split[DmVerityTable::C_PAYLOAD],
                                          '=', '\n', &payload_pairs) ||
      payload_pairs.size() != 1) {
    LOG(ERROR) << "Invalid payload.";
    return {};
  }
  const auto& payload = payload_pairs[0].second;

  base::StringPairs hashtree_pairs;
  if (!base::SplitStringIntoKeyValuePairs(
          table_split[DmVerityTable::C_HASHTREE], '=', '\n', &hashtree_pairs) ||
      hashtree_pairs.size() != 1) {
    LOG(ERROR) << "Invalid hashtree.";
    return {};
  }
  const auto& hashtree = hashtree_pairs[0].second;

  base::StringPairs root_digest_pairs;
  if (!base::SplitStringIntoKeyValuePairs(
          table_split[DmVerityTable::C_ROOT_DIGEST], '=', '\n',
          &root_digest_pairs) ||
      root_digest_pairs.size() != 1) {
    LOG(ERROR) << "Invalid root digest.";
    return {};
  }
  const auto& root_digest_str = root_digest_pairs[0].second;
  if (root_digest_str.size() > DM_BHT_MAX_DIGEST_SIZE) {
    LOG(ERROR) << "Invalid root digest length.";
    return {};
  }
  DmVerityTable::RootDigestType root_digest = {};
  std::copy(root_digest_str.begin(), root_digest_str.end(),
            root_digest.begin());

  std::optional<DmVerityTable::SaltType> salt;
  if (table_split.size() >= DmVerityTable::C_LAST_INDEX) {
    base::StringPairs salt_pairs;
    if (!base::SplitStringIntoKeyValuePairs(table_split[DmVerityTable::C_SALT],
                                            '=', '\n', &salt_pairs) ||
        salt_pairs.size() != 1) {
      LOG(ERROR) << "Invalid salt.";
      return {};
    }
    const auto& salt_str = salt_pairs[0].second;
    if (salt_str.size() > sizeof(DmVerityTable::SaltType)) {
      LOG(ERROR) << "Invalid salt length.";
      return {};
    }
    salt = DmVerityTable::SaltType{};
    std::copy(salt_str.begin(), salt_str.end(), salt->begin());
  }

  decltype(DmVerityTable::DevInfo::block_count) num_data_sectors;
  if (!base::StringToUint64(table_split[DmVerityTable::C_NUM_DATA_SECTOR],
                            &num_data_sectors)) {
    LOG(ERROR) << "Invalid num data sectors.";
    return {};
  }

  return DmVerityTable{
      alg,
      std::move(root_digest),
      salt,
      DmVerityTable::DevInfo{
          .dev = payload,
          .block_size = PAGE_SIZE,
          .block_count = verity_to_bytes(num_data_sectors) / PAGE_SIZE,
      },
      DmVerityTable::DevInfo{
          .dev = hashtree,
      },
      DmVerityTable::HashPlacement::COLOCATED};
}

}  // namespace

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

std::optional<DmVerityTable> DmVerityTable::Parse(const std::string& table_str,
                                                  Format format) {
  auto table_split = base::SplitString(
      table_str, " ", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_NONEMPTY);
  switch (format) {
    case Format::VANILLA:
      return ParseVanilla(table_split);
    case Format::CROS:
      return ParseCrOS(table_split);
    default:
      return {};
  }
}

std::string DmVerityTable::GetAlgorithm() const {
  return alg_;
}

DmVerityTable::RootDigestType DmVerityTable::GetRootDigest() const {
  return root_digest_;
}

std::optional<DmVerityTable::SaltType> DmVerityTable::GetSalt() const {
  return salt_;
}

DmVerityTable::DevInfo DmVerityTable::GetDataDevice() const {
  return data_dev_;
}

DmVerityTable::DevInfo DmVerityTable::GetHashDevice() const {
  return hash_dev_;
}

DmVerityTable::HashPlacement DmVerityTable::GetHashPlacement() const {
  return hash_placement_;
}

}  // namespace verity

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.

#ifndef VERITY_DM_VERITY_TABLE_H_
#define VERITY_DM_VERITY_TABLE_H_

#include <array>
#include <optional>
#include <string>

#include <brillo/brillo_export.h>

#include "verity/dm-bht.h"

namespace verity {

// `DmVerityTable` represents dm-verity table formats and provides methods for
// working with them.
class BRILLO_EXPORT DmVerityTable {
 public:
  using RootDigestType = std::array<uint8_t, DM_BHT_MAX_DIGEST_SIZE>;

  // The salt in hex requires twice `DM_BHT_SALT_SIZE`. | + 1| is to make the
  // check at
  // https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/verity/dm-bht.cc;l=744
  // happy.
  using SaltType = std::array<char, DM_BHT_SALT_SIZE * 2 + 1>;

  // The table format types.
  enum class Format {
    CROS = 0,
    VANILLA,
  };

  // The placement decision for the hash device.
  enum class HashPlacement {
    COLOCATED = 0,
    SEPARATE,
  };

  enum CrOSIndex {
    C_DATA_START_SECTOR = 0,
    C_NUM_DATA_SECTOR,
    C_VERITY_TARGET,
    C_PAYLOAD,
    C_HASHTREE,
    C_HASH_START,
    C_ALGORITHM,
    C_ROOT_DIGEST,
    C_SALT,
    C_LAST_INDEX,  // Note: Always keep as the last value.
  };

  enum VanillaIndex {
    V_DATA_START_SECTOR = 0,
    V_NUM_DATA_SECTOR,
    V_VERITY_TARGET,
    V_VERSION,
    V_DATA_DEVICE,
    V_HASH_DEVICE,
    V_DATA_DEVICE_BLOCK_SIZE,
    V_HASH_DEVICE_BLOCK_SIZE,
    V_DATA_DEVICE_BLOCK_END,
    V_HASH_DEVICE_BLOCK_START,
    V_ALGORITHM,
    V_ROOT_DIGEST,
    V_SALT,
    V_LAST_INDEX,  // Note: Always keep as the last value.
  };

  // `DevInfo` represents device information such as the root or hash device
  // when targeting verity.
  struct DevInfo {
    // The name of the device.
    std::string dev;
    // The device's block size.
    uint64_t block_size = PAGE_SIZE;
    // The device's block count.
    uint64_t block_count = 0;

    bool operator==(const DevInfo& o) const {
      return dev == o.dev && block_size == o.block_size &&
             block_count == o.block_count;
    }
  };

  DmVerityTable(const std::string& alg,
                RootDigestType root_digest,
                std::optional<SaltType> salt,
                const DevInfo& data_dev,
                const DevInfo& hash_dev,
                HashPlacement hash_placement = HashPlacement::COLOCATED)
      : alg_(alg),
        root_digest_(root_digest),
        salt_(salt),
        data_dev_(data_dev),
        hash_dev_(hash_dev),
        hash_placement_(hash_placement) {}
  virtual ~DmVerityTable() = default;

  DmVerityTable(const DmVerityTable&) = delete;
  DmVerityTable& operator=(const DmVerityTable&) = delete;

  DmVerityTable(DmVerityTable&&) = default;
  DmVerityTable& operator=(DmVerityTable&&) = default;

  bool operator==(const DmVerityTable& o) const;

  static std::optional<DmVerityTable> Parse(const std::string& table_str,
                                            Format format);

  // Prints the dm-verity table in the requested `Format`.
  // Returns `std::nullopt` on error.
  std::optional<std::string> Print(Format format);

  // All the getters you'd need!
  std::string GetAlgorithm() const;
  RootDigestType GetRootDigest() const;
  std::optional<SaltType> GetSalt() const;
  DevInfo GetDataDevice() const;
  DevInfo GetHashDevice() const;
  HashPlacement GetHashPlacement() const;

 private:
  std::string alg_;
  RootDigestType root_digest_;
  std::optional<SaltType> salt_;
  DevInfo data_dev_;
  DevInfo hash_dev_;
  HashPlacement hash_placement_;
};

}  // namespace verity

#endif  // VERITY_DM_VERITY_TABLE_H_

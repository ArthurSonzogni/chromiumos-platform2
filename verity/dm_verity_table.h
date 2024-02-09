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

  // `DevInfo` represents device information such as the root or hash device
  // when targeting verity.
  struct DevInfo {
    // The name of the device.
    std::string dev;
    // The device's block size.
    uint64_t block_size = PAGE_SIZE;
    // The device's block count.
    uint64_t block_count = 0;
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

  DmVerityTable(DmVerityTable&) = delete;
  DmVerityTable& operator=(const DmVerityTable&) = delete;

  // Prints the dm-verity table in the requested `Format`.
  // Returns `std::nullopt` on error.
  std::optional<std::string> Print(Format format);

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

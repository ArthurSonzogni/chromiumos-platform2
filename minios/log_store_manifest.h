// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_LOG_STORE_MANIFEST_H_
#define MINIOS_LOG_STORE_MANIFEST_H_

#include "minios/log_store_manifest_interface.h"

#include <cstdint>
#include <optional>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>
#include <minios/proto_bindings/minios.pb.h>

namespace minios {

extern const uint64_t kBlockSize;
class LogStoreManifest : public LogStoreManifestInterface {
 public:
  LogStoreManifest(base::FilePath disk_path,
                   uint64_t kernel_size,
                   uint64_t partition_size);

  LogStoreManifest(const LogStoreManifest&) = delete;
  LogStoreManifest& operator=(const LogStoreManifest&) = delete;

  ~LogStoreManifest() override = default;

  bool Generate(const LogManifest::Entry& entry) override;
  std::optional<LogManifest> Retreive() override;
  bool Write() override;
  // Clear any manifest stores found on disk.
  void Clear() override;

 private:
  FRIEND_TEST(LogStoreManifestTest, VerifyGenerate);
  FRIEND_TEST(LogStoreManifestTest, WriteFailsWithoutGenerate);
  FRIEND_TEST(LogStoreManifestTest, VerifyFind);
  FRIEND_TEST(LogStoreManifestTest, DisabledWithInvalidArgs);
  FRIEND_TEST(LogStoreManifestTest, VerifyWriteAndRetrieve);
  FRIEND_TEST(LogStoreManifestTest, VerifyClear);

  // Helper function to find manifest block. Returns the byte index of the magic
  // start.
  std::optional<uint64_t> FindManifestMagic();

  // Returns true if construction parameters are valid and disk file was opened
  // successfully.
  bool IsValid() const { return valid_; }

  void SetValid(bool valid) { valid_ = valid; }

  base::FilePath disk_path_;
  base::File disk_;
  const uint64_t kernel_size_;
  const uint64_t partition_size_;
  const uint64_t manifest_store_start_;

  std::optional<uint64_t> disk_manifest_location_;
  std::optional<LogManifest> manifest_;

  bool valid_ = true;
};

}  // namespace minios

#endif  // MINIOS_LOG_STORE_MANIFEST_H_

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_LOG_STORE_MANIFEST_H_
#define MINIOS_LOG_STORE_MANIFEST_H_

#include <cstdint>
#include <optional>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>
#include <minios/proto_bindings/minios.pb.h>

namespace minios {

extern const uint64_t kBlockSize;

// Interface for a log store manifest helper class.
class LogStoreManifestInterface {
 public:
  virtual ~LogStoreManifestInterface() = default;

  // Generate a manifest with the given `entry`.
  virtual bool Generate(const LogManifest::Entry& entry) = 0;

  // Retrieve a previously written manifest from disk. This is done by
  // inspecting the first `sizeof(kLogStoreMagic)` bytes of every block on
  // `disk_path` until a magic value is found. If no manifest is found on disk,
  // a `nullopt` is returned.
  virtual std::optional<LogManifest> Retrieve() = 0;

  // Write a manifest in the `manifest_store_offset_block` of the current disk.
  // Note that the first `sizeof(kLogStoreMagic)` bytes will be a magic value,
  // followed by the serialized protobuf.
  virtual bool Write() = 0;

  // Clear any manifest stores found on disk. Similar to `Retrieve` we first
  // seek the manifest store, and then write `0` until the end of the partition.
  virtual void Clear() = 0;
};

class LogStoreManifest : public LogStoreManifestInterface {
 public:
  LogStoreManifest(base::FilePath disk_path,
                   uint64_t kernel_size,
                   uint64_t partition_size);
  ~LogStoreManifest() override = default;

  LogStoreManifest(const LogStoreManifest&) = delete;
  LogStoreManifest& operator=(const LogStoreManifest&) = delete;

  bool Generate(const LogManifest::Entry& entry) override;
  std::optional<LogManifest> Retrieve() override;
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

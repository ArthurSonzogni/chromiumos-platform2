// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/utils/utils.h"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/files/file_util.h>
#include <crypto/secure_hash.h>
#include <crypto/sha2.h>

using base::FilePath;
using crypto::SecureHash;
using std::unique_ptr;
using std::vector;

namespace dlcservice {

namespace {
constexpr char kDlcLogicalVolumePrefix[] = "dlc_";
constexpr char kDlcSlotA[] = "_a";
constexpr char kDlcSlotB[] = "_b";
}  // namespace

const char kDlcPowerwashSafeFile[] = "/opt/google/dlc/_powerwash_safe_";
const char kPackage[] = "package";
const char kManifestName[] = "imageloader.json";

std::string Utils::LogicalVolumeName(const std::string& id,
                                     PartitionSlot slot) {
  static const std::string& kPrefix(kDlcLogicalVolumePrefix);
  switch (slot) {
    case PartitionSlot::A:
      return kPrefix + id + kDlcSlotA;
    case PartitionSlot::B:
      return kPrefix + id + kDlcSlotB;
  }
}

std::string LogicalVolumeName(const std::string& id,
                              PartitionSlot slot,
                              std::unique_ptr<UtilsInterface> utils) {
  return utils->LogicalVolumeName(id, slot);
}

bool Utils::HashFile(const base::FilePath& path,
                     int64_t size,
                     vector<uint8_t>* sha256,
                     bool skip_size_check) {
  base::File f(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!f.IsValid()) {
    PLOG(ERROR) << "Failed to read file at " << path.value()
                << ", reason: " << base::File::ErrorToString(f.error_details());
    return false;
  }

  if (!skip_size_check) {
    auto length = f.GetLength();
    if (length < 0) {
      LOG(ERROR) << "Failed to get length for file at " << path.value();
      return false;
    }
    if (length < size) {
      LOG(ERROR) << "File size " << length
                 << " is smaller than intended file size " << size;
      return false;
    }
  }

  constexpr int64_t kMaxBufSize = 4096;
  unique_ptr<SecureHash> hash(SecureHash::Create(SecureHash::SHA256));

  vector<char> buf(kMaxBufSize);
  for (; size > 0; size -= kMaxBufSize) {
    int bytes = std::min(kMaxBufSize, size);
    if (f.ReadAtCurrentPos(buf.data(), bytes) != bytes) {
      PLOG(ERROR) << "Failed to read from file at " << path.value();
      return false;
    }
    hash->Update(buf.data(), bytes);
  }
  sha256->resize(crypto::kSHA256Length);
  hash->Finish(sha256->data(), sha256->size());
  return true;
}

bool HashFile(const base::FilePath& path,
              int64_t size,
              vector<uint8_t>* sha256,
              bool skip_size_check,
              std::unique_ptr<UtilsInterface> utils) {
  return utils->HashFile(path, size, sha256, skip_size_check);
}

std::shared_ptr<imageloader::Manifest> Utils::GetDlcManifest(
    const FilePath& dlc_manifest_path,
    const std::string& id,
    const std::string& package) {
  std::string dlc_json_str;
  const auto& dlc_manifest_file =
      dlc_manifest_path.Append(id).Append(package).Append(kManifestName);

  if (!base::ReadFileToString(dlc_manifest_file, &dlc_json_str)) {
    LOG(ERROR) << "Failed to read DLC manifest file '"
               << dlc_manifest_file.value() << "'.";
    return nullptr;
  }

  auto manifest = std::make_shared<imageloader::Manifest>();
  if (!manifest->ParseManifest(dlc_json_str)) {
    LOG(ERROR) << "Failed to parse DLC manifest for DLC:" << id << ".";
    return nullptr;
  }

  return manifest;
}

std::shared_ptr<imageloader::Manifest> GetDlcManifest(
    const FilePath& dlc_manifest_path,
    const std::string& id,
    const std::string& package,
    std::unique_ptr<UtilsInterface> utils) {
  return utils->GetDlcManifest(dlc_manifest_path, id, package);
}

}  // namespace dlcservice

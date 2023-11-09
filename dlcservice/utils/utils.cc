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
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/values.h>
#include <crypto/secure_hash.h>
#include <crypto/sha2.h>

#include "dlcservice/metadata/metadata.h"

using base::FilePath;
using crypto::SecureHash;
using std::unique_ptr;
using std::vector;

namespace dlcservice {

const char kDlcLogicalVolumePrefix[] = "dlc_";
const char kDlcLogicalVolumeSlotA[] = "_a";
const char kDlcLogicalVolumeSlotB[] = "_b";

const char kDlcPowerwashSafeFile[] = "/opt/google/dlc/_powerwash_safe_";
const char kPackage[] = "package";
const char kManifestName[] = "imageloader.json";

std::string Utils::LogicalVolumeName(const std::string& id,
                                     PartitionSlot slot) {
  static const std::string& kPrefix(kDlcLogicalVolumePrefix);
  switch (slot) {
    case PartitionSlot::A:
      return kPrefix + id + kDlcLogicalVolumeSlotA;
    case PartitionSlot::B:
      return kPrefix + id + kDlcLogicalVolumeSlotB;
  }
}

std::string LogicalVolumeName(const std::string& id,
                              PartitionSlot slot,
                              std::unique_ptr<UtilsInterface> utils) {
  return utils->LogicalVolumeName(id, slot);
}

std::string Utils::LogicalVolumeNameToId(const std::string& lv_name) {
  if (!lv_name.starts_with(kDlcLogicalVolumePrefix)) {
    return "";
  }
  std::string id;
  id = lv_name.substr(strlen(kDlcLogicalVolumePrefix));
  if (!id.ends_with(kDlcLogicalVolumeSlotA) &&
      !id.ends_with(kDlcLogicalVolumeSlotB)) {
    return "";
  }
  return id.substr(0, id.size() - strlen(kDlcLogicalVolumeSlotA));
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

std::shared_ptr<imageloader::Manifest> Utils::GetDlcManifest(
    const std::string& id, const base::FilePath& dlc_manifest_path) {
  InitializeDlcMetadata(dlc_manifest_path);
  if (auto manifest = GetDlcManifestInternal(id)) {
    return manifest;
  } else {
    return GetDlcManifest(dlc_manifest_path, id, kPackage);
  }
}

std::shared_ptr<imageloader::Manifest> Utils::GetDlcManifestInternal(
    const std::string& id) {
  if (!metadata_)
    return nullptr;

  auto entry = metadata_->Get(id);
  if (!entry) {
    LOG(ERROR) << "Failed to get metadata for DLC=" << id;
    return nullptr;
  }

  auto manifest = std::make_shared<imageloader::Manifest>();
  if (!manifest->ParseManifest(entry->manifest)) {
    LOG(ERROR) << "Failed to parse manifest for DLC=" << id;
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

std::shared_ptr<imageloader::Manifest> GetDlcManifest(
    const std::string& id,
    const FilePath& dlc_manifest_path,
    std::unique_ptr<UtilsInterface> utils) {
  return utils->GetDlcManifest(id, dlc_manifest_path);
}

DlcIdList Utils::GetSupportedDlcIds(const base::FilePath& metadata_path) {
  InitializeDlcMetadata(metadata_path);
  if (metadata_) {
    return metadata_->ListDlcIds(metadata::Metadata::FilterKey::kNone,
                                 base::Value());
  }

  return {};
}

bool Utils::InitializeDlcMetadata(const base::FilePath& path) {
  if (metadata_)
    return true;

  metadata_ = std::make_unique<metadata::Metadata>(path);
  if (!metadata_->Initialize()) {
    metadata_.reset();
    LOG(ERROR) << "Failed to initialize the DLC metadata.";
    return false;
  }
  return true;
}

}  // namespace dlcservice

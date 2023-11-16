//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/install_action.h"

#include <inttypes.h>

#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/constants/imageloader.h>
#include <crypto/secure_hash.h>
#include <crypto/sha2.h>
#include <libimageloader/manifest.h>

#include "update_engine/common/boot_control.h"
#include "update_engine/common/dlcservice_interface.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/image_properties.h"

namespace chromeos_update_engine {

namespace {
constexpr char kBandaidUrl[] = "https://edgedl.me.gvt1.com/edgedl/dlc";
constexpr char kLorryUrl[] = "https://dl.google.com/dlc";

constexpr char kBandaidArtifactsMetaUrl[] = "https://edgedl.me.gvt1.com/edgedl";
constexpr char kLorryArtifactsMetaUrl[] = "https://dl.google.com";

constexpr char kDefaultArtifact[] = "dlc.img";
constexpr char kDefaultPackage[] = "package";
constexpr char kDefaultSlotting[] = "dlc-scaled";
}  // namespace

InstallAction::InstallAction(std::unique_ptr<HttpFetcher> http_fetcher,
                             const std::string& id,
                             const std::string& slotting,
                             const std::string& manifest_dir)
    : http_fetcher_(std::move(http_fetcher)), id_(id) {
  slotting_ = slotting.empty() ? kDefaultSlotting : slotting;
  manifest_dir_ =
      manifest_dir.empty() ? imageloader::kDlcManifestRootpath : manifest_dir;
}

InstallAction::~InstallAction() {}

void InstallAction::PerformAction() {
  LOG(INFO) << "InstallAction performing action.";

  manifest_ = SystemState::Get()->dlc_utils()->GetDlcManifest(
      id_, base::FilePath(manifest_dir_));
  if (!manifest_) {
    LOG(ERROR) << "Could not retrieve manifest for " << id_;
    processor_->ActionComplete(this, ErrorCode::kScaledInstallationError);
    return;
  }
  image_props_ = LoadImageProperties();
  http_fetcher_->set_delegate(this);

  // Get the DLC device partition.
  auto partition_name =
      base::FilePath("dlc").Append(id_).Append(kDefaultPackage).value();
  auto* boot_control = SystemState::Get()->boot_control();

  std::string partition;
  if (!boot_control->GetPartitionDevice(
          partition_name, boot_control->GetCurrentSlot(), &partition)) {
    LOG(ERROR) << "Could not retrieve device partition for " << id_;
    processor_->ActionComplete(this, ErrorCode::kScaledInstallationError);
    return;
  }

  f_.Initialize(base::FilePath(partition), base::File::Flags::FLAG_OPEN |
                                               base::File::Flags::FLAG_READ |
                                               base::File::Flags::FLAG_WRITE);
  if (!f_.IsValid()) {
    LOG(ERROR) << "Could not open device partition for " << id_ << " at "
               << partition;
    processor_->ActionComplete(this, ErrorCode::kScaledInstallationError);
    return;
  }
  LOG(INFO) << "Installing to " << partition;

  std::string url_to_fetch;
  const auto& artifacts_meta = manifest_->artifacts_meta();
  if (artifacts_meta.valid) {
    auto UrlToFetch = [artifacts_meta](const std::string& url) -> std::string {
      return base::FilePath(url)
          .Append(artifacts_meta.uri)
          .Append(kDefaultArtifact)
          .value();
    };
    url_to_fetch = UrlToFetch(kBandaidArtifactsMetaUrl);
    backup_urls_ = {UrlToFetch(kLorryArtifactsMetaUrl)};
  } else {
    auto UrlToFetch = [this](const std::string& url) -> std::string {
      return base::FilePath(url)
          .Append(image_props_.builder_path)
          .Append(slotting_)
          .Append(id_)
          .Append(kDefaultPackage)
          .Append(kDefaultArtifact)
          .value();
    };
    url_to_fetch = UrlToFetch(kBandaidUrl);
    backup_urls_ = {UrlToFetch(kLorryUrl)};
  }
  StartInstallation(url_to_fetch);
}

void InstallAction::TerminateProcessing() {
  http_fetcher_->TerminateTransfer();
}

bool InstallAction::ReceivedBytes(HttpFetcher* fetcher,
                                  const void* bytes,
                                  size_t length) {
  uint64_t new_offset = offset_ + length;
  // Overflow upper bound check against manifest.
  if (new_offset > manifest_->size()) {
    LOG(ERROR) << "Overflow of bytes, terminating.";
    http_fetcher_->TerminateTransfer();
    return false;
  }

  if (delegate()) {
    delegate_->BytesReceived(new_offset, manifest_->size());
  }

  hash_->Update(bytes, length);
  int64_t total_written_bytes = 0;
  do {
    int written_bytes =
        f_.Write(offset_ + total_written_bytes,
                 static_cast<const char*>(bytes) + total_written_bytes,
                 length - total_written_bytes);
    if (written_bytes == -1) {
      PLOG(ERROR) << "Failed to write bytes.";
      http_fetcher_->TerminateTransfer();
      return false;
    }

    total_written_bytes += written_bytes;
  } while (total_written_bytes != length);

  offset_ = new_offset;
  return true;
}

void InstallAction::TransferComplete(HttpFetcher* fetcher, bool successful) {
  if (!successful) {
    // Continue to use backup URLs.
    if (backup_url_index_ < backup_urls_.size()) {
      LOG(INFO) << "Using backup url at index=" << backup_url_index_;
      StartInstallation(backup_urls_[backup_url_index_++]);
      return;
    }
    LOG(ERROR) << "Transfer failed.";
    TerminateInstallation();
    return;
  }

  auto expected_offset = manifest_->size();
  if (offset_ != expected_offset) {
    LOG(ERROR) << "Transferred bytes offset (" << offset_
               << ") don't match the expected offset (" << expected_offset
               << ").";
    TerminateInstallation();
    return;
  }
  LOG(INFO) << "Transferred bytes offset (" << expected_offset << ") is valid.";

  std::vector<uint8_t> sha256(crypto::kSHA256Length);
  hash_->Finish(sha256.data(), sha256.size());
  auto expected_sha256 = manifest_->image_sha256();
  auto expected_sha256_str =
      base::HexEncode(expected_sha256.data(), expected_sha256.size());
  if (sha256 != expected_sha256) {
    LOG(ERROR) << "Transferred bytes hash ("
               << base::HexEncode(sha256.data(), sha256.size())
               << ") don't match the expected hash (" << expected_sha256_str
               << ").";
    TerminateInstallation();
    return;
  }
  LOG(INFO) << "Transferred bytes hash (" << expected_sha256_str
            << ") is valid.";

  processor_->ActionComplete(this, ErrorCode::kSuccess);
}

void InstallAction::TransferTerminated(HttpFetcher* fetcher) {
  LOG(ERROR) << "Failed to complete transfer.";
  TerminateInstallation();
}

void InstallAction::StartInstallation(const std::string& url_to_fetch) {
  LOG(INFO) << "Starting installation using URL=" << url_to_fetch;
  offset_ = 0;
  hash_.reset(crypto::SecureHash::Create(crypto::SecureHash::SHA256));
  http_fetcher_->SetOffset(0);
  http_fetcher_->UnsetLength();
  http_fetcher_->BeginTransfer(url_to_fetch);
}

void InstallAction::TerminateInstallation() {
  processor_->ActionComplete(this, ErrorCode::kScaledInstallationError);
}

}  // namespace chromeos_update_engine

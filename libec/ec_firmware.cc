// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/ec_firmware.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>

namespace ec {

static const char* kSections[] = {
    [EC_IMAGE_UNKNOWN] = "UNKNOWN",
    [EC_IMAGE_RO] = "EC_RO",
    [EC_IMAGE_RW] = "EC_RW",
};

static const char* kSectionsVersion[] = {
    [EC_IMAGE_UNKNOWN] = "UNKNOWN",
    [EC_IMAGE_RO] = "RO_FRID",
    [EC_IMAGE_RW] = "RW_FWID",
};

std::unique_ptr<EcFirmware> EcFirmware::Create(const base::FilePath file) {
  if (!base::PathExists(file) || base::DirectoryExists(file)) {
    LOG(ERROR) << "Failed to find firmware file '" << file.value() << "'.";
    return nullptr;
  }

  // Using new to access non-public constructor. See https://abseil.io/tips/134.
  auto fw = base::WrapUnique(new EcFirmware());

  if (!fw->image_.Initialize(file) || !fw->image_.IsValid()) {
    LOG(ERROR) << "Failed to open firmware file '" << file.value() << "'.";
    return nullptr;
  }

  auto fmap_offset = fmap_find(fw->image_.data(), fw->image_.length());

  fw->fmap_ =
      reinterpret_cast<const struct fmap*>(fw->image_.data() + fmap_offset);

  /* The firmware file's self reported size should not be larger than the file
   * size. */
  if (fw->fmap_->size > fw->image_.length()) {
    LOG(ERROR) << "FMAP reported an image size of " << fw->fmap_->size
               << ", which is larger than the entire file size, "
               << fw->image_.length() << ", for '" << file.value() << "'.";
    return nullptr;
  }

  /* Verify size and offset for all ares. */
  for (int i = 0; i < fw->fmap_->nareas; i++) {
    if (fw->fmap_->areas[i].offset > fw->fmap_->size ||
        (fw->fmap_->size - fw->fmap_->areas[i].offset) <
            fw->fmap_->areas[i].size) {
      LOG(ERROR) << "Invalid firmware file based on FMAP. Area name: "
                 << reinterpret_cast<const char*>(fw->fmap_->areas[i].name)
                 << " size: " << fw->fmap_->areas[i].size
                 << " offset: " << fw->fmap_->areas[i].offset
                 << " fmap size: " << fw->fmap_->size;
      return nullptr;
    }
  }

  return fw;
}

bool EcFirmware::VerifyImage(const enum ec_image image) const {
  if ((image != EC_IMAGE_RO) && (image != EC_IMAGE_RW)) {
    LOG(ERROR) << "Invalid image.";
    return false;
  }
  return true;
}

std::optional<uint32_t> EcFirmware::GetOffset(const enum ec_image image) const {
  if (!VerifyImage(image)) {
    return std::nullopt;
  }

  const struct fmap_area* area = fmap_find_area(fmap_, kSections[image]);
  if (area == nullptr) {
    LOG(ERROR) << "Failed to find FMAP area " << kSections[image];
    return std::nullopt;
  }

  return area->offset;
}

std::optional<uint32_t> EcFirmware::GetSize(const enum ec_image image) const {
  if (!VerifyImage(image)) {
    return std::nullopt;
  }

  const struct fmap_area* area = fmap_find_area(fmap_, kSections[image]);
  if (area == nullptr) {
    LOG(ERROR) << "Failed to find FMAP area " << kSections[image];
    return std::nullopt;
  }

  return area->size;
}

std::optional<std::string> EcFirmware::GetVersion(
    const enum ec_image image) const {
  if (!VerifyImage(image)) {
    return std::nullopt;
  }

  const struct fmap_area* area = fmap_find_area(fmap_, kSectionsVersion[image]);
  if (area == nullptr) {
    LOG(ERROR) << "Failed to find FMAP area " << kSectionsVersion[image];
    return std::nullopt;
  }

  const char* ver = reinterpret_cast<const char*>(image_.data() + area->offset);
  if (strnlen(ver, area->size) >= area->size) {
    LOG(ERROR) << "Invalid version string.";

    return std::nullopt;
  }

  return std::string(ver);
}

base::span<const uint8_t> EcFirmware::GetData(const enum ec_image image) const {
  if (!VerifyImage(image)) {
    return {};
  }

  const struct fmap_area* area = fmap_find_area(fmap_, kSections[image]);
  if (area == nullptr) {
    LOG(ERROR) << "Failed to find FMAP area " << kSections[image];
    return {};
  }

  return {image_.data() + area->offset, area->size};
}

}  // namespace ec

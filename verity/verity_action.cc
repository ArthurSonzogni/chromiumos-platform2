// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "verity/verity_action.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/file.h>
#include <base/logging.h>
#include <base/files/scoped_temp_dir.h>

#include "verity/dm_verity_table.h"
#include "verity/file_hasher.h"

namespace verity {

namespace {

constexpr char kSourceImg[] = "source.img";
constexpr char kHashTree[] = "hashtree";

}  // namespace

bool DmVerityAction::PreVerify(const base::FilePath& payload_path,
                               const DmVerityTable& dm_verity_table) {
  int64_t payload_size = -1;
  if (!base::GetFileSize(payload_path, &payload_size)) {
    LOG(ERROR) << "Failed to get payload size.";
    return false;
  }

  const auto& data_dev = dm_verity_table.GetDataDevice();
  const auto source_img_bytes = data_dev.NumBytes();
  // Exit early if there is something fishy about the payload.
  if (payload_size < source_img_bytes) {
    LOG(ERROR) << "Payload size is invalid based on table, too small.";
    return false;
  }
  if (payload_size == source_img_bytes) {
    LOG(ERROR) << "Payload size is invalid based on table, "
               << "should not be the same as source image bytes.";
    return false;
  }
  return true;
}

bool DmVerityAction::TruncatePayloadToSource(
    const base::FilePath& payload_path,
    const base::FilePath& source_img_path,
    const DmVerityTable& dm_verity_table,
    std::unique_ptr<base::File>* out_source_img_file) {
  if (!base::CopyFile(payload_path, source_img_path)) {
    LOG(ERROR) << "Failed to copy payload into source image.";
    return false;
  }
  auto source_img_file = std::make_unique<base::File>(
      source_img_path,
      base::File::FLAG_OPEN | base::File::FLAG_READ | base::File::FLAG_WRITE);
  if (!source_img_file->IsValid()) {
    LOG(ERROR) << "Failed to open source image.";
    return false;
  }

  const auto& data_dev = dm_verity_table.GetDataDevice();
  const auto source_img_bytes = data_dev.NumBytes();
  if (!source_img_file->SetLength(source_img_bytes)) {
    LOG(ERROR) << "Failed to set source image length.";
    return false;
  }

  *out_source_img_file = std::move(source_img_file);
  return true;
}

int DmVerityAction::Verify(const base::FilePath& payload_path,
                           const DmVerityTable& dm_verity_table) {
  if (!DmVerityAction::PreVerify(payload_path, dm_verity_table)) {
    LOG(ERROR) << "Failed to pre-verify payload.";
    return -1;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir() || !temp_dir.IsValid()) {
    LOG(ERROR) << "Failed to create temporary directory.";
    return -1;
  }

  // Truncate the payload to actual source image size.
  std::unique_ptr<base::File> source_img_file;
  base::FilePath source_img_path(temp_dir.GetPath().Append(kSourceImg));
  if (!DmVerityAction::TruncatePayloadToSource(
          payload_path, source_img_path, dm_verity_table, &source_img_file)) {
    LOG(ERROR) << "Failed to truncate payload to source image.";
    return -1;
  }

  base::FilePath hashtree_path(temp_dir.GetPath().Append(kHashTree));
  auto hashtree_file = std::make_unique<base::File>(
      hashtree_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                         base::File::FLAG_WRITE);
  if (!hashtree_file->IsValid()) {
    LOG(ERROR) << "Failed to open hashtree.";
    return -1;
  }

  const auto& alg = dm_verity_table.GetAlgorithm();
  const auto& salt = dm_verity_table.GetSalt();
  const auto& data_dev = dm_verity_table.GetDataDevice();
  // We only support verifying colocated hash devices, so no need to fetch it
  // (for the time being).
  verity::FileHasher hasher(std::move(source_img_file),
                            std::move(hashtree_file), data_dev.block_count,
                            alg.c_str());
  LOG_IF(FATAL, !hasher.Initialize()) << "Failed to initialize hasher";
  if (salt)
    hasher.set_salt(std::string(salt->data()).c_str());
  LOG_IF(FATAL, !hasher.Hash()) << "Failed to hash hasher";
  LOG_IF(FATAL, !hasher.Store()) << "Failed to store hasher";
  const auto& actual_table =
      hasher.GetRawTable(DmVerityTable::HashPlacement::COLOCATED);

  if (dm_verity_table != actual_table) {
    LOG(ERROR) << "Tables are not the same.";
    return -1;
  }

  std::string hashtree_contents;
  if (!base::ReadFileToString(hashtree_path, &hashtree_contents)) {
    LOG(ERROR) << "Failed to read hashtree contents.";
    return -1;
  }

  if (!base::AppendToFile(source_img_path, hashtree_contents)) {
    LOG(ERROR) << "Failed to colocated hashtree onto source image.";
    return -1;
  }

  if (!base::ContentsEqual(source_img_path, payload_path)) {
    LOG(ERROR) << "Final payload mismatch, did you forget to append the "
                  "hashtree fully?";
    return -1;
  }

  return 0;
}

}  // namespace verity

// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/blob_file_writer.h"

#include "update_engine/common/utils.h"

#include <base/logging.h>

namespace chromeos_update_engine {

off_t BlobFileWriter::StoreBlob(const brillo::Blob& blob) {
  base::AutoLock auto_lock(blob_mutex_);
  if (!utils::PWriteAll(blob_fd_, blob.data(), blob.size(), *blob_file_size_))
    return -1;

  off_t result = *blob_file_size_;
  *blob_file_size_ += blob.size();

  stored_blobs_++;
  if (total_blobs_ > 0 && (10 * (stored_blobs_ - 1) / total_blobs_) !=
                              (10 * stored_blobs_ / total_blobs_)) {
    LOG(INFO) << (100 * stored_blobs_ / total_blobs_) << "% complete "
              << stored_blobs_ << "/" << total_blobs_
              << " ops (output size: " << *blob_file_size_ << ")";
  }
  return result;
}

void BlobFileWriter::IncTotalBlobs(size_t increment) {
  base::AutoLock auto_lock(blob_mutex_);
  total_blobs_ += increment;
}

}  // namespace chromeos_update_engine

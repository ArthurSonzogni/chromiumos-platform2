// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_BLOB_FILE_WRITER_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_BLOB_FILE_WRITER_H_

#include <base/synchronization/lock.h>
#include <brillo/secure_blob.h>

namespace chromeos_update_engine {

class BlobFileWriter {
 public:
  // Create the BlobFileWriter object that will manage the blobs stored to
  // |blob_fd| in a thread safe way.
  BlobFileWriter(int blob_fd, off_t* blob_file_size)
      : blob_fd_(blob_fd), blob_file_size_(blob_file_size) {}
  BlobFileWriter(const BlobFileWriter&) = delete;
  BlobFileWriter& operator=(const BlobFileWriter&) = delete;

  // Store the passed |blob| in the blob file. Returns the offset at which it
  // was stored, or -1 in case of failure.
  off_t StoreBlob(const brillo::Blob& blob);

  // Increase |total_blobs| by |increment|. Thread safe.
  void IncTotalBlobs(size_t increment);

 private:
  size_t total_blobs_{0};
  size_t stored_blobs_{0};

  // The file and its size are protected with the |blob_mutex_|.
  int blob_fd_;
  off_t* blob_file_size_;

  base::Lock blob_mutex_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_GENERATOR_BLOB_FILE_WRITER_H_

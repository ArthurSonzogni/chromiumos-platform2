// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_ANNOTATED_OPERATION_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_ANNOTATED_OPERATION_H_

#include <ostream>  // NOLINT(readability/streams)
#include <string>

#include <brillo/secure_blob.h>

#include "update_engine/payload_generator/blob_file_writer.h"
#include "update_engine/update_metadata.pb.h"

namespace chromeos_update_engine {

struct AnnotatedOperation {
  // The name given to the operation, for logging and debugging purposes only.
  // This normally includes the path to the file and the chunk used, if any.
  std::string name;

  // The InstallOperation, as defined by the protobuf.
  InstallOperation op;

  // Writes |blob| to the end of |blob_file|. It sets the data_offset and
  // data_length in AnnotatedOperation to match the offset and size of |blob|
  // in |blob_file|.
  bool SetOperationBlob(const brillo::Blob& blob, BlobFileWriter* blob_file);
};

// For logging purposes.
std::ostream& operator<<(std::ostream& os, const AnnotatedOperation& aop);

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_GENERATOR_ANNOTATED_OPERATION_H_

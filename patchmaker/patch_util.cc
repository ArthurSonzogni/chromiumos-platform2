// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchmaker/patch_util.h"

#include <memory>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <bsdiff/bsdiff.h>
#include <bsdiff/bspatch.h>
#include <bsdiff/patch_writer_factory.h>

#include "patchmaker/file_util.h"

namespace util {

bool DoBsDiff(const base::FilePath& old_file,
              const base::FilePath& new_file,
              const base::FilePath& patch_file) {
  std::optional<brillo::Blob> old_data, new_data;
  std::unique_ptr<bsdiff::PatchWriterInterface> bsdiff_patch_writer =
      bsdiff::CreateBsdiffPatchWriter(patch_file.value());

  old_data = ReadFileToBlob(old_file);
  if (!old_data.has_value()) {
    LOG(ERROR) << "Failed to read old file for bsdiff";
    return false;
  }

  new_data = ReadFileToBlob(new_file);
  if (!new_data.has_value()) {
    LOG(ERROR) << "Failed to read new file for bsdiff";
    return false;
  }

  return 0 == bsdiff::bsdiff(old_data->data(), old_data->size(),
                             new_data->data(), new_data->size(),
                             bsdiff_patch_writer.get(), nullptr);
}

bool DoBsPatch(const base::FilePath& old_file,
               const base::FilePath& new_file,
               const base::FilePath& patch_file) {
  return 0 == bsdiff::bspatch(old_file.value().c_str(),
                              new_file.value().c_str(),
                              patch_file.value().c_str(), nullptr, nullptr);
}

}  // namespace util

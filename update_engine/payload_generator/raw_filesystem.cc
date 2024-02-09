// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/raw_filesystem.h"

#include <memory>

#include "update_engine/common/utils.h"
#include "update_engine/payload_generator/extent_ranges.h"
#include "update_engine/update_metadata.pb.h"

using std::unique_ptr;

namespace chromeos_update_engine {

unique_ptr<RawFilesystem> RawFilesystem::Create(const std::string& filename,
                                                uint64_t block_size,
                                                uint64_t block_count) {
  unique_ptr<RawFilesystem> result(new RawFilesystem());
  result->filename_ = filename;
  result->block_size_ = block_size;
  result->block_count_ = block_count;
  return result;
}

size_t RawFilesystem::GetBlockSize() const {
  return block_size_;
}

size_t RawFilesystem::GetBlockCount() const {
  return block_count_;
}

bool RawFilesystem::GetFiles(std::vector<File>* files) const {
  files->clear();
  File file;
  file.name = filename_;
  file.extents = {ExtentForRange(0, block_count_)};
  files->push_back(file);
  return true;
}

}  // namespace chromeos_update_engine

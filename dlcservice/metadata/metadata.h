// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_METADATA_METADATA_H_
#define DLCSERVICE_METADATA_METADATA_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <base/files/file_path.h>
#include <base/values.h>
#include <brillo/brillo_export.h>

#include "dlcservice/metadata/compressor_interface.h"
#include "dlcservice/metadata/metadata_interface.h"

namespace dlcservice::metadata {

// The default maximum size of metadata files.
BRILLO_EXPORT extern const size_t kMaxMetadataFileSize;
// The prefix of metadata files. Metadata files are named in the format
// `<kMetadataPrefix><file_id>`.
BRILLO_EXPORT extern const std::string_view kMetadataPrefix;

// Load DLC metadata for the dlcservice daemon.
class BRILLO_EXPORT Metadata : public MetadataInterface {
 public:
  explicit Metadata(
      const base::FilePath& metadata_path,
      size_t max_file_size = kMaxMetadataFileSize,
      std::unique_ptr<CompressorInterface> compressor = nullptr,
      std::unique_ptr<CompressorInterface> decompressor = nullptr);
  ~Metadata() override = default;

  Metadata(const Metadata&) = delete;
  Metadata& operator=(const Metadata&) = delete;

  bool Initialize() override;
  std::optional<Entry> Get(const DlcId& id) override;
  bool Set(const DlcId& id, const Entry& entry) override;
  bool LoadMetadata(const DlcId& id) override;
  void UpdateFileIds() override;

  const base::Value::Dict& GetCache() const override { return cache_; }
  const std::set<DlcId>& GetFileIds() const override { return file_ids_; }

 private:
  // Estimate the compressed size of given metadata, returns `-1` on error.
  int CompressionSize(const std::string& metadata);

  // Flush the cache into the metadata file.
  bool FlushCache();

  // Flush `compressed_metadata_` to a metadata file, and name it
  // `kMetadataPrefix``file_id`.
  bool FlushBuffer(const DlcId& file_id);

  const base::FilePath metadata_path_;
  // The maximum size of each metadata file, align with the filesystem block
  // size to get performance and it should be in sync with the chromite.
  const size_t max_file_size_;
  // Cache currently loaded and parsed metadata file for read or modify.
  base::Value::Dict cache_;
  // The `file_id`s inside current metadata directory.
  std::set<DlcId> file_ids_;
  // The buffer for read and write compressed metadata.
  std::string compressed_metadata_;
  // The compressor and decompressor for metadata compression/decompression.
  std::unique_ptr<CompressorInterface> compressor_;
  std::unique_ptr<CompressorInterface> decompressor_;
};

}  // namespace dlcservice::metadata

#endif  // DLCSERVICE_METADATA_METADATA_H_

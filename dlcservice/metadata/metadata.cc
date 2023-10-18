// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/metadata/metadata.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>

#include "dlcservice/metadata/compressor_interface.h"
#include "dlcservice/metadata/metadata_interface.h"
#include "dlcservice/metadata/zlib_compressor.h"

namespace dlcservice::metadata {

namespace {
constexpr char kMetadataFilePattern[] = "_metadata_*";

constexpr char kManifest[] = "manifest";
constexpr char kTable[] = "table";

constexpr char kKeyStringFactoryInstall[] = "factory-install";
constexpr char kKeyStringPowerwashSafe[] = "powerwash-safe";
constexpr char kKeyStringPreloadAllowed[] = "preload-allowed";
}  // namespace

const size_t kMaxMetadataFileSize = 4096;
const std::string_view kMetadataPrefix("_metadata_");

Metadata::Metadata(const base::FilePath& metadata_path,
                   size_t max_file_size,
                   std::unique_ptr<CompressorInterface> compressor,
                   std::unique_ptr<CompressorInterface> decompressor)
    : metadata_path_(metadata_path),
      max_file_size_(max_file_size),
      compressor_(std::move(compressor)),
      decompressor_(std::move(decompressor)) {
  if (!compressor_) {
    compressor_ = std::make_unique<ZlibCompressor>();
  }
  if (!decompressor_) {
    decompressor_ = std::make_unique<ZlibDecompressor>();
  }
  compressed_metadata_.reserve(max_file_size_);
}

bool Metadata::Initialize() {
  UpdateFileIds();
  return compressor_->Initialize() && decompressor_->Initialize();
}

std::optional<Metadata::Entry> Metadata::Get(const DlcId& id) {
  if (!LoadMetadata(id)) {
    LOG(ERROR) << "Failed to load the metadata data file for DLC=" << id;
    return std::nullopt;
  }

  auto* metadata_val = cache_.FindDict(id);
  if (!metadata_val) {
    LOG(ERROR) << "Unable to find DLC=" << id << " in the metadata.";
    return std::nullopt;
  }

  Metadata::Entry entry;

  auto* manifest_val = metadata_val->FindDict(kManifest);
  if (!manifest_val) {
    LOG(ERROR) << "Could not get manifest for DLC=" << id;
    return std::nullopt;
  }
  entry.manifest = manifest_val->Clone();

  auto* table_str = metadata_val->FindString(kTable);
  if (!table_str) {
    LOG(ERROR) << "Could not get table for DLC=" << id;
    return std::nullopt;
  }
  entry.table = *table_str;

  return entry;
}

bool Metadata::Set(const DlcId& id, const Metadata::Entry& entry) {
  // Load, modify and save the metadata file that contains the target DLC.
  if (!LoadMetadata(id)) {
    cache_.clear();
  }

  cache_.Set(id, base::Value::Dict()
                     .Set(kManifest, entry.manifest.Clone())
                     .Set(kTable, entry.table));
  // Update the `file_ids_` since new file may be created after modification.
  if (FlushCache()) {
    UpdateFileIds();
    return true;
  }

  return false;
}

int Metadata::CompressionSize(const std::string& metadata) {
  auto compressor_copy = compressor_->Clone();
  if (!compressor_copy) {
    return -1;
  }

  auto data_out = compressor_copy->Process(metadata, /*flush=*/true);
  if (!data_out) {
    return -1;
  }

  return data_out->size();
}

bool Metadata::FlushCache() {
  // The first of ascending DLC IDs added to current compressed metadata file
  // buffer, it will be used as the `file_id` to name the metadata file.
  DlcId min_id;
  compressor_->Reset();
  compressed_metadata_.clear();
  for (const auto& [id, metadata] : cache_) {
    auto metadata_str = base::WriteJson(metadata);
    if (!metadata_str) {
      LOG(ERROR) << "Failed to convert metadata to JSON for DLC=" << id;
      return false;
    }
    std::string metadata_entry =
        base::StringPrintf("\"%s\":%s,", id.c_str(), metadata_str->c_str());

    int comp_size = CompressionSize(metadata_entry);
    if (comp_size < 0) {
      LOG(ERROR) << "Unable to estimate metadata compression size, flushing "
                    "metadata failed.";
      return false;
    }
    if (compressed_metadata_.size() + comp_size > max_file_size_) {
      // If unable to fit into current file, flush current compressed metadata
      // to file and start a new output stream to re-process the metadata.
      if (!FlushBuffer(min_id)) {
        return false;
      }
      min_id.clear();

      comp_size = CompressionSize(metadata_entry);
      if (comp_size < 0 || comp_size > max_file_size_) {
        LOG(ERROR) << "Unable to save metadata for DLC=" << id
                   << " due to compression size error, size=" << comp_size
                   << " max_file_size=" << max_file_size_;
        return false;
      }
    }

    auto buffer = compressor_->Process(metadata_entry, /*flush=*/false);
    if (!buffer) {
      LOG(ERROR) << "Unable to compress metadata for DLC=" << id;
      return false;
    }
    compressed_metadata_.append(*buffer);
    if (min_id.empty())
      min_id = id;
  }

  return FlushBuffer(min_id);
}

bool Metadata::FlushBuffer(const DlcId& file_id) {
  // Save to file.
  bool ret = true;
  if (file_id.size()) {
    // Flush all the data to output buffer.
    auto buffer = compressor_->Process(/*data_in=*/"", /*flush=*/true);
    if (buffer) {
      compressed_metadata_.append(*buffer);
    } else {
      LOG(ERROR) << "Unable to flush the compressed metadata";
      ret = false;
    }

    ret =
        ret && compressed_metadata_.size() &&
        base::WriteFile(
            metadata_path_.Append(std::string(kMetadataPrefix).append(file_id)),
            compressed_metadata_);
  }
  if (!ret) {
    LOG(ERROR) << "Failed to save the metadata file=" << file_id;
  }
  compressor_->Reset();
  compressed_metadata_.clear();
  return ret;
}

void Metadata::UpdateFileIds() {
  file_ids_.clear();
  base::FileEnumerator file_enumerator(
      metadata_path_, false, base::FileEnumerator::FILES,
      base::FilePath::StringType(kMetadataFilePattern));
  for (base::FilePath name = file_enumerator.Next(); !name.empty();
       name = file_enumerator.Next()) {
    // Skip `_metadata_`.
    if (name.BaseName().value().size() == kMetadataPrefix.size())
      continue;
    file_ids_.emplace(name.BaseName().value(), kMetadataPrefix.size());
  }
}

bool Metadata::LoadMetadata(const DlcId& id) {
  if (cache_.FindDict(id))
    return true;

  LOG(INFO) << "Loading metadata for DLC=" << id;
  // Locate the metadata file by binary search the `file_id`.
  auto file_id = file_ids_.upper_bound(id);
  if (file_id == file_ids_.begin()) {
    LOG(ERROR) << "Unable to find metadata for DLC=" << id;
    return false;
  }

  // Read and decompress metadata.
  base::FilePath fp =
      metadata_path_.Append(std::string(kMetadataPrefix).append(*(--file_id)));
  if (!base::ReadFileToString(fp, &compressed_metadata_)) {
    LOG(ERROR) << "Failed to read DLC metadata file=" << fp.value();
    return false;
  }

  if (!decompressor_->Reset()) {
    LOG(ERROR) << "Failed to reset decompressor.";
    return false;
  }
  auto decompressed_metadata =
      decompressor_->Process(compressed_metadata_, /*flush=*/true);
  compressed_metadata_.clear();
  if (!decompressed_metadata) {
    return false;
  }

  // Parse decompressed metadata json.
  auto metadata_val = base::JSONReader::ReadAndReturnValueWithError(
      std::string("{").append(*decompressed_metadata).append("}"),
      base::JSON_ALLOW_TRAILING_COMMAS);
  if (!metadata_val.has_value()) {
    LOG(ERROR) << "Could not parse the DLC metadata as JSON. Error: "
               << metadata_val.error().message;
    return false;
  }

  if (!metadata_val->is_dict()) {
    LOG(ERROR) << "DLC metadata content is not dictionary.";
    return false;
  }

  cache_ = std::move(metadata_val->GetDict());
  return true;
}

DlcIdList Metadata::ListDlcIds(const FilterKey& filter_key,
                               const base::Value& filter_val) {
  auto key_str = FilterKeyToString(filter_key);
  if (!key_str)
    return {};

  if (const auto& idx = GetIndex(*key_str)) {
    LOG(INFO) << "Get from indexed DLC IDs.";
    return *idx;
  }

  // Lookup in metadata.
  DlcIdList ids;
  for (const auto& file_id : GetFileIds()) {
    if (!LoadMetadata(file_id)) {
      LOG(ERROR) << "Failed to Load DLC metadata file=" << file_id;
      continue;
    }

    for (const auto& [id, val] : GetCache()) {
      if (filter_key != FilterKey::kNone) {
        const auto* manifest_dict = val.GetDict().FindDict(kManifest);
        if (!manifest_dict)
          continue;
        const auto* manifest_val = manifest_dict->Find(*key_str);
        if (!manifest_val || *manifest_val != filter_val)
          continue;
      }

      ids.push_back(id);
    }
  }
  return ids;
}

std::optional<DlcIdList> Metadata::GetIndex(const std::string& key) {
  if (key.empty())
    return std::nullopt;

  // TODO(b/303259102): Better/stricter index file naming to prevent collision.
  auto idx_file = base::StringPrintf("_%s_", key.c_str());
  std::replace(idx_file.begin(), idx_file.end(), '-', '_');
  auto idx_path = metadata_path_.Append(idx_file);

  std::string idx_str;
  if (!base::ReadFileToString(idx_path, &idx_str)) {
    LOG(ERROR) << "Failed to read the index file.";
    return std::nullopt;
  }

  return base::SplitString(idx_str, base::kWhitespaceASCII,
                           base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
}

std::optional<std::string> Metadata::FilterKeyToString(
    const Metadata::FilterKey& key_enum) {
  switch (key_enum) {
    case FilterKey::kNone:
      return "";
    case FilterKey::kFactoryInstall:
      return kKeyStringFactoryInstall;
    case FilterKey::kPowerwashSafe:
      return kKeyStringPowerwashSafe;
    case FilterKey::kPreloadAllowed:
      return kKeyStringPreloadAllowed;
    default:
      LOG(ERROR) << "Unsupported filter key.";
      return std::nullopt;
  }
}

}  // namespace dlcservice::metadata

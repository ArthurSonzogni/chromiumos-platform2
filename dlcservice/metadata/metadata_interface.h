// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_METADATA_METADATA_INTERFACE_H_
#define DLCSERVICE_METADATA_METADATA_INTERFACE_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <base/values.h>

namespace dlcservice::metadata {

using DlcId = std::string;
using DlcIdList = std::vector<DlcId>;

class MetadataInterface {
 public:
  MetadataInterface() = default;
  virtual ~MetadataInterface() = default;

  MetadataInterface(const MetadataInterface&) = delete;
  MetadataInterface& operator=(const MetadataInterface&) = delete;

  struct Entry {
    base::Value::Dict manifest;
    std::string table;
  };

  enum class FilterKey {
    kNone,
    kFactoryInstall,
    kPowerwashSafe,
    kPreloadAllowed
  };

  // Initialize the metadata.
  virtual bool Initialize() = 0;

  // Get DLC metadata `Entry` by ID. Returns nullopt on error.
  virtual std::optional<Entry> Get(const DlcId& id) = 0;

  // Set a DLC metadata `Entry`, returns true if success.
  // Requires writable rootfs.
  virtual bool Set(const DlcId& id, const Entry& entry) = 0;

  // Load, parse and cache metadata file that contains given `DlcId`.
  virtual bool LoadMetadata(const DlcId& id) = 0;

  // Update the `file_id`s inside current metadata directory. This needs to be
  // called after constructed the object.
  virtual void UpdateFileIds() = 0;

  // Get a list of DLC IDs, optionally filter and return only the ones that
  // match given manifest key and value. Set the key to `FilterKey::kNone` to
  // skip filtering.
  // NOTE: May be slow for unindexed filter keys.
  virtual DlcIdList ListDlcIds(const FilterKey& filter_key,
                               const base::Value& filter_val) = 0;

  // Convert `FilterKey` enum to std::string. Returns nullopt on error.
  virtual std::optional<std::string> FilterKeyToString(
      const FilterKey& key_enum) = 0;

  // Getter for cached raw data.
  virtual const base::Value::Dict& GetCache() const = 0;

  // Getter for file_ids.
  virtual const std::set<DlcId>& GetFileIds() const = 0;
};

}  // namespace dlcservice::metadata

#endif  // DLCSERVICE_METADATA_METADATA_INTERFACE_H_

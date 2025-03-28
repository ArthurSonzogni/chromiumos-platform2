// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IMAGELOADER_MANIFEST_H_
#define IMAGELOADER_MANIFEST_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/values.h>
#include <brillo/brillo_export.h>

namespace imageloader {

BRILLO_EXPORT extern const char kDlcRedactedId[];
BRILLO_EXPORT extern const char kDlcRedactedSize[];
BRILLO_EXPORT extern const char kDlcRedactedHash[];

// The supported file systems for images.
enum class BRILLO_EXPORT FileSystem { kExt2, kExt4, kSquashFS, kBlob };

// The artifacts meta(data) for DLC images.
struct BRILLO_EXPORT ArtifactsMeta {
  bool valid = false;
  std::string uri;

  auto operator<=>(const ArtifactsMeta&) const = default;
};

using ArtifactsMeta = struct ArtifactsMeta;

// A class to parse and store imageloader.json manifest. See manifest.md.
//
// NOTE: For developers, remember to update manifest.md when adding/removing
// fields into the manifest.
class BRILLO_EXPORT Manifest {
 public:
  Manifest() = default;
  virtual ~Manifest() = default;
  Manifest(const Manifest&) = delete;
  Manifest& operator=(const Manifest&) = delete;

  // Equals comparator.
  bool operator==(const Manifest& rhs) const;

  // Parse the manifest raw string. Return true if successful.
  bool ParseManifest(const std::string& manifest_raw);
  // Parse the manifest raw dictionary value. Return true if successful.
  bool ParseManifest(const base::Value::Dict& manifest_dict);

  // Getters for required manifest fields:
  int manifest_version() const { return manifest_version_; }
  const std::vector<uint8_t>& image_sha256() const { return image_sha256_; }
  const std::vector<uint8_t>& table_sha256() const { return table_sha256_; }
  const std::string& version() const { return version_; }

  // Getters for optional manifest fields:
  FileSystem fs_type() const { return fs_type_; }
  const std::string& id() const { return id_; }
  const std::string& package() const { return package_; }
  const std::string& name() const { return name_; }
  const std::string& image_type() const { return image_type_; }
  int64_t preallocated_size() const { return preallocated_size_; }
  int64_t size() const { return size_; }
  bool is_removable() const { return is_removable_; }
  // Indicator for |dlcservice| to allow preloading at a per DLC level.
  bool preload_allowed() const { return preload_allowed_; }
  // Indicator for |dlcservice| to allow factory installed DLC images.
  bool factory_install() const { return factory_install_; }
  bool mount_file_required() const { return mount_file_required_; }
  bool reserved() const { return reserved_; }
  bool critical_update() const { return critical_update_; }
  const std::string& description() const { return description_; }
  const std::map<std::string, std::string> metadata() const {
    return metadata_;
  }
  bool use_logical_volume() const { return use_logical_volume_; }
  bool scaled() const { return scaled_; }
  bool powerwash_safe() const { return powerwash_safe_; }
  bool user_tied() const { return user_tied_; }
  const ArtifactsMeta& artifacts_meta() const { return artifacts_meta_; }
  bool force_ota() const { return force_ota_; }

  const std::string& sanitized_id() const { return sanitized_id_; }
  const std::string& sanitized_size() const { return sanitized_size_; }
  const std::string& sanitized_preallocated_size() const {
    return sanitized_preallocated_size_;
  }
  const std::string& sanitized_image_sha256() const {
    return sanitized_image_sha256_;
  }
  const std::set<std::string>& attributes() const { return attributes_; }

 private:
  // Required manifest fields:
  int manifest_version_ = 0;
  std::vector<uint8_t> image_sha256_;
  std::vector<uint8_t> table_sha256_;
  std::string version_;

  // Optional manifest fields:
  FileSystem fs_type_ = FileSystem::kExt4;
  std::string id_;
  std::string package_;
  std::string name_;
  std::string image_type_;
  int64_t preallocated_size_ = 0;
  int64_t size_ = 0;
  bool is_removable_ = false;
  bool preload_allowed_ = false;
  bool factory_install_ = false;
  bool mount_file_required_ = false;
  bool reserved_ = false;
  bool critical_update_ = false;
  std::string description_;
  std::map<std::string, std::string> metadata_;
  bool use_logical_volume_ = false;
  bool scaled_ = false;
  bool powerwash_safe_ = false;
  bool user_tied_ = false;
  ArtifactsMeta artifacts_meta_{
      .valid = false,
  };
  bool force_ota_ = false;
  std::set<std::string> attributes_;

  // Sanitized fields:
  std::string sanitized_id_;
  std::string sanitized_size_;
  std::string sanitized_preallocated_size_;
  std::string sanitized_image_sha256_;
};

}  // namespace imageloader

#endif  // IMAGELOADER_MANIFEST_H_

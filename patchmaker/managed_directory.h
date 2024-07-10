// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHMAKER_MANAGED_DIRECTORY_H_
#define PATCHMAKER_MANAGED_DIRECTORY_H_

#include <vector>

#include <base/files/file_path.h>

#include "patchmaker/proto_bindings/patch_manifest.pb.h"

constexpr char kPatchManifestFilename[] = "patch_manifest.textproto";
constexpr char kPatchExtension[] = "_patch";

// Simply, two files that have sizes within 20% will be clustered together.
const float kClusterRatio = 1.2;

class ManagedDirectory {
 public:
  // We are on the encode path, and we are creating a new managed directory. We
  // may or may not have a precomputed patch manifest to follow
  bool CreateNew(const base::FilePath& managed_dir_root,
                 std::optional<base::FilePath> input_manifest_path);

  bool Encode(const base::FilePath& src_path,
              const base::FilePath& dest_path,
              const std::vector<base::FilePath>& immutable_paths);

  // We are on the decode path. The input managed_dir_path may not be the root
  // of the managed directory, as the caller may be preparing to decode an
  // individual file or sub-directory.
  bool CreateFromExisting(const base::FilePath& path_within_managed_dir);

  bool Decode(const base::FilePath& target_path,
              const base::FilePath& dest_path);

 private:
  bool ManifestEntryIsUnderTargetPath(const base::FilePath& target_path,
                                      const PatchManifestEntry& entry);

  bool CommitManifestToFile();

  base::FilePath directory_root_;
  PatchManifest manifest_;
};

#endif  // PATCHMAKER_MANAGED_DIRECTORY_H_

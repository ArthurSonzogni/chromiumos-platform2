// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchmaker/managed_directory.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/proto_file_io.h>

#include "patchmaker/compression_util.h"
#include "patchmaker/directory_util.h"
#include "patchmaker/file_util.h"
#include "patchmaker/patch_util.h"

namespace {

std::vector<std::vector<base::FilePath>> ClusterFilesInDirectoryBySize(
    const base::FilePath& src_path) {
  util::SortableFileList file_entries = util::GetFilesInDirectory(src_path);

  std::vector<std::vector<base::FilePath>> clusters{};

  // Sort files by size so we can do a simple rolling cluster in one pass
  std::sort(file_entries.begin(), file_entries.end(),
            [](auto& left, auto& right) { return left.second < right.second; });

  int num_clustered_files = 0, first_size_in_cluster;

  for (const auto& entry : file_entries) {
    if (clusters.empty()) {
      clusters.emplace_back(std::vector<base::FilePath>{entry.first});
      first_size_in_cluster = entry.second;
      continue;
    }

    if (entry.second > kClusterRatio * first_size_in_cluster) {
      num_clustered_files += clusters.back().size();
      clusters.emplace_back(std::vector<base::FilePath>());
      first_size_in_cluster = entry.second;
    }
    clusters.back().emplace_back(entry.first);
  }
  num_clustered_files += clusters.back().size();

  CHECK(num_clustered_files == file_entries.size());
  return clusters;
}

std::vector<int> IndicesMatchingNameAndDepth(
    const base::FilePath& path, const std::vector<base::FilePath>& files) {
  std::vector<int> matching_indices;

  int reference_depth = path.GetComponents().size();
  const base::FilePath reference_name = path.BaseName();

  for (int i = 0; i < files.size(); i++) {
    if (files[i].BaseName() == reference_name &&
        files[i].GetComponents().size() == reference_depth) {
      matching_indices.emplace_back(i);
    }
  }
  return matching_indices;
}

std::vector<int> IndicesMatchingExtension(
    const base::FilePath& path, const std::vector<base::FilePath>& files) {
  std::vector<int> matching_indices;

  const std::string reference_extension = path.FinalExtension();

  for (int i = 0; i < files.size(); i++) {
    if (files[i].FinalExtension() == reference_extension) {
      matching_indices.emplace_back(i);
    }
  }
  return matching_indices;
}

std::vector<int> IndicesMatchingAll(const base::FilePath& path,
                                    const std::vector<base::FilePath>& files) {
  std::vector<int> matching_indices;

  for (int i = 0; i < files.size(); i++) {
    matching_indices.emplace_back(i);
  }
  return matching_indices;
}

// Receive a cluster of similarly-sized files, and iteratively choose which
// files are shipped full-size, and which are patched.
std::vector<PatchManifestEntry> EncodeFileClusterFromSrcToDest(
    const std::vector<base::FilePath>& cluster,
    const base::FilePath& src_path,
    const base::FilePath& dest_path) {
  std::vector<base::FilePath> full_files;
  std::vector<PatchManifestEntry> manifest_entries_for_cluster;

  std::optional<size_t> compressed_size;
  int64_t patched_size;

  base::FilePath temp_patch_file;
  base::CreateTemporaryFile(&temp_patch_file);

  for (const auto& entry : cluster) {
    PatchManifestEntry manifest_entry;
    std::set<int> visited_indices;

    // Get baseline size for this file using compression
    compressed_size = util::GetCompressedSize(entry);
    CHECK(compressed_size.has_value());

    int selected_base_candidate_idx = -1;
    // Iterate a prioritized list of candidate base files
    for (const auto& list : {IndicesMatchingNameAndDepth(entry, full_files),
                             IndicesMatchingExtension(entry, full_files),
                             IndicesMatchingAll(entry, full_files)}) {
      for (auto base_candidate_idx : list) {
        // Continue to the next file if we'd already visited this index
        if (!visited_indices.insert(base_candidate_idx).second)
          continue;

        // Set selected_base_candidate_idx and break if better than zstd
        if (!util::DoBsDiff(full_files[base_candidate_idx], entry,
                            temp_patch_file)) {
          // Something went wrong, continue to the next file
          continue;
        } else {
          base::GetFileSize(temp_patch_file, &patched_size);
          if (patched_size < compressed_size) {
            selected_base_candidate_idx = base_candidate_idx;
            break;
          }
        }
      }
      // Continue breaking out of the loop if we found a good base
      if (selected_base_candidate_idx >= 0) {
        break;
      }
    }

    base::FilePath dest_path_absl, src_path_rel, base_path_rel, patch_path_rel;

    // Grab destination file for this entry in the destination directory
    dest_path_absl = util::AppendRelativePathOn(src_path, entry, dest_path);
    src_path_rel =
        util::AppendRelativePathOn(src_path, entry, base::FilePath());

    std::string md5_str = util::GetMD5SumForFile(entry);

    if (selected_base_candidate_idx < 0) {
      // We didn't find a good patch recipe, copy the full file
      full_files.emplace_back(entry);
      base::CopyFile(entry, dest_path_absl);
      LOG(INFO) << "Compression is best, installed full at " << dest_path_absl;
    } else {
      // We found a good patch recipe, copy the patch
      dest_path_absl = dest_path_absl.AddExtension(kPatchExtension);
      base::CopyFile(temp_patch_file, dest_path_absl);
      LOG(INFO) << "Found suitable patch, installed at " << dest_path_absl;

      // Manifest bookkeeping
      base_path_rel = util::AppendRelativePathOn(
          src_path, full_files[selected_base_candidate_idx], base::FilePath());

      patch_path_rel = util::AppendRelativePathOn(dest_path, dest_path_absl,
                                                  base::FilePath());

      manifest_entry.set_base_file_name(base_path_rel.value());
      manifest_entry.set_patch_file_name(patch_path_rel.value());
    }

    manifest_entry.set_original_file_md5_checksum(md5_str);
    manifest_entry.set_original_file_name(src_path_rel.value());

    manifest_entries_for_cluster.emplace_back(manifest_entry);
  }

  return manifest_entries_for_cluster;
}

bool GetManagedDirectoryRoot(const base::FilePath& target_path,
                             base::FilePath* directory_root) {
  base::FilePath manifest_dir = target_path;

  // Iterate upwards until we locate the patch manifest
  while (!util::IsFile(manifest_dir.Append(kPatchManifestFilename))) {
    if (manifest_dir.GetComponents().size() <= 1) {
      // We already checked the root, quit
      return false;
    }

    // Move up to parent directory
    manifest_dir = manifest_dir.DirName();
  }
  *directory_root = manifest_dir;
  return true;
}

}  // namespace

// We are on the encode path, and we are creating a new managed directory. We
// may or may not have a precomputed patch manifest to follow
bool ManagedDirectory::CreateNew(
    const base::FilePath& managed_dir_root,
    std::optional<base::FilePath> input_manifest_path) {
  directory_root_ = managed_dir_root;
  if (!input_manifest_path.has_value()) {
    return true;
  }

  if (!util::IsFile(*input_manifest_path)) {
    LOG(ERROR) << "File " << *input_manifest_path << " doesn't exist";
    return false;
  }
  if (!brillo::ReadTextProtobuf(*input_manifest_path, &manifest_)) {
    LOG(ERROR) << "Couldn't load provided patch manifest";
    return false;
  }

  return true;
}

bool ManagedDirectory::Encode(const base::FilePath& src_path,
                              const base::FilePath& dest_path) {
  // Create destination directory
  if (!util::CopyEmptyTreeToDirectory(src_path, dest_path))
    return false;

  if (manifest_.entry().size()) {
    // We were given a manifest, let's follow its recipe for each file.
    for (const PatchManifestEntry& entry : manifest_.entry()) {
      if (entry.has_patch_file_name()) {
        if (!util::DoBsDiff(src_path.Append(entry.base_file_name()),
                            src_path.Append(entry.original_file_name()),
                            dest_path.Append(entry.patch_file_name()))) {
          LOG(ERROR) << "Failed to call bsdiff";
          return false;
        }
      } else {
        if (!base::CopyFile(src_path.Append(entry.original_file_name()),
                            dest_path.Append(entry.original_file_name()))) {
          return false;
        }
      }
    }
    // Verify that the resulting contents are identical in size to what the
    // manifest anticipated.
    CHECK(ComputeDirectorySize(src_path) == manifest_.directory_size_full());
    CHECK(ComputeDirectorySize(dest_path) ==
          manifest_.directory_size_patched());
  } else {
    // We were not given a manifest, let's find a recipe for each file.

    // Gather files in clusters by size
    std::vector<std::vector<base::FilePath>> file_clusters =
        ClusterFilesInDirectoryBySize(src_path);

    // Each cluster is processed individually for performance reasons, as files
    // with very different sizes shouldn't be patched together anyways.
    for (const auto& cluster : file_clusters) {
      std::vector<PatchManifestEntry> entries =
          EncodeFileClusterFromSrcToDest(cluster, src_path, dest_path);
      for (const auto& m_e : entries) {
        *manifest_.add_entry() = m_e;
      }
    }

    // Populate and commit manifest to file
    manifest_.set_directory_size_full(ComputeDirectorySize(src_path));
    manifest_.set_directory_size_patched(ComputeDirectorySize(dest_path));
  }

  return CommitManifestToFile();
}

bool ManagedDirectory::CommitManifestToFile() {
  base::FilePath manifest_path = directory_root_.Append(kPatchManifestFilename);

  base::File new_manifest(
      manifest_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!brillo::WriteTextProtobuf(new_manifest.GetPlatformFile(), manifest_)) {
    LOG(ERROR) << "Couldn't write new journal to temp file";
    return false;
  }
  LOG(INFO) << "Wrote manifest to " << manifest_path;
  return true;
}

// Create Managed Directory for the decode path. The input directory path may or
// may not be the root of the managed directory, as the caller may be preparing
// to decode an individual file or sub-directory.
bool ManagedDirectory::CreateFromExisting(const base::FilePath& managed_path) {
  if (!GetManagedDirectoryRoot(managed_path, &directory_root_)) {
    LOG(ERROR) << "Directory " << managed_path << " appears to be unmanaged";
    return false;
  }

  base::FilePath manifest_path = directory_root_.Append(kPatchManifestFilename);
  if (!brillo::ReadTextProtobuf(manifest_path, &manifest_)) {
    LOG(ERROR) << "Failed to read manifest file, exiting";
    return false;
  }

  return true;
}

bool ManagedDirectory::ManifestEntryIsUnderTargetPath(
    const base::FilePath& target_path, const PatchManifestEntry& entry) {
  // The entry's filename is a relative path within the managed directory.
  // Here we convert the target path to be relative for comparison.
  base::FilePath relative_target_path = util::AppendRelativePathOn(
      directory_root_, target_path, base::FilePath());

  // The entry's path will match the relative target_path exactly if the
  // target is a single file, and will have the relative target path as a
  // substring if the entry is below the target's directory
  if (entry.original_file_name().find(relative_target_path.value()) !=
      std::string::npos) {
    return true;
  }

  return false;
}

bool ManagedDirectory::Decode(const base::FilePath& target_path,
                              const base::FilePath& dest_path) {
  LOG(INFO) << "Decoding " << target_path << " to " << dest_path;

  if (!util::CopyEmptyTreeToDirectory(directory_root_, dest_path)) {
    LOG(ERROR) << "Failed to create empty tree";
    return false;
  }

  base::FilePath dest_file;
  for (const PatchManifestEntry& entry : manifest_.entry()) {
    // Filter out any files outside the requested sub-tree
    if (!ManifestEntryIsUnderTargetPath(target_path, entry)) {
      continue;
    }

    dest_file = dest_path.Append(entry.original_file_name());
    if (entry.has_patch_file_name()) {
      if (!util::DoBsPatch(directory_root_.Append(entry.base_file_name()),
                           dest_file,
                           directory_root_.Append(entry.patch_file_name()))) {
        LOG(ERROR) << "bspatch returned failure";
        return false;
      }
    } else {
      if (!CopyFile(directory_root_.Append(entry.original_file_name()),
                    dest_file)) {
        LOG(ERROR) << "CopyFile returned false";
        return false;
      }
    }
    if (util::GetMD5SumForFile(dest_file) !=
        entry.original_file_md5_checksum()) {
      LOG(ERROR) << "MD5 checksum didn't match after reconstruction";
      return false;
    }
  }
  return true;
}

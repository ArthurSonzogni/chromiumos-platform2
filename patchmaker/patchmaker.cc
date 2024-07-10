// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/hash/md5.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/proto_file_io.h>

#include "patchmaker/directory_util.h"
#include "patchmaker/file_util.h"
#include "patchmaker/managed_directory.h"

namespace {

bool EncodeDirectory(const base::FilePath& src_path,
                     const base::FilePath& dest_path,
                     const std::string& input_manifest_str,
                     const std::string& immutable_path_str) {
  ManagedDirectory managed_dir;

  if (!managed_dir.CreateNew(dest_path,
                             input_manifest_str.empty()
                                 ? std::optional<base::FilePath>{}
                                 : base::FilePath(input_manifest_str))) {
    LOG(ERROR) << "Failed to initialize ManagedDirectory";
    return false;
  }

  std::vector<base::FilePath> immutable_paths;
  util::ParseDelimitedFilePaths(immutable_path_str, &immutable_paths);
  for (const auto& path : immutable_paths) {
    if (!base::PathExists(path)) {
      LOG(ERROR) << "Path requesting immutability doesn't exist: " << path;
      return false;
    }
  }

  if (!managed_dir.Encode(src_path, dest_path, immutable_paths)) {
    LOG(ERROR) << "Failed to encode source path " << src_path;
    return false;
  }

  return true;
}

// Target path could be a single file or sub-directory within a managed
// directory
bool DecodeDirectory(const base::FilePath& target_path,
                     const base::FilePath& dest_path) {
  ManagedDirectory managed_dir;

  if (!managed_dir.CreateFromExisting(target_path)) {
    LOG(ERROR) << "Failed to initialize ManagedDirectory";
    return false;
  }
  if (!managed_dir.Decode(target_path, dest_path)) {
    LOG(ERROR) << "Failed to decode target path " << target_path;
    return false;
  }

  return true;
}

bool EndToEndTest(const base::FilePath& src_path) {
  // Take a directory as input, encode it, and then reconstruct it,
  // ensuring that the reconstructed contents are identical to the
  // originals.

  // Step 1 - Create temp directories for encode and decode
  base::ScopedTempDir tmp_encode, tmp_decode;
  if (!tmp_encode.CreateUniqueTempDir() || !tmp_decode.CreateUniqueTempDir()) {
    LOG(ERROR) << "Failed to create temp directories for testing";
    return false;
  }

  // Step 2 - Call EncodeDirectory from src_path to tmp_encode
  LOG(INFO) << "Encoding into " << tmp_encode.GetPath();
  if (!EncodeDirectory(src_path, tmp_encode.GetPath(), "", "")) {
    LOG(ERROR) << "Encode step failed";
    return false;
  }

  // Step 3 - Call DecodeDirectory from tmp_encode to tmp_decode
  LOG(INFO) << "Decoding into " << tmp_decode.GetPath();
  if (!DecodeDirectory(tmp_encode.GetPath(), tmp_decode.GetPath())) {
    LOG(ERROR) << "Decode step failed";
    return false;
  }

  // Step 4 - Ensure src_path and tmp_decode paths have identical contents
  if (!util::DirectoriesAreEqual(src_path, tmp_decode.GetPath())) {
    LOG(ERROR) << "Failed to validate equality after decode";
    return false;
  }

  LOG(INFO) << "Src size " << ComputeDirectorySize(src_path)
            << ", Encoded size " << ComputeDirectorySize(tmp_encode.GetPath());

  LOG(INFO) << "All validation checks passed :)";
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(encode, false, "Generate patches to replace original files");
  DEFINE_bool(decode, false, "Reconstruct original files from patches");
  DEFINE_bool(end_to_end, false, "Encode, decode, and validate contents");

  DEFINE_string(src_path, "", "Source path for operation");
  DEFINE_string(dest_path, "", "Destination path for encode operation");

  DEFINE_string(input_manifest, "", "Optional: Input manifest for operation");
  DEFINE_string(immutable_paths, "",
                "Optional: Colon (':') separated list of immutable files or "
                "directories that must be left intact.");

  brillo::FlagHelper::Init(argc, argv, "Patch utility for binary storage.");

  int num_operations = FLAGS_encode + FLAGS_decode + FLAGS_end_to_end;
  if (num_operations != 1) {
    LOG(ERROR) << "Expected one of --encode / --decode / --end_to_end";
    return EXIT_FAILURE;
  }

  if (FLAGS_src_path.empty()) {
    LOG(ERROR) << "--src_path is required";
    return EXIT_FAILURE;
  }

  if (FLAGS_end_to_end)
    return EndToEndTest(base::FilePath(FLAGS_src_path)) ? EXIT_SUCCESS
                                                        : EXIT_FAILURE;

  if (FLAGS_dest_path.empty()) {
    LOG(ERROR) << "--dest_path is required";
    return EXIT_FAILURE;
  }

  if (FLAGS_encode) {
    return EncodeDirectory(base::FilePath(FLAGS_src_path),
                           base::FilePath(FLAGS_dest_path),
                           FLAGS_input_manifest, FLAGS_immutable_paths)
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
  }

  if (FLAGS_decode) {
    return DecodeDirectory(base::FilePath(FLAGS_src_path),
                           base::FilePath(FLAGS_dest_path))
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
  }

  NOTREACHED_NORETURN();
}

// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/enrollment_storage.h"

#include <string>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/strings/strcat.h>
#include <base/strings/string_piece.h>

namespace faced {
namespace {

// Name of daemon.
constexpr char kFaced[] = "faced";
// Name of enrollment file to read and write from.
constexpr char kEnrollmentFileName[] = "enrollment";

}  // namespace

absl::Status EnrollmentStorage::WriteEnrollment(base::StringPiece user_id,
                                                base::StringPiece data) {
  base::FilePath save_path = GetEnrollmentFilePath(user_id);

  base::File::Error error;
  if (!CreateDirectoryAndGetError(save_path.DirName(), &error)) {
    return absl::UnavailableError(
        base::StrCat({"Unable to create directory for user: ",
                      base::File::ErrorToString(error)}));
  }

  if (!base::ImportantFileWriter::WriteFileAtomically(save_path, data)) {
    return absl::UnavailableError(
        "Unable to save enrollment to file for user.");
  }

  return absl::OkStatus();
}

absl::StatusOr<std::string> EnrollmentStorage::ReadEnrollment(
    base::StringPiece user_id) {
  base::FilePath enrollment_path = GetEnrollmentFilePath(user_id);

  std::string data;
  if (!base::ReadFileToString(enrollment_path, &data)) {
    return absl::UnavailableError("Unable to read enrollment for user.");
  }

  return data;
}

base::FilePath EnrollmentStorage::GetEnrollmentFilePath(
    base::StringPiece user_id) {
  return root_path_.Append(kFaced).Append(user_id).Append(kEnrollmentFileName);
}

}  // namespace faced

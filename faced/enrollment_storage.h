// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_ENROLLMENT_STORAGE_H_
#define FACED_ENROLLMENT_STORAGE_H_

#include <string>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/files/file_path.h>
#include <base/strings/string_piece.h>

namespace faced {

inline constexpr char kDaemonStorePath[] = "/run/daemon-store";

// EnrollmentStorage reads and writes enrollments per user to disk to a daemon
// store folder that is shared with the user's cryptohome.
class EnrollmentStorage {
 public:
  // Constructor sets the file path to be /run/daemon-store/faced/<user_id>,
  // which is bound to /home/root/<user_id>/faced.
  explicit EnrollmentStorage(
      const base::FilePath& root_path = base::FilePath(kDaemonStorePath))
      : root_path_(root_path) {}

  // Writes an enrollment for a specified user.
  absl::Status WriteEnrollment(base::StringPiece user_id,
                               base::StringPiece data);

  // Reads an enrollment for a specified user.
  absl::StatusOr<std::string> ReadEnrollment(base::StringPiece user_id);

 private:
  // Returns the filepath to load and save an enrollment given a user_id.
  base::FilePath GetEnrollmentFilePath(base::StringPiece user_id);

  base::FilePath root_path_;
};

}  // namespace faced

#endif  // FACED_ENROLLMENT_STORAGE_H_

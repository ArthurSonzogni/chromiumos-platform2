// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_ARCHIVE_MOUNTER_H_
#define CROS_DISKS_ARCHIVE_MOUNTER_H_

#include <memory>
#include <string>
#include <vector>

#include "cros-disks/fuse_mounter.h"

namespace cros_disks {

class Metrics;

// An implementation of FUSEMounter tailored for mounting archives.
class ArchiveMounter : public FUSEMounter {
 public:
  static constexpr char kChromeNamespace[] = "/run/namespaces/mnt_chrome";

  ArchiveMounter(const Platform* platform,
                 brillo::ProcessReaper* process_reaper,
                 std::string archive_type,
                 Metrics* metrics,
                 std::string metrics_name,
                 std::vector<int> password_needed_exit_codes,
                 std::unique_ptr<SandboxedProcessFactory> sandbox_factory,
                 std::vector<std::string> extra_command_line_options = {});
  ArchiveMounter(const ArchiveMounter&) = delete;
  ArchiveMounter& operator=(const ArchiveMounter&) = delete;

  ~ArchiveMounter() override;

  bool CanMount(const std::string& source,
                const std::vector<std::string>& params,
                base::FilePath* suggested_dir_name) const override;

  OwnerUser GetDaemonUser() const;

 protected:
  // FUSEMounter overrides:
  MountErrorType InterpretReturnCode(int return_code) const override;

  std::unique_ptr<SandboxedProcess> PrepareSandbox(
      const std::string& source,
      const base::FilePath& target_path,
      std::vector<std::string> params,
      MountErrorType* error) const final;

  virtual std::vector<std::string> GetBindPaths(
      base::StringPiece original_path) const {
    return {std::string(original_path)};
  }

 private:
  const std::string archive_type_;
  const std::string extension_;
  Metrics* const metrics_;
  const std::string metrics_name_;
  const std::vector<int> password_needed_exit_codes_;
  const std::unique_ptr<SandboxedProcessFactory> sandbox_factory_;
  const std::vector<std::string> extra_command_line_options_;

  // Archivemount can read "foo.bz2" and "bar.qux.gz" files that are compressed
  // but aren't archives (multiple source files rolled into one). It calls
  // these formats "raw" and treats them as a single-element archive.
  //
  // Note that while "bar.qux.gz" is raw, "bar.tar.gz" is not (it is a
  // compressed archive). However, the archive_type argument passed to the
  // constructor is just "gz", since we cannot practically enumerate all
  // two-part extensions ("a.gz", "b.gz", ..., "qux.gz", ..., "tar.gz", ...),
  //
  // This format_raw_ field being true, based only on the archive_type
  // constructor argument and not the archive's actual path name.
  //
  // "archivemount" in this comment means a specific program
  // (https://github.com/cybernoid/archivemount). This C++ class is also called
  // "ArchiveMounter", but that name uses the "archive mounter" words in their
  // general technical sense.
  //
  // Historically, we executed the archivemount program, not the fuse-archive
  // program. More recently, we use fuse-archive which is a drop-in
  // replacement, featurewise, but is faster.
  const bool format_raw_;

  friend class ArchiveMounterTest;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_ARCHIVE_MOUNTER_H_

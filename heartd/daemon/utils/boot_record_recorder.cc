// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/utils/boot_record_recorder.h"

#include <string>

#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "heartd/daemon/boot_record.h"
#include "heartd/daemon/database.h"

namespace heartd {

namespace {

void CollectShutdownTime(const base::FilePath& root_dir,
                         const Database* db_ptr) {
  base::FileEnumerator file_enum(
      root_dir.Append(kMetricsPath), /*recursive=*/false,
      base::FileEnumerator::FileType::FILES |
          base::FileEnumerator::FileType::DIRECTORIES);
  // According to b/293410814, there should be only one bootstat archive.
  auto path = file_enum.Next();
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    LOG(ERROR) << "Failed to open shutdown metrics file: " << path;
    return;
  }

  base::File::Info info;
  if (!file.GetInfo(&info)) {
    LOG(ERROR) << "Failed to obtain the info of file: " << path;
    return;
  }

  db_ptr->InsertBootRecord(
      BootRecord(path.BaseName().value(), info.creation_time));
}

void CollectBootID(const base::FilePath& root_dir, const Database* db_ptr) {
  auto boot_id_path = root_dir.Append(kBootIDPath);
  base::File file(boot_id_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    LOG(ERROR) << "Failed to open boot id file: " << boot_id_path;
    return;
  }

  base::File::Info info;
  if (!file.GetInfo(&info)) {
    LOG(ERROR) << "Failed to obtain the info of /var/log/boot_id.log";
    return;
  }

  std::string content;
  base::ReadFileToString(boot_id_path, &content);
  auto lines = base::SplitString(content, "\n", base::TRIM_WHITESPACE,
                                 base::SPLIT_WANT_NONEMPTY);
  if (lines.empty()) {
    LOG(ERROR) << "Failed to get boot_id records";
    return;
  }

  // Example:
  // 2024-01-01T00:00:00.00000Z INFO boot_id: 6d415d5587ed4024be70a645f2b019c3
  auto tokens = base::SplitString(lines.back(), " ", base::TRIM_WHITESPACE,
                                  base::SPLIT_WANT_NONEMPTY);
  if (tokens.size() != 4 || tokens[2] != "boot_id:") {
    LOG(ERROR) << "Failed to parse boot_id records: " << lines.back();
    return;
  }

  db_ptr->InsertBootRecord(BootRecord(tokens.back(), info.last_modified));
}

}  // namespace

void RecordBootMetrics(const base::FilePath& root_dir, const Database* db_ptr) {
  CollectShutdownTime(root_dir, db_ptr);
  CollectBootID(root_dir, db_ptr);
}

}  // namespace heartd

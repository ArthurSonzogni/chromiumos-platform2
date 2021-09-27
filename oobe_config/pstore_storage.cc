// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/pstore_storage.h"

#include <sstream>
#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>

#include "oobe_config/rollback_constants.h"

namespace oobe_config {
namespace {

const char kRollbackDataKey[] = "rollback_data";
const char kRamoopsFilePattern[] = "pmsg-ramoops-*";

base::FilePath PrefixAbsolutePath(const base::FilePath& prefix,
                                  const base::FilePath& file_path) {
  if (prefix.empty())
    return file_path;
  DCHECK(!file_path.empty());
  DCHECK_EQ('/', file_path.value()[0]);
  return prefix.Append(file_path.value().substr(1));
}

base::FileEnumerator EnumerateRamoops(const base::FilePath& root_path) {
  return base::FileEnumerator(
      PrefixAbsolutePath(root_path, base::FilePath(kPstorePath)),
      /*recursive=*/false, base::FileEnumerator::FILES, kRamoopsFilePattern);
}

bool ExtractRollbackData(const base::FilePath& file,
                         std::string* rollback_data) {
  std::string file_content;
  base::ReadFileToString(file, &file_content);
  std::stringstream file_stream(file_content);
  std::string key;
  while (file_stream && key != kRollbackDataKey) {
    file_stream >> key;
  }
  if (file_stream && key == kRollbackDataKey) {
    std::string hex_rollback_data;
    file_stream >> hex_rollback_data;
    *rollback_data = hex_rollback_data;
    return true;  // Data may be completely empty - that is valid as well.
  }
  return false;
}

base::Optional<std::string> HexToBinary(const std::string& hex) {
  std::string binary;
  bool success = base::HexStringToString(hex, &binary);

  if (!success) {
    LOG(ERROR) << "Could not decode rollback data.";
    return base::nullopt;
  }
  return binary;
}

}  // namespace

bool StageForPstore(const std::string& data, const base::FilePath& root_path) {
  std::string hex_data_with_header = base::StrCat(
      {kRollbackDataKey, " ", base::HexEncode(data.data(), data.size())});

  int bytes_written = base::WriteFile(
      PrefixAbsolutePath(root_path, base::FilePath(kRollbackDataForPmsgFile)),
      hex_data_with_header.c_str(), hex_data_with_header.size());
  if (bytes_written != hex_data_with_header.size()) {
    LOG(ERROR) << "Could not write " << kRollbackDataForPmsgFile;
    return false;
  }
  return true;
}

base::Optional<std::string> LoadFromPstore(const base::FilePath& root_path) {
  base::FileEnumerator pmsg_ramoops_enumerator = EnumerateRamoops(root_path);
  for (base::FilePath ramoops_file = pmsg_ramoops_enumerator.Next();
       !ramoops_file.empty(); ramoops_file = pmsg_ramoops_enumerator.Next()) {
    LOG(INFO) << "Looking at file " << ramoops_file.value();
    std::string rollback_data;
    if (ExtractRollbackData(ramoops_file, &rollback_data)) {
      return HexToBinary(rollback_data);
    }
    LOG(INFO) << "No rollback data found in that file.";
  }
  LOG(ERROR) << "No rollback data found.";
  return base::nullopt;
}

}  // namespace oobe_config

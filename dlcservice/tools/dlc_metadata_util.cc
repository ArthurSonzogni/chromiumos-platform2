// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/values.h>
#include <brillo/flag_helper.h>
#include <chromeos/constants/imageloader.h>
#include <dlcservice/metadata/metadata.h>
#include <libimageloader/manifest.h>

using dlcservice::metadata::Metadata;

namespace {
bool CheckExclusiveFlags(const std::vector<bool>& flags) {
  return std::count(std::begin(flags), std::end(flags), true) == 1;
}
}  // namespace

class DlcMetadataUtil {
 public:
  DlcMetadataUtil(int argc, const char** argv) : argc_(argc), argv_(argv) {}
  ~DlcMetadataUtil() = default;

  DlcMetadataUtil(const DlcMetadataUtil&) = delete;
  DlcMetadataUtil& operator=(const DlcMetadataUtil&) = delete;

  int Run();

 private:
  enum Action {
    kGetAction,
    kSetAction,
  };

  bool ParseFlags();
  int SetMetadata();
  int GetMetadata();
  std::optional<Metadata::Entry> ReadMetadataEntry();

  int argc_;
  const char** argv_;

  Action action_;
  std::string id_;
  base::FilePath file_path_;

  Metadata metadata_{base::FilePath(imageloader::kDlcManifestRootpath)};
};

int DlcMetadataUtil::Run() {
  if (!ParseFlags())
    return EX_USAGE;

  if (!metadata_.Initialize()) {
    LOG(ERROR) << "Failed to initialize metadata.";
    return EX_SOFTWARE;
  }

  switch (action_) {
    case kGetAction:
      return GetMetadata();
    case kSetAction:
      return SetMetadata();
  }
}

bool DlcMetadataUtil::ParseFlags() {
  DEFINE_bool(get, false, "Get the metadata and print to stdout as JSON");
  DEFINE_bool(set, false, "Set the metadata from input JSON");
  DEFINE_string(id, "", "The ID of the DLC");
  DEFINE_string(file, "", "Use the file instead of stdin/stdout");

  brillo::FlagHelper::Init(argc_, argv_, "dlc_metadata_util");

  if (!CheckExclusiveFlags({FLAGS_get, FLAGS_set})) {
    LOG(ERROR) << "One of the 'get' or 'set' option should be specified.";
    return false;
  }
  if (FLAGS_get) {
    action_ = kGetAction;
  } else {
    action_ = kSetAction;
  }

  id_ = FLAGS_id;
  if (id_.empty()) {
    LOG(ERROR) << "DLC ID cannot be empty.";
    return false;
  }

  file_path_ = base::FilePath(FLAGS_file);
  return true;
}

int DlcMetadataUtil::GetMetadata() {
  auto entry = metadata_.Get(id_);
  if (!entry) {
    LOG(ERROR) << "Unable to get metadata for " << id_;
    return EX_SOFTWARE;
  }

  auto dict = base::Value::Dict()
                  .Set("manifest", std::move(entry->manifest))
                  .Set("table", entry->table);
  auto json =
      base::WriteJsonWithOptions(dict, base::JSONWriter::OPTIONS_PRETTY_PRINT);
  if (!json)
    return EX_SOFTWARE;

  if (file_path_.empty()) {
    std::cout << *json;
    return EX_OK;
  } else {
    return base::WriteFile(file_path_, *json) ? EX_OK : EX_IOERR;
  }
}

int DlcMetadataUtil::SetMetadata() {
  auto entry = ReadMetadataEntry();
  if (!entry) {
    LOG(ERROR) << "Failed to read or parse metadata entry.";
    return EX_DATAERR;
  }

  return metadata_.Set(id_, *entry) ? EX_OK : EX_SOFTWARE;
}

std::optional<Metadata::Entry> DlcMetadataUtil::ReadMetadataEntry() {
  std::string metadata_str;
  if (file_path_.empty()) {
    // Read metadata entry from stdin.
    std::string buffer;
    while (std::getline(std::cin, buffer)) {
      metadata_str.append(buffer);
    }
  } else {
    if (!base::ReadFileToString(file_path_, &metadata_str)) {
      LOG(ERROR) << "Unable to read the metadata entry from the input file.";
      return std::nullopt;
    }
  }

  // Parse and construct metadata entry.
  auto metadata_val = base::JSONReader::ReadAndReturnValueWithError(
      metadata_str, base::JSON_PARSE_RFC);

  if (!metadata_val.has_value() || !metadata_val->is_dict()) {
    LOG(ERROR) << "Could not parse input metadata entry as JSON. Error: "
               << metadata_val.error().message;
    return std::nullopt;
  }

  Metadata::Entry entry;
  auto* manifest_val = metadata_val->GetDict().FindDict("manifest");
  if (!manifest_val) {
    LOG(ERROR) << "Could not get manifest from the input";
    return std::nullopt;
  }
  if (!imageloader::Manifest().ParseManifest(*manifest_val)) {
    LOG(ERROR) << "Could not parse manifest from the input";
    return std::nullopt;
  }
  entry.manifest = std::move(*manifest_val);

  auto* table_val = metadata_val->GetDict().FindString("table");
  if (!table_val) {
    LOG(ERROR) << "Could not get verity table from the input";
    return std::nullopt;
  }
  entry.table = std::move(*table_val);

  return entry;
}

int main(int argc, const char** argv) {
  return DlcMetadataUtil(argc, argv).Run();
}

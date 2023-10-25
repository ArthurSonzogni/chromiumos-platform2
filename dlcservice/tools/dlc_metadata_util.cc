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
int CountExclusiveFlags(const std::vector<bool>& flags) {
  return std::count(std::begin(flags), std::end(flags), true);
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
    kListAction,
  };

  bool ParseFlags();
  int SetMetadata();
  int GetMetadata();
  int ListDlcIds();
  std::optional<Metadata::Entry> ReadMetadataEntry();

  int argc_;
  const char** argv_;

  Action action_;
  std::string id_;
  base::FilePath file_path_;
  Metadata::FilterKey filter_key_;
  base::FilePath metadata_dir_;

  std::unique_ptr<Metadata> metadata_;
};

int DlcMetadataUtil::Run() {
  if (!ParseFlags())
    return EX_USAGE;

  metadata_ = std::make_unique<Metadata>(metadata_dir_);
  if (!metadata_->Initialize()) {
    LOG(ERROR) << "Failed to initialize metadata.";
    return EX_SOFTWARE;
  }

  switch (action_) {
    case kGetAction:
      return GetMetadata();
    case kSetAction:
      return SetMetadata();
    case kListAction:
      return ListDlcIds();
  }
}

bool DlcMetadataUtil::ParseFlags() {
  DEFINE_bool(get, false, "Get the metadata and print to stdout as JSON");
  DEFINE_bool(set, false, "Set the metadata from input JSON");
  DEFINE_bool(list, false, "List all DLC IDs, or a subset if filters is given");
  DEFINE_string(id, "", "The ID of the DLC");
  DEFINE_string(file, "", "Use the file instead of stdin/stdout");
  DEFINE_string(metadata_dir, "",
                "The DLC metadata directory path. "
                "Manifest root path is used if not specified");
  DEFINE_bool(factory_install, false, "Filter factory installed DLCs");
  DEFINE_bool(powerwash_safe, false, "Filter powerwash safe DLCs");
  DEFINE_bool(preload_allowed, false, "Filter preload allowed DLCs");

  brillo::FlagHelper::Init(argc_, argv_, "dlc_metadata_util");

  if (CountExclusiveFlags({FLAGS_get, FLAGS_set, FLAGS_list}) != 1) {
    LOG(ERROR)
        << "One of the 'get', 'set' or 'list' option should be specified.";
    return false;
  }

  if (FLAGS_get) {
    action_ = kGetAction;
  } else if (FLAGS_set) {
    action_ = kSetAction;
  } else if (FLAGS_list) {
    action_ = kListAction;
  } else {
    LOG(ERROR) << "Invalid option.";
    return false;
  }

  id_ = FLAGS_id;
  if ((FLAGS_get || FLAGS_set) && id_.empty()) {
    LOG(ERROR) << "DLC ID cannot be empty.";
    return false;
  }

  if (CountExclusiveFlags({FLAGS_factory_install, FLAGS_powerwash_safe,
                           FLAGS_preload_allowed}) > 1) {
    LOG(ERROR) << "At most one filter is supported.";
    return false;
  }
  if (FLAGS_factory_install) {
    filter_key_ = Metadata::FilterKey::kFactoryInstall;
  } else if (FLAGS_powerwash_safe) {
    filter_key_ = Metadata::FilterKey::kPowerwashSafe;
  } else if (FLAGS_preload_allowed) {
    filter_key_ = Metadata::FilterKey::kPreloadAllowed;
  } else {
    filter_key_ = Metadata::FilterKey::kNone;
  }

  file_path_ = base::FilePath(FLAGS_file);
  if (FLAGS_metadata_dir.empty()) {
    metadata_dir_ = base::FilePath(imageloader::kDlcManifestRootpath);
  } else {
    metadata_dir_ = base::FilePath(FLAGS_metadata_dir);
  }
  if (!base::PathExists(metadata_dir_)) {
    LOG(ERROR) << "The metadata direcotry " << metadata_dir_
               << " does not exists.";
    return false;
  }
  return true;
}

int DlcMetadataUtil::GetMetadata() {
  auto entry = metadata_->Get(id_);
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

  return metadata_->Set(id_, *entry) ? EX_OK : EX_SOFTWARE;
}

int DlcMetadataUtil::ListDlcIds() {
  const auto& ids = metadata_->ListDlcIds(filter_key_, base::Value(true));
  auto id_list = base::Value::List::with_capacity(ids.size());
  for (const auto& id : ids)
    id_list.Append(id);

  std::string json;
  base::JSONWriter::Write(id_list, &json);
  std::cout << json;
  return EX_OK;
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

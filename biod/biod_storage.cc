// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_storage.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <sstream>
#include <utility>

#include <base/base64.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/guid.h>
#include <base/json/json_reader.h>
#include <base/json/json_string_value_serializer.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/values.h>

namespace biod {

using base::FilePath;

namespace {
constexpr char kDaemonStorePath[] = "/run/daemon-store";
constexpr char kRecordFileName[] = "Record";
constexpr char kBiod[] = "biod";

// Members of the JSON file.
constexpr char kBioManagerMember[] = "biomanager";
constexpr char kData[] = "data";
constexpr char kLabel[] = "label";
constexpr char kRecordId[] = "record_id";
constexpr char kValidationVal[] = "match_validation_value";
constexpr char kVersionMember[] = "version";
}  // namespace

BiodStorage::BiodStorage(const std::string& biometrics_manager_name)
    : root_path_(kDaemonStorePath),
      biometrics_manager_name_(biometrics_manager_name),
      allow_access_(false) {}

void BiodStorage::SetRootPathForTesting(const base::FilePath& root_path) {
  root_path_ = root_path;
}

bool BiodStorage::WriteRecord(const BiometricsManager::Record& record,
                              base::Value data) {
  if (!allow_access_) {
    LOG(ERROR) << "Access to the storage mounts not allowed.";
    return false;
  }

  if (!record.IsValidUTF8()) {
    LOG(ERROR) << "Record contains invalid UTF8.";
    return false;
  }

  const std::string& record_id(record.GetId());
  base::Value record_value(base::Value::Type::DICTIONARY);
  record_value.SetStringKey(kLabel, record.GetLabel());
  record_value.SetStringKey(kRecordId, record_id);

  if (record.SupportsPositiveMatchSecret()) {
    record_value.SetStringKey(kValidationVal, record.GetValidationValBase64());
    record_value.SetIntKey(kVersionMember, kRecordFormatVersion);
  } else {
    record_value.SetIntKey(kVersionMember,
                           kRecordFormatVersionNoValidationValue);
  }

  record_value.SetKey(kData, std::move(data));
  record_value.SetStringKey(kBioManagerMember, biometrics_manager_name_);

  std::string json_string;
  JSONStringValueSerializer json_serializer(&json_string);
  if (!json_serializer.Serialize(record_value)) {
    LOG(ERROR) << "Failed to serialize record with id " << record_id
               << " to JSON.";
    return false;
  }

  FilePath record_storage_filename = GetRecordFilename(record);
  if (record_storage_filename.empty()) {
    LOG(ERROR) << "Unable to get filename for record.";
    return false;
  }

  {
    brillo::ScopedUmask owner_only_umask(~(0700));

    if (!base::CreateDirectory(record_storage_filename.DirName())) {
      PLOG(ERROR) << "Cannot create directory: "
                  << record_storage_filename.DirName().value() << ".";
      return false;
    }
  }

  {
    brillo::ScopedUmask owner_only_umask(~(0600));

    if (!base::ImportantFileWriter::WriteFileAtomically(record_storage_filename,
                                                        json_string)) {
      LOG(ERROR) << "Failed to write JSON file: "
                 << record_storage_filename.value() << ".";
      return false;
    }
  }

  LOG(INFO) << "Done writing record with id " << record_id
            << " to file successfully. ";
  return true;
}

std::unique_ptr<std::vector<uint8_t>>
BiodStorage::ReadValidationValueFromRecord(int record_format_version,
                                           const base::Value& record_dictionary,
                                           const FilePath& record_path) {
  std::string validation_val_str;
  if (record_format_version == kRecordFormatVersion) {
    const std::string* validation_val_str_ptr =
        record_dictionary.FindStringKey(kValidationVal);
    if (!validation_val_str_ptr) {
      LOG(ERROR) << "Cannot read validation value from " << record_path.value()
                 << ".";
      return nullptr;
    }
    validation_val_str = *validation_val_str_ptr;
    if (!base::Base64Decode(validation_val_str, &validation_val_str)) {
      LOG(ERROR) << "Unable to base64 decode validation value from "
                 << record_path.value() << ".";
      return nullptr;
    }
  } else if (record_format_version == kRecordFormatVersionNoValidationValue) {
    // If the record has format version 1, it should have no validation value
    // field. In that case, load an empty validation value.
    LOG(INFO) << "Record from " << record_path.value() << " does not have "
              << "validation value and needs migration.";
  } else {
    LOG(ERROR) << "Invalid format version from record " << record_path.value()
               << ".";
    return nullptr;
  }

  return std::make_unique<std::vector<uint8_t>>(validation_val_str.begin(),
                                                validation_val_str.end());
}

BiodStorage::ReadRecordResult BiodStorage::ReadRecords(
    const std::unordered_set<std::string>& user_ids) {
  ReadRecordResult ret;
  for (const auto& user_id : user_ids) {
    auto result = ReadRecordsForSingleUser(user_id);
    for (const auto& valid : result.valid_records) {
      ret.valid_records.emplace_back(valid);
    }
    for (const auto& invalid : result.invalid_records) {
      ret.invalid_records.emplace_back(invalid);
    }
  }
  return ret;
}

BiodStorage::ReadRecordResult BiodStorage::ReadRecordsForSingleUser(
    const std::string& user_id) {
  BiodStorage::ReadRecordResult ret;

  if (!allow_access_) {
    LOG(ERROR) << "Access to the storage mounts not yet allowed.";
    return ret;
  }

  FilePath biod_path =
      root_path_.Append(kBiod).Append(user_id).Append(biometrics_manager_name_);
  base::FileEnumerator enum_records(biod_path, false,
                                    base::FileEnumerator::FILES, "Record*");
  for (FilePath record_path = enum_records.Next(); !record_path.empty();
       record_path = enum_records.Next()) {
    std::string json_string;
    BiodStorage::Record cur_record;

    cur_record.metadata.user_id = user_id;

    if (!base::ReadFileToString(record_path, &json_string)) {
      LOG(ERROR) << "Failed to read the string from " << record_path.value()
                 << ".";
      ret.invalid_records.emplace_back(cur_record);
      continue;
    }

    auto record_value = base::JSONReader::ReadAndReturnValueWithError(
        json_string, base::JSON_ALLOW_TRAILING_COMMAS);

    if (!record_value.value) {
      LOG_IF(ERROR, !record_value.error_message.empty())
          << "JSON error message: " << record_value.error_message << ".";
      ret.invalid_records.emplace_back(cur_record);
      continue;
    }

    if (!record_value.value->is_dict()) {
      LOG(ERROR) << "Value " << record_path.value() << " is not a dictionary.";
      ret.invalid_records.emplace_back(cur_record);
      continue;
    }
    base::Value record_dictionary = std::move(*record_value.value);

    const std::string* label = record_dictionary.FindStringKey(kLabel);

    if (!label) {
      LOG(ERROR) << "Cannot read label from " << record_path.value() << ".";
      ret.invalid_records.emplace_back(cur_record);
      continue;
    }
    cur_record.metadata.label = *label;

    const std::string* record_id = record_dictionary.FindStringKey(kRecordId);

    if (!record_id) {
      LOG(ERROR) << "Cannot read record id from " << record_path.value() << ".";
      ret.invalid_records.emplace_back(cur_record);
      continue;
    }
    cur_record.metadata.record_id = *record_id;

    base::Optional<int> record_format_version =
        record_dictionary.FindIntKey(kVersionMember);
    if (!record_format_version.has_value()) {
      LOG(ERROR) << "Cannot read record format version from "
                 << record_path.value() << ".";
      ret.invalid_records.emplace_back(cur_record);
      continue;
    }
    cur_record.metadata.record_format_version = *record_format_version;

    std::unique_ptr<std::vector<uint8_t>> validation_val =
        ReadValidationValueFromRecord(*record_format_version, record_dictionary,
                                      record_path);
    if (!validation_val) {
      ret.invalid_records.emplace_back(cur_record);
      continue;
    }
    cur_record.metadata.validation_val = *validation_val;

    const base::Value* data = record_dictionary.FindKey(kData);

    if (!data) {
      LOG(ERROR) << "Cannot read data from " << record_path.value() << ".";
      ret.invalid_records.emplace_back(cur_record);
      continue;
    }
    cur_record.data = data->GetString();

    ret.valid_records.emplace_back(cur_record);
  }
  return ret;
}

bool BiodStorage::DeleteRecord(const std::string& user_id,
                               const std::string& record_id) {
  if (!allow_access_) {
    LOG(ERROR) << "Access to the storage mounts not yet allowed.";
    return false;
  }

  FilePath record_storage_filename = root_path_.Append(kBiod)
                                         .Append(user_id)
                                         .Append(biometrics_manager_name_)
                                         .Append(kRecordFileName + record_id);

  if (!base::PathExists(record_storage_filename)) {
    LOG(INFO) << "Trying to delete record " << record_id
              << " which does not exist on disk.";
    return true;
  }
  if (!base::DeleteFile(record_storage_filename)) {
    LOG(ERROR) << "Fail to delete record " << record_id << " from disk.";
    return false;
  }
  LOG(INFO) << "Done deleting record " << record_id << " from disk.";
  return true;
}

std::string BiodStorage::GenerateNewRecordId() {
  std::string record_id(base::GenerateGUID());
  // dbus member names only allow '_'
  std::replace(record_id.begin(), record_id.end(), '-', '_');
  return record_id;
}

base::FilePath BiodStorage::GetRecordFilename(
    const BiometricsManager::Record& record) {
  std::vector<FilePath> paths = {FilePath(kBiod), FilePath(record.GetUserId()),
                                 FilePath(biometrics_manager_name_),
                                 FilePath(kRecordFileName + record.GetId())};

  FilePath record_storage_filename = root_path_;
  for (const auto& path : paths) {
    if (path.IsAbsolute()) {
      LOG(ERROR) << "Path component must not be absolute: '" << path << "'";
      return base::FilePath();
    }
    record_storage_filename = record_storage_filename.Append(path);
  }

  return record_storage_filename;
}

}  // namespace biod

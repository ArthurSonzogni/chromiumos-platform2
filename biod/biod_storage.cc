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

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/guid.h>
#include <base/json/json_string_value_serializer.h>
#include <base/message_loop/message_loop.h>
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
constexpr char kVersionMember[] = "version";
// Version of the file format.
constexpr int kFormatVersion = 1;
}  // namespace

BiodStorage::BiodStorage(const std::string& biometrics_manager_name,
                         const ReadRecordsCallback& load_record)
    : root_path_(kDaemonStorePath),
      biometrics_manager_name_(biometrics_manager_name),
      load_record_(load_record),
      allow_access_(false) {}

void BiodStorage::SetRootPathForTesting(const base::FilePath& root_path) {
  root_path_ = root_path;
}

bool BiodStorage::WriteRecord(const BiometricsManager::Record& record,
                              std::unique_ptr<base::Value> data) {
  if (!allow_access_) {
    LOG(ERROR) << "Access to the storage mounts not allowed.";
    return false;
  }

  const std::string& record_id(record.GetId());
  base::DictionaryValue record_value;
  record_value.SetString(kLabel, record.GetLabel());
  record_value.SetString(kRecordId, record_id);
  record_value.Set(kData, std::move(data));
  record_value.SetInteger(kVersionMember, kFormatVersion);
  record_value.SetString(kBioManagerMember, biometrics_manager_name_);

  std::string json_string;
  JSONStringValueSerializer json_serializer(&json_string);
  if (!json_serializer.Serialize(record_value)) {
    LOG(ERROR) << "Failed to serialize record with id " << record_id
               << " to JSON.";
    return false;
  }

  std::unique_ptr<ScopedUmask> owner_only_umask(new ScopedUmask(~(0700)));

  FilePath record_storage_filename = root_path_.Append(kBiod)
                                         .Append(record.GetUserId())
                                         .Append(biometrics_manager_name_)
                                         .Append(kRecordFileName + record_id);
  if (!base::CreateDirectory(record_storage_filename.DirName())) {
    PLOG(ERROR) << "Cannot create directory: "
                << record_storage_filename.DirName().value() << ".";
    return false;
  }

  owner_only_umask.reset(new ScopedUmask(~(0600)));

  if (!base::ImportantFileWriter::WriteFileAtomically(record_storage_filename,
                                                      json_string)) {
    LOG(ERROR) << "Failed to write JSON file: "
               << record_storage_filename.value() << ".";
    return false;
  }
  LOG(INFO) << "Done writing record with id " << record_id
            << " to file successfully. ";
  return true;
}

bool BiodStorage::ReadRecords(const std::unordered_set<std::string>& user_ids) {
  bool read_records_from_all_users = true;
  for (const auto& user_id : user_ids) {
    read_records_from_all_users &= ReadRecordsForSingleUser(user_id);
  }
  return read_records_from_all_users;
}

bool BiodStorage::ReadRecordsForSingleUser(const std::string& user_id) {
  if (!allow_access_) {
    LOG(ERROR) << "Access to the storage mounts not yet allowed.";
    return false;
  }

  FilePath biod_path =
      root_path_.Append(kBiod).Append(user_id).Append(biometrics_manager_name_);
  base::FileEnumerator enum_records(biod_path, false,
                                    base::FileEnumerator::FILES, "Record*");
  bool read_all_records_successfully = true;
  for (FilePath record_path = enum_records.Next(); !record_path.empty();
       record_path = enum_records.Next()) {
    std::string json_string;
    if (!base::ReadFileToString(record_path, &json_string)) {
      LOG(ERROR) << "Failed to read the string from " << record_path.value()
                 << ".";
      read_all_records_successfully = false;
      continue;
    }

    JSONStringValueDeserializer json_deserializer(json_string);
    json_deserializer.set_allow_trailing_comma(true);
    int error_code;
    std::string error_message;
    std::unique_ptr<base::Value> record_value(
        json_deserializer.Deserialize(&error_code, &error_message));

    if (!record_value) {
      LOG_IF(ERROR, error_code)
          << "Error in deserializing JSON from path " << record_path.value()
          << " with code " << error_code << ".";
      LOG_IF(ERROR, !error_message.empty())
          << "JSON error message: " << error_message << ".";
      read_all_records_successfully = false;
      continue;
    }

    base::DictionaryValue* record_dictionary;

    if (!record_value->GetAsDictionary(&record_dictionary)) {
      LOG(ERROR) << "Cannot cast " << record_path.value()
                 << " to a dictionary value.";
      read_all_records_successfully = false;
      continue;
    }

    std::string label;

    if (!record_dictionary->GetString(kLabel, &label)) {
      LOG(ERROR) << "Cannot read label from " << record_path.value() << ".";
      read_all_records_successfully = false;
      continue;
    }

    std::string record_id;

    if (!(record_dictionary->GetString(kRecordId, &record_id))) {
      LOG(ERROR) << "Cannot read record id from " << record_path.value() << ".";
      read_all_records_successfully = false;
      continue;
    }

    base::Value* data = nullptr;

    if (!(record_dictionary->Get(kData, &data))) {
      LOG(ERROR) << "Cannot read data from " << record_path.value() << ".";
      read_all_records_successfully = false;
      continue;
    }

    if (!load_record_.Run(user_id, label, record_id, *data)) {
      LOG(ERROR) << "Cannot load record from " << record_path.value() << ".";
      read_all_records_successfully = false;
      continue;
    }
  }
  return read_all_records_successfully;
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
  if (!base::DeleteFile(record_storage_filename, false)) {
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
}  // namespace biod

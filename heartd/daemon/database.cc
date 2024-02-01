// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/database.h"

#include <string>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <sqlite3.h>

#include "heartd/daemon/boot_record.h"

namespace heartd {

namespace {

int TableExistsCallback(void* const /* int* const */ data,
                        const int col_count,
                        char** const cols,
                        char** const /* names */) {
  DCHECK(data != nullptr);
  DCHECK(cols != nullptr);

  auto* const table_count = static_cast<int*>(data);
  if (col_count != 1 || cols[0] == nullptr ||
      !base::StringToInt(cols[0], table_count)) {
    LOG(ERROR) << "Table existence check failed";
    return SQLITE_ERROR;
  }
  return SQLITE_OK;
}

int GetBootRecordCallback(void* const /* std::vector<BootRecord>* const */ data,
                          const int col_count,
                          char** const cols,
                          char** const /* names */) {
  DCHECK(data != nullptr);
  DCHECK(cols != nullptr);

  if (col_count != 2) {
    LOG(ERROR) << "GetBootRecord failed";
    return SQLITE_ERROR;
  }

  auto* const boot_records = static_cast<std::vector<BootRecord>*>(data);
  BootRecord boot_record;
  if (!cols[0]) {
    LOG(ERROR) << "BootRecord.id is null";
    return SQLITE_ERROR;
  }
  boot_record.id = std::string(cols[0]);

  if (!cols[1]) {
    LOG(ERROR) << "BootRecord.time is null";
    return SQLITE_ERROR;
  }

  int64_t time;
  if (!base::StringToInt64(cols[1], &time)) {
    LOG(ERROR) << "BootRecord.time is not a number";
    return SQLITE_ERROR;
  }
  boot_record.time = base::Time::FromMillisecondsSinceUnixEpoch(time);

  boot_records->push_back(boot_record);
  return SQLITE_OK;
}

}  // namespace

Database::Database(const std::string& db_path) {
  sqlite3* db_ptr;
  int result = sqlite3_open(db_path.c_str(), &db_ptr);
  db_ = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>(db_ptr,
                                                           &sqlite3_close);
  if (result != SQLITE_OK) {
    LOG(ERROR) << "Failed to connect to database: " << result;
    db_ = nullptr;
  }
}

Database::~Database() = default;

void Database::Init() const {
  if (!db_) {
    LOG(ERROR) << "No database connection, skip the initialization.";
    return;
  }

  CreateBootRecordTableIfNotExist();
}

bool Database::IsOpen() const {
  return db_ != nullptr;
}

bool Database::TableExists(const std::string& table_name) const {
  if (!IsOpen()) {
    LOG(ERROR) << "Trying to query table of a closed database";
    return false;
  }

  int table_count = 0;
  const std::string sql = base::StringPrintf(
      "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = "
      "'%s';",
      table_name.c_str());
  ExecResult result = ExecSQL(sql, TableExistsCallback, &table_count);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to query table existence: " << result.msg;
    return false;
  }

  if (table_count <= 0)
    return false;

  DCHECK(table_count == 1) << "There should be only one table with name '"
                           << table_name << "'";

  return true;
}

void Database::RemoveOutdatedData(const std::string& table_name) const {
  if (!IsOpen()) {
    LOG(ERROR) << "Trying to modify table of a closed database";
    return;
  }

  auto time_before_30_days = base::Time().Now() - base::Days(30);
  const std::string sql = base::StringPrintf(
      "DELETE FROM %s WHERE time < %" PRId64, table_name.c_str(),
      time_before_30_days.InMillisecondsSinceUnixEpoch());
  ExecResult result = ExecSQL(sql);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to delete outdated data: " << result.msg;
  }
}

void Database::InsertBootRecord(const BootRecord& boot_record) const {
  if (!IsOpen()) {
    LOG(ERROR) << "Trying to modify table of a closed database";
    return;
  }

  const std::string sql = base::StringPrintf(
      "INSERT INTO %s (id, time) VALUES (\"%s\", %" PRId64 ")",
      kBootRecordTable, boot_record.id.c_str(),
      boot_record.time.InMillisecondsSinceUnixEpoch());
  ExecResult result = ExecSQL(sql);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to insert boot record data: " << result.msg;
  }
}

std::vector<BootRecord> Database::GetBootRecordFromTime(
    const base::Time& time) const {
  if (!IsOpen()) {
    LOG(ERROR) << "Trying to query table of a closed database";
    return {};
  }

  std::vector<BootRecord> boot_records;
  const std::string sql =
      base::StringPrintf("SELECT id,time FROM %s WHERE time >= %" PRId64,
                         kBootRecordTable, time.InMillisecondsSinceUnixEpoch());
  ExecResult result = ExecSQL(sql, GetBootRecordCallback, &boot_records);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to query boot record data: " << result.msg;
    return {};
  }

  return boot_records;
}

Database::ExecResult Database::ExecSQL(const std::string& sql) const {
  return ExecSQL(sql, nullptr, nullptr);
}

Database::ExecResult Database::ExecSQL(const std::string& sql,
                                       SqliteCallback callback,
                                       void* data) const {
  char* error_msg = nullptr;
  int result = sqlite3_exec(db_.get(), sql.c_str(), callback, data, &error_msg);

  std::string msg;
  if (error_msg) {
    msg.assign(error_msg);
    sqlite3_free(error_msg);
  }

  return {result, msg};
}

bool Database::CreateBootRecordTableIfNotExist() const {
  if (!IsOpen()) {
    LOG(ERROR) << "Trying to create table of a closed database";
    return false;
  }

  if (TableExists(kBootRecordTable)) {
    return true;
  }

  const std::string sql = base::StringPrintf(
      "CREATE TABLE %s ("
      "id   TEXT    PRIMARY KEY NOT NULL,"
      "time INTEGER NOT NULL)",
      kBootRecordTable);

  const auto result = ExecSQL(sql);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to create " << kBootRecordTable
               << " table: " << result.msg;
    return false;
  }

  return true;
}

}  // namespace heartd

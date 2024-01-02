// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/database.h"

#include <string>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <sqlite3.h>

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

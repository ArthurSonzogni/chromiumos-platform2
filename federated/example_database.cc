// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/example_database.h"

#include <cinttypes>
#include <string>
#include <unordered_set>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <bits/stdint-intn.h>
#include <sqlite3.h>

#include "federated/utils.h"

namespace federated {

namespace {

// Used in CheckIntegrity to extract state code and result string from sql exec.
int IntegrityCheckCallback(void* data, int count, char** row, char** names) {
  CHECK(data);
  CHECK(row);
  auto* integrity_result = static_cast<std::string*>(data);
  if (!row[0]) {
    LOG(ERROR) << "Integrity check returned null";
    return SQLITE_ERROR;
  }
  integrity_result->assign(row[0]);
  return SQLITE_OK;
}

// Used in ClientTableExists to extract state code and table_count from SQL
// exec.
int ClientTableExistsCallback(void* data, int count, char** row, char** names) {
  CHECK(data);
  CHECK(row);
  auto* table_count = static_cast<int*>(data);
  if (!row[0] || !base::StringToInt(row[0], table_count)) {
    LOG(ERROR) << "TableExist check returned invalid data";
    return SQLITE_ERROR;
  }
  return SQLITE_OK;
}

// Prepare sqlite statement group for the given table. Statements (stmt)
// are compiled sql that can bind values to its parameters (`?` in the
// sql string). Table name must be assigned in stmt (not configurable),
// so we must prepare stmt group for each client.
bool PrepareStatements(sqlite3* const db,
                       const std::string& client_name,
                       ExampleDatabase::StmtGroup* stmt_group) {
  std::string sql = base::StringPrintf(
      "SELECT id, example FROM %s ORDER BY id LIMIT ?;", client_name.c_str());
  int result = sqlite3_prepare_v2(db, sql.c_str(), -1,
                                  &stmt_group->stmt_for_streaming, nullptr);
  if (result != SQLITE_OK) {
    LOG(ERROR)
        << "Failed to prepare sqlite statement stmt_for_streaming for client "
        << client_name << " with error message:" << sqlite3_errmsg(db);
    stmt_group->stmt_for_streaming = nullptr;
    return false;
  }

  sql = base::StringPrintf("INSERT INTO %s (example, timestamp) VALUES (?, ?);",
                           client_name.c_str());
  result = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_group->stmt_for_insert,
                              nullptr);
  if (result != SQLITE_OK) {
    LOG(ERROR)
        << "Failed to prepare sqlite statement stmt_for_insert for client "
        << client_name << " with error message:" << sqlite3_errmsg(db);
    stmt_group->stmt_for_insert = nullptr;
    return false;
  }

  sql =
      base::StringPrintf("DELETE FROM %s WHERE id <= ?;", client_name.c_str());
  result = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_group->stmt_for_delete,
                              nullptr);
  if (result != SQLITE_OK) {
    LOG(ERROR)
        << "Failed to prepare sqlite statement stmt_for_delete for client "
        << client_name << " with error message:" << sqlite3_errmsg(db);
    stmt_group->stmt_for_delete = nullptr;
    return false;
  }

  sql = base::StringPrintf("SELECT COUNT(*) FROM %s;", client_name.c_str());
  result = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_group->stmt_for_check,
                              nullptr);
  if (result != SQLITE_OK) {
    LOG(ERROR)
        << "Failed to prepare sqlite statement stmt_for_check for client "
        << client_name << " with error message:" << sqlite3_errmsg(db);
    stmt_group->stmt_for_check = nullptr;
    return false;
  }

  return true;
}

}  // namespace

void ExampleDatabase::StmtGroup::Finalize() {
  // Per https://www.sqlite.org/c3ref/finalize.html, it's harmless to finalize a
  // nullptr.
  sqlite3_finalize(stmt_for_streaming);
  sqlite3_finalize(stmt_for_insert);
  sqlite3_finalize(stmt_for_delete);
  sqlite3_finalize(stmt_for_check);
}

ExampleDatabase::ExampleDatabase(const base::FilePath& db_path,
                                 const std::unordered_set<std::string>& clients)
    : db_path_(db_path), db_(nullptr, nullptr), clients_(clients) {
  for (const auto& client : clients_) {
    DCHECK(!client.empty()) << "Client name cannot be empty";
    stmts_.emplace(client, StmtGroup());
  }
}

ExampleDatabase::~ExampleDatabase() {
  Close();
}

bool ExampleDatabase::Init() {
  sqlite3* db_ptr;
  int result = sqlite3_open(db_path_.MaybeAsASCII().c_str(), &db_ptr);
  db_ = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>(db_ptr,
                                                           &sqlite3_close);
  if (result != SQLITE_OK) {
    LOG(ERROR) << "Failed to connect to database: " << result;
    db_ = nullptr;
    return false;
  }

  for (const auto& client : clients_) {
    if ((!ClientTableExists(client) && !CreateClientTable(client)) ||
        !PrepareStatements(db_.get(), client, &stmts_[client])) {
      LOG(ERROR) << "Failed to prepare table for client " << client;
      Close();

      return false;
    }
  }

  return true;
}

bool ExampleDatabase::IsOpen() const {
  return db_.get() != nullptr;
}

bool ExampleDatabase::Close() {
  if (!db_)
    return true;

  for (const auto& client : clients_) {
    stmts_[client].Finalize();
  }

  // If the database is successfully closed, db_ pointer must be released.
  // Otherwise sqlite3_close will be called again on already released db_
  // pointer by the destructor, which will result in undefined behavior.
  int result = sqlite3_close(db_.get());
  if (result != SQLITE_OK) {
    // This should never happen
    LOG(ERROR) << "sqlite3_close returns error code: " << result;
    return false;
  }

  db_.release();
  return true;
}

bool ExampleDatabase::CheckIntegrity() const {
  // Integrity_check(N) returns a single row and a single column with string
  // "ok" if there is no error. Otherwise a maximum of N rows are returned
  // with each row representing a single error.
  std::string integrity_result;
  ExecResult result = ExecSql("PRAGMA integrity_check(1)",
                              IntegrityCheckCallback, &integrity_result);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to check integrity: (" << result.code << ") "
               << result.error_msg;
    return false;
  }

  return integrity_result == "ok";
}

bool ExampleDatabase::InsertExample(const ExampleRecord& example_record) {
  // The table for example_record.client_name must exist.
  const auto& client_name = example_record.client_name;
  if (clients_.find(client_name) == clients_.end()) {
    LOG(ERROR) << "Unregistered client_name '" << client_name << "'.";
    return false;
  }

  auto* stmt = stmts_[client_name].stmt_for_insert;
  DCHECK(stmt);

  sqlite3_clear_bindings(stmt);
  if (sqlite3_bind_blob(stmt, 1, example_record.serialized_example.c_str(),
                        example_record.serialized_example.length(),
                        nullptr) == SQLITE_OK &&
      sqlite3_bind_int64(stmt, 2, example_record.timestamp.ToJavaTime()) ==
          SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_DONE) {
    sqlite3_reset(stmt);
    return true;
  }

  LOG(ERROR) << "Failed to insert an example to table "
             << example_record.client_name;
  sqlite3_reset(stmt);

  return false;
}

// Streaming examples with sqlite3_step.
bool ExampleDatabase::PrepareStreamingForClient(const std::string& client_name,
                                                const int32_t limit) {
  if (streaming_open_) {
    LOG(ERROR) << "The previous streaming for client "
               << current_streaming_client_
               << "is still open, call CloseStreaming() first.";
    return false;
  }

  if (clients_.find(client_name) == clients_.end()) {
    LOG(ERROR) << "Unregistered client_name '" << client_name << "'.";
    return false;
  }

  int32_t example_count = ExampleCountOfClientTable(client_name);
  if (example_count < kMinExampleCount) {
    DVLOG(1) << "Client '" << client_name << "' example_count " << example_count
             << " doesn't meet the minimum requirement " << kMinExampleCount;
    return false;
  }

  auto* stmt = stmts_[client_name].stmt_for_streaming;
  DCHECK(stmt);

  if (sqlite3_stmt_busy(stmt)) {
    LOG(WARNING) << "An unexpected streaming already exists with sql='"
                 << sqlite3_expanded_sql(stmt) << "', cancelling it now.";
  }
  // Resets the prepared statement anyway.
  sqlite3_reset(stmt);

  sqlite3_clear_bindings(stmt);
  if (sqlite3_bind_int(stmt, 1, limit) != SQLITE_OK) {
    LOG(ERROR) << "Failed to bind limit to stmt_for_streaming of client "
               << client_name;
    sqlite3_reset(stmt);
    return false;
  }

  streaming_open_ = true;
  end_of_streaming_ = false;
  current_streaming_client_ = client_name;
  return true;
}

base::Optional<ExampleRecord> ExampleDatabase::GetNextStreamedRecord() {
  if (!streaming_open_) {
    LOG(ERROR) << "No open streaming, call PrepareStreamingForClient first";
    return base::nullopt;
  }

  if (clients_.find(current_streaming_client_) == clients_.end()) {
    LOG(ERROR) << "Unregistered client_name '" << current_streaming_client_
               << "'.";
    return base::nullopt;
  }

  if (end_of_streaming_) {
    LOG(ERROR) << "The streaming already hit SQLITE_DONE but not closed "
                  "properly, please call CloseStreaming() first.";
    return base::nullopt;
  }

  auto* stmt = stmts_[current_streaming_client_].stmt_for_streaming;
  DCHECK(stmt);

  int code = sqlite3_step(stmt);
  if (code == SQLITE_DONE) {
    end_of_streaming_ = true;
    return base::nullopt;
  }
  if (code != SQLITE_ROW) {
    LOG(ERROR) << "Error when executing sqlite3_step.";
    return base::nullopt;
  }

  int64_t id = sqlite3_column_int64(stmt, 0);
  const unsigned char* example_buffer =
      reinterpret_cast<const unsigned char*>(sqlite3_column_blob(stmt, 1));
  const int example_buffer_len = sqlite3_column_bytes(stmt, 1);
  if (id <= 0 || !example_buffer || example_buffer_len <= 0) {
    LOG(ERROR) << "Failed to extract example from stmt_for_streaming";
    return base::nullopt;
  }
  ExampleRecord example_record;
  example_record.id = id;
  example_record.serialized_example =
      std::string(example_buffer, example_buffer + example_buffer_len);
  return example_record;
}

void ExampleDatabase::CloseStreaming() {
  if (!streaming_open_) {
    LOG(ERROR) << "No open streaming to close";
    return;
  }
  if (clients_.find(current_streaming_client_) == clients_.end()) {
    LOG(ERROR) << "Unregistered client_name '" << current_streaming_client_
               << "'.";
    return;
  }

  auto* stmt = stmts_[current_streaming_client_].stmt_for_streaming;
  DCHECK(stmt);
  sqlite3_reset(stmt);
  current_streaming_client_ = std::string();
  streaming_open_ = false;
  end_of_streaming_ = false;
}

bool ExampleDatabase::DeleteExamplesWithSmallerIdForClient(
    const std::string& client_name, const int64_t id) {
  if (clients_.find(client_name) == clients_.end()) {
    LOG(ERROR) << "Unregistered client_name '" << client_name << "'.";
    return false;
  }

  auto* stmt = stmts_[client_name].stmt_for_delete;
  DCHECK(stmt);

  sqlite3_clear_bindings(stmt);
  if (sqlite3_bind_int64(stmt, 1, id) == SQLITE_OK &&
      sqlite3_step(stmt) == SQLITE_DONE) {
    int delete_count = sqlite3_changes(db_.get());

    sqlite3_reset(stmt);

    if (delete_count <= 0) {
      LOG(ERROR) << "Client " << client_name
                 << " does not have examples with id <= " << id;
      return false;
    }
    return true;
  }
  LOG(ERROR) << "Error in delete examples from table " << client_name
             << " with id <= " << id;

  sqlite3_reset(stmt);

  return false;
}

bool ExampleDatabase::ClientTableExists(const std::string& client_name) const {
  int table_count = 0;
  const std::string sql = base::StringPrintf(
      "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = "
      "'%s';",
      client_name.c_str());
  ExecResult result = ExecSql(sql, ClientTableExistsCallback, &table_count);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to call ClientTableExists for client " << client_name
               << " with ExecResult: (" << result.code << ") "
               << result.error_msg;
    return false;
  }

  if (table_count <= 0)
    return false;

  DCHECK(table_count == 1) << "There should be only one table with name "
                           << client_name;

  return true;
}

bool ExampleDatabase::CreateClientTable(const std::string& client_name) const {
  const std::string sql = base::StringPrintf(
      "CREATE TABLE %s ("
      "  id         INTEGER PRIMARY KEY AUTOINCREMENT"
      "                     NOT NULL,"
      "  example    BLOB    NOT NULL,"
      "  timestamp  INTEGER NOT NULL"
      ")",
      client_name.c_str());
  ExecResult result = ExecSql(sql);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to create table " << client_name << ": ("
               << result.code << ") " << result.error_msg;
    return false;
  }
  return true;
}

int32_t ExampleDatabase::ExampleCountOfClientTable(
    const std::string& client_name) {
  auto* stmt = stmts_[client_name].stmt_for_check;
  DCHECK(stmt);
  sqlite3_reset(stmt);

  int code = sqlite3_step(stmt);
  if (code != SQLITE_ROW) {
    LOG(ERROR)
        << "Error when executing sqlite3_step in ExampleCountOfClientTable.";
    return 0;
  }

  int count = sqlite3_column_int(stmt, 0);
  return count;
}

ExampleDatabase::ExecResult ExampleDatabase::ExecSql(
    const std::string& sql) const {
  return ExecSql(sql, nullptr, nullptr);
}

ExampleDatabase::ExecResult ExampleDatabase::ExecSql(const std::string& sql,
                                                     SqliteCallback callback,
                                                     void* data) const {
  char* error_msg = nullptr;
  int result = sqlite3_exec(db_.get(), sql.c_str(), callback, data, &error_msg);
  // According to sqlite3_exec() documentation, error_msg points to memory
  // allocated by sqlite3_malloc(), which must be freed by sqlite3_free().
  std::string error_msg_str;
  if (error_msg) {
    error_msg_str.assign(error_msg);
    sqlite3_free(error_msg);
  }
  return {result, error_msg_str};
}

}  // namespace federated

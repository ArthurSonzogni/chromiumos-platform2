// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_database.h"

#include <cinttypes>
#include <utility>

#include <base/containers/contains.h>
#include <base/containers/cxx20_erase_set.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <sqlite3.h>

namespace dlp {

namespace {

int GetFileEntriesCallback(void* data, int count, char** row, char** names) {
  auto* file_entries_out = static_cast<std::vector<FileEntry>*>(data);
  FileEntry file_entry;

  if (!row[0]) {
    LOG(ERROR) << "FileEntry.inode is null";
    return SQLITE_ERROR;
  }
  if (!base::StringToInt64(row[0], &file_entry.inode)) {
    LOG(ERROR) << "FileEntry.inode is not a number";
    return SQLITE_ERROR;
  }

  if (!row[1]) {
    LOG(ERROR) << "FileEntry.source_url is null";
    return SQLITE_ERROR;
  }
  file_entry.source_url = row[1];

  if (!row[2]) {
    LOG(ERROR) << "FileEntry.referrer_url is null";
    return SQLITE_ERROR;
  }
  file_entry.referrer_url = row[2];

  file_entries_out->push_back(std::move(file_entry));
  return SQLITE_OK;
}

int GetInodesCallback(void* data, int count, char** row, char** names) {
  auto* inodes_out = static_cast<std::set<int64_t>*>(data);

  if (!row[0]) {
    LOG(ERROR) << "file_entries.inode is null";
    return SQLITE_ERROR;
  }
  int64_t inode;
  if (!base::StringToInt64(row[0], &inode)) {
    LOG(ERROR) << "file_entries.inode is not a number";
    return SQLITE_ERROR;
  }
  inodes_out->insert(inode);
  return SQLITE_OK;
}

// Escapes string in SQL. Replaces ' with ''.
std::string EscapeSQLString(const std::string& string_to_escape) {
  std::string escaped_string = string_to_escape;
  base::ReplaceSubstringsAfterOffset(&escaped_string, 0, "'", "''");
  return escaped_string;
}

}  // namespace

DlpDatabase::DlpDatabase(const base::FilePath& db_path)
    : db_path_(db_path), db_(nullptr, nullptr) {}

DlpDatabase::~DlpDatabase() {
  Close();
}

int DlpDatabase::Init() {
  sqlite3* db_ptr;
  int result = sqlite3_open(db_path_.MaybeAsASCII().c_str(), &db_ptr);
  db_ = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>(db_ptr,
                                                           &sqlite3_close);
  if (result != SQLITE_OK) {
    LOG(ERROR) << "Failed to connect to database: " << result;
    db_ = nullptr;
  }

  if (!FileEntriesTableExists() && !CreateFileEntriesTable()) {
    LOG(ERROR) << "Failed to create file_entries table";
    db_ = nullptr;
  }
  return result;
}

bool DlpDatabase::IsOpen() const {
  return db_.get() != nullptr;
}

int DlpDatabase::Close() {
  if (!db_)
    return SQLITE_OK;

  int result = sqlite3_close(db_.get());
  if (result == SQLITE_OK)
    db_.release();

  return result;
}

bool DlpDatabase::FileEntriesTableExists() const {
  const ExecResult result = ExecSQL("SELECT id FROM file_entries LIMIT 1");
  return result.error_msg.find("no such table") == std::string::npos;
}

bool DlpDatabase::CreateFileEntriesTable() {
  const std::string sql =
      "CREATE TABLE file_entries ("
      " inode INTEGER PRIMARY KEY NOT NULL,"
      " source_url TEXT NOT NULL,"
      " referrer_url TEXT NOT NULL"
      ")";
  const ExecResult result = ExecSQL(sql);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to create table: " << result.error_msg;
    return false;
  }
  return true;
}

bool DlpDatabase::InsertFileEntry(const FileEntry& file_entry) {
  if (!IsOpen())
    return false;

  const std::string sql = base::StringPrintf(
      "INSERT INTO file_entries (inode, source_url, referrer_url)"
      " VALUES (%" PRId64 ", '%s', '%s')",
      file_entry.inode, EscapeSQLString(file_entry.source_url).c_str(),
      EscapeSQLString(file_entry.referrer_url).c_str());
  ExecResult result = ExecSQL(sql);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to insert file entry: (" << result.code << ") "
               << result.error_msg;
    return false;
  }
  return true;
}

std::optional<FileEntry> DlpDatabase::GetFileEntryByInode(int64_t inode) const {
  if (!IsOpen())
    return std::nullopt;

  std::vector<FileEntry> file_entries;
  ExecResult result =
      ExecSQL(base::StringPrintf("SELECT inode,source_url,referrer_url"
                                 " FROM file_entries WHERE inode = %" PRId64,
                                 inode),
              GetFileEntriesCallback, &file_entries);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to query: (" << result.code << ") "
               << result.error_msg;
    return std::nullopt;
  }

  if (file_entries.size() == 0) {
    return std::nullopt;
  }
  if (file_entries.size() > 1) {
    LOG(ERROR) << "Multiple entries for: (" << inode << ")";
  }

  return std::make_optional(file_entries[0]);
}

bool DlpDatabase::DeleteFileEntryByInode(int64_t inode) {
  if (!IsOpen())
    return false;

  const std::string sql = base::StringPrintf(
      "DELETE FROM file_entries WHERE inode = %" PRId64, inode);
  if (ExecDeleteSQL(sql) != 1) {
    LOG(ERROR) << "File entry " << inode << " does not exist in the database";
    return false;
  }

  return true;
}

bool DlpDatabase::DeleteFileEntriesWithInodesNotInSet(
    std::set<ino64_t> inodes_to_keep) {
  if (!IsOpen())
    return false;

  std::set<int64_t> inodes;
  ExecResult result =
      ExecSQL("SELECT inode FROM file_entries", GetInodesCallback, &inodes);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to query: (" << result.code << ") "
               << result.error_msg;
    return false;
  }

  base::EraseIf(inodes, [&inodes_to_keep](int64_t inode) {
    return base::Contains(inodes_to_keep, inode);
  });
  if (inodes.size() == 0) {
    return true;
  }

  std::string sql = "DELETE FROM file_entries WHERE inode IN (";
  bool first = true;
  for (int64_t inode : inodes) {
    if (!first) {
      sql += ",";
    }
    sql += base::NumberToString(inode);
    first = false;
  }
  sql += ")";

  const int deleted = ExecDeleteSQL(sql);
  if (deleted != inodes.size()) {
    LOG(ERROR) << "Failed to cleanup database, deleted: " << deleted
               << ", instead of: " << inodes.size();
    return false;
  }
  return true;
}

DlpDatabase::ExecResult DlpDatabase::ExecSQL(const std::string& sql) const {
  return ExecSQL(sql, nullptr, nullptr);
}

DlpDatabase::ExecResult DlpDatabase::ExecSQL(const std::string& sql,
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

int DlpDatabase::ExecDeleteSQL(const std::string& sql) {
  ExecResult result = ExecSQL(sql);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to delete: (" << result.code << ") "
               << result.error_msg;
    return -1;
  }

  return sqlite3_changes(db_.get());
}

}  // namespace dlp

// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_database.h"

#include <cinttypes>
#include <utility>
#include "dlp/dlp_metrics.h"

#include <base/containers/contains.h>
#include <base/containers/cxx20_erase_set.h>
#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/task/sequenced_task_runner.h>
#include <sqlite3.h>

namespace dlp {

namespace {

int GetLegacyFileEntriesCallback(void* data,
                                 int count,
                                 char** row,
                                 char** names) {
  auto* file_entries_out = static_cast<std::map<FileId, FileEntry>*>(data);
  FileEntry file_entry;

  if (!row[0]) {
    LOG(ERROR) << "FileEntry.inode is null";
    return SQLITE_ERROR;
  }
  if (!base::StringToUint64(row[0], &file_entry.id.first)) {
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

  file_entries_out->insert_or_assign(file_entry.id, std::move(file_entry));
  return SQLITE_OK;
}

int GetFileEntriesCallback(void* data, int count, char** row, char** names) {
  auto* file_entries_out = static_cast<std::map<FileId, FileEntry>*>(data);
  FileEntry file_entry;

  if (!row[0]) {
    LOG(ERROR) << "FileEntry.inode is null";
    return SQLITE_ERROR;
  }
  if (!base::StringToUint64(row[0], &file_entry.id.first)) {
    LOG(ERROR) << "FileEntry.inode is not a number";
    return SQLITE_ERROR;
  }

  if (!row[1]) {
    LOG(ERROR) << "FileEntry.crtime is null";
    return SQLITE_ERROR;
  }
  if (!base::StringToInt64(row[1], &file_entry.id.second)) {
    LOG(ERROR) << "FileEntry.crtime is not a number";
    return SQLITE_ERROR;
  }

  if (!row[2]) {
    LOG(ERROR) << "FileEntry.source_url is null";
    return SQLITE_ERROR;
  }
  file_entry.source_url = row[2];

  if (!row[3]) {
    LOG(ERROR) << "FileEntry.referrer_url is null";
    return SQLITE_ERROR;
  }
  file_entry.referrer_url = row[3];

  file_entries_out->insert_or_assign(file_entry.id, std::move(file_entry));
  return SQLITE_OK;
}

int GetIdsCallback(void* data, int count, char** row, char** names) {
  auto* ids_out = static_cast<std::set<FileId>*>(data);

  if (!row[0] || !row[1]) {
    LOG(ERROR) << "file_entries.id is null";
    return SQLITE_ERROR;
  }
  FileId id;
  if (!base::StringToUint64(row[0], &id.first) ||
      !base::StringToInt64(row[1], &id.second)) {
    LOG(ERROR) << "file_entries.id is not a number";
    return SQLITE_ERROR;
  }
  ids_out->insert(id);
  return SQLITE_OK;
}

// Escapes string in SQL. Replaces ' with ''.
std::string EscapeSQLString(const std::string& string_to_escape) {
  std::string escaped_string = string_to_escape;
  base::ReplaceSubstringsAfterOffset(&escaped_string, 0, "'", "''");
  return escaped_string;
}

}  // namespace

class DlpDatabase::Core {
 public:
  // Creates an instance to talk to the database file at |db_path|. Init() must
  // be called to establish connection.
  Core(const base::FilePath& db_path,
       scoped_refptr<base::SequencedTaskRunner> parent_task_runner,
       DlpDatabaseDelegate* const delegate);

  // Not copyable or movable.
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

  ~Core();

  // Implements the functionality from main class.
  std::pair<int, bool> Init();
  bool UpsertFileEntry(const FileEntry& file_entry);
  bool UpsertLegacyFileEntryForTesting(const FileEntry& file_entry);
  bool UpsertFileEntries(const std::vector<FileEntry>& file_entries);
  std::map<FileId, FileEntry> GetFileEntriesByIds(
      std::vector<FileId> ids) const;
  std::map<FileId, FileEntry> GetAllEntries() const;
  bool DeleteFileEntryByInode(ino64_t inode);
  bool DeleteFileEntriesWithIdsNotInSet(std::set<FileId> ids_to_keep);
  bool MigrateDatabase(const std::vector<FileId>& existing_files);

 private:
  // Returns true if the database connection is open.
  bool IsOpen() const;
  // Closes database connection. Returns |SQLITE_OK| if no error occurred.
  // Otherwise SQLite error code is returned.
  int Close();
  // Returns true if file entries table exists.
  bool FileEntriesTableExists() const;
  bool FileEntriesLegacyTableExists() const;
  // Creates new file entries table. Returns true if no error occurred.
  bool CreateFileEntriesTable();
  bool CreateFileEntriesLegacyTableForTesting();

  using SqliteCallback = int (*)(void*, int, char**, char**);
  // Struct holding the result of a call to Sqlite.
  struct ExecResult {
    int code;
    std::string error_msg;
  };
  // Execute SQL.
  ExecResult ExecSQL(const std::string& sql) const;
  ExecResult ExecSQL(const std::string& sql,
                     SqliteCallback callback,
                     void* data) const;
  // Executes SQL that deletes rows. Returns number of rows affected. Returns -1
  // if error occurs.
  int ExecDeleteSQL(const std::string& sql);

  void ForwardUMAErrorToParentThread(DatabaseError error) const;

  const base::FilePath db_path_;
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_;

  // Task runner from which this thread is started and where the delegate is
  // running.
  scoped_refptr<base::SequencedTaskRunner> parent_task_runner_;
  DlpDatabaseDelegate* const delegate_;
};

DlpDatabase::Core::Core(
    const base::FilePath& db_path,
    scoped_refptr<base::SequencedTaskRunner> parent_task_runner,
    DlpDatabaseDelegate* const delegate)
    : db_path_(db_path),
      db_(nullptr, nullptr),
      parent_task_runner_(parent_task_runner),
      delegate_(delegate) {
  CHECK(delegate_);
  CHECK(parent_task_runner_->RunsTasksInCurrentSequence());
}

DlpDatabase::Core::~Core() {
  Close();
}

std::pair<int, bool> DlpDatabase::Core::Init() {
  sqlite3* db_ptr;
  int result = sqlite3_open(db_path_.MaybeAsASCII().c_str(), &db_ptr);
  db_ = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>(db_ptr,
                                                           &sqlite3_close);
  if (result != SQLITE_OK) {
    LOG(ERROR) << "Failed to connect to database: " << result;
    ForwardUMAErrorToParentThread(DatabaseError::kConnectionError);
    db_ = nullptr;
  }

  if (!FileEntriesTableExists() && !CreateFileEntriesTable()) {
    LOG(ERROR) << "Failed to create file_entries table";
    ForwardUMAErrorToParentThread(DatabaseError::kCreateTableError);
    db_ = nullptr;
  }
  return {result, /*migration_needed=*/result == SQLITE_OK &&
                      FileEntriesLegacyTableExists()};
}

bool DlpDatabase::Core::IsOpen() const {
  return db_.get() != nullptr;
}

int DlpDatabase::Core::Close() {
  if (!db_)
    return SQLITE_OK;

  int result = sqlite3_close(db_.get());
  if (result == SQLITE_OK)
    db_.release();

  return result;
}

bool DlpDatabase::Core::FileEntriesTableExists() const {
  const ExecResult result =
      ExecSQL("SELECT id FROM file_entries_crtime LIMIT 1");
  return result.error_msg.find("no such table") == std::string::npos;
}

bool DlpDatabase::Core::FileEntriesLegacyTableExists() const {
  const ExecResult result = ExecSQL("SELECT id FROM file_entries LIMIT 1");
  return result.error_msg.find("no such table") == std::string::npos;
}

bool DlpDatabase::Core::CreateFileEntriesTable() {
  const std::string sql =
      "CREATE TABLE file_entries_crtime ("
      " inode INTEGER NOT NULL,"
      " crtime INTEGER NOT NULL,"
      " source_url TEXT NOT NULL,"
      " referrer_url TEXT NOT NULL,"
      "PRIMARY KEY(inode, crtime))";
  const ExecResult result = ExecSQL(sql);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to create table: " << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kCreateTableError);
    return false;
  }
  return true;
}

bool DlpDatabase::Core::CreateFileEntriesLegacyTableForTesting() {
  const std::string sql =
      "CREATE TABLE file_entries ("
      " inode INTEGER PRIMARY KEY NOT NULL,"
      " source_url TEXT NOT NULL,"
      " referrer_url TEXT NOT NULL"
      ")";
  const ExecResult result = ExecSQL(sql);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to create table: " << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kCreateTableError);
    return false;
  }
  return true;
}

bool DlpDatabase::Core::UpsertFileEntry(const FileEntry& file_entry) {
  if (!IsOpen())
    return false;

  const std::string sql = base::StringPrintf(
      "INSERT OR REPLACE INTO file_entries_crtime (inode, crtime, source_url, "
      "referrer_url)"
      " VALUES (%" PRId64 ", %" PRId64 ", '%s', '%s')",
      file_entry.id.first, file_entry.id.second,
      EscapeSQLString(file_entry.source_url).c_str(),
      EscapeSQLString(file_entry.referrer_url).c_str());
  ExecResult result = ExecSQL(sql);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to insert file entry: (" << result.code << ") "
               << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kInsertIntoTableError);
    return false;
  }
  return true;
}

bool DlpDatabase::Core::UpsertLegacyFileEntryForTesting(
    const FileEntry& file_entry) {
  if (!IsOpen() || !CreateFileEntriesLegacyTableForTesting())
    return false;

  const std::string sql = base::StringPrintf(
      "INSERT OR REPLACE INTO file_entries (inode, source_url, referrer_url)"
      " VALUES (%" PRId64 ", '%s', '%s')",
      file_entry.id.first, EscapeSQLString(file_entry.source_url).c_str(),
      EscapeSQLString(file_entry.referrer_url).c_str());
  ExecResult result = ExecSQL(sql);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to insert file entry: (" << result.code << ") "
               << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kInsertIntoTableError);
    return false;
  }
  return true;
}

bool DlpDatabase::Core::UpsertFileEntries(
    const std::vector<FileEntry>& file_entries) {
  if (!IsOpen()) {
    LOG(ERROR) << "Failed to insert file entries because database is not open";
    return false;
  }

  std::string sql =
      "INSERT OR REPLACE INTO file_entries_crtime (inode, crtime, source_url, "
      "referrer_url) "
      "VALUES";
  bool first = true;
  for (const auto& file_entry : file_entries) {
    if (!first) {
      sql += ",";
    }
    sql += base::StringPrintf("(%" PRId64 ", %" PRId64 ", '%s', '%s')",
                              file_entry.id.first, file_entry.id.second,
                              EscapeSQLString(file_entry.source_url).c_str(),
                              EscapeSQLString(file_entry.referrer_url).c_str());
    first = false;
  }
  sql += ";";

  ExecResult result = ExecSQL(sql);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to insert file entries: (" << result.code << ") "
               << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kInsertIntoTableError);
    return false;
  }
  return true;
}

std::map<FileId, FileEntry> DlpDatabase::Core::GetFileEntriesByIds(
    std::vector<FileId> ids) const {
  std::map<FileId, FileEntry> file_entries;
  if (!IsOpen())
    return file_entries;

  std::string sql;
  sql +=
      "SELECT inode,crtime,source_url,referrer_url FROM file_entries_crtime "
      "WHERE (inode, crtime) IN (";
  bool first = true;
  for (FileId id : ids) {
    if (!first) {
      sql += ",";
    }
    sql += "(" + base::NumberToString(id.first) + ", " +
           base::NumberToString(id.second) + ")";
    first = false;
  }
  sql += ")";

  ExecResult result = ExecSQL(sql, GetFileEntriesCallback, &file_entries);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to query: (" << result.code << ") "
               << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kQueryError);
    file_entries.clear();
    return file_entries;
  }

  return file_entries;
}

std::map<FileId, FileEntry> DlpDatabase::Core::GetAllEntries() const {
  std::map<FileId, FileEntry> file_entries;
  if (!IsOpen())
    return file_entries;

  std::string sql;
  sql += "SELECT inode,crtime,source_url,referrer_url FROM file_entries_crtime";

  ExecResult result = ExecSQL(sql, GetFileEntriesCallback, &file_entries);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to query: (" << result.code << ") "
               << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kQueryError);
    file_entries.clear();
    return file_entries;
  }

  return file_entries;
}

bool DlpDatabase::Core::DeleteFileEntryByInode(ino64_t inode) {
  if (!IsOpen())
    return false;

  const std::string sql = base::StringPrintf(
      "DELETE FROM file_entries_crtime WHERE inode = %" PRId64, inode);
  return ExecDeleteSQL(sql) >= 0;
}

bool DlpDatabase::Core::DeleteFileEntriesWithIdsNotInSet(
    std::set<FileId> ids_to_keep) {
  if (!IsOpen())
    return false;

  std::set<FileId> ids;
  ExecResult result = ExecSQL("SELECT inode,crtime FROM file_entries_crtime",
                              GetIdsCallback, &ids);
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to query: (" << result.code << ") "
               << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kQueryError);
    return false;
  }

  base::EraseIf(ids, [&ids_to_keep](FileId id) {
    return base::Contains(ids_to_keep, id);
  });
  if (ids.size() == 0) {
    return true;
  }

  std::string sql =
      "DELETE FROM file_entries_crtime WHERE (inode, crtime) IN (";
  bool first = true;
  for (FileId id : ids) {
    if (!first) {
      sql += ",";
    }
    sql += "(" + base::NumberToString(id.first) + ", " +
           base::NumberToString(id.second) + ")";
    first = false;
  }
  sql += ")";

  const int deleted = ExecDeleteSQL(sql);
  if (deleted != ids.size()) {
    LOG(ERROR) << "Failed to cleanup database, deleted: " << deleted
               << ", instead of: " << ids.size();
    return false;
  }
  return true;
}

bool DlpDatabase::Core::MigrateDatabase(
    const std::vector<FileId>& existing_files) {
  // The old database is already migrated and removed.
  if (!IsOpen() || !FileEntriesLegacyTableExists()) {
    return true;
  }
  std::map<FileId, FileEntry> old_file_entries;

  std::string sql = "SELECT inode,source_url,referrer_url FROM file_entries";
  ExecResult result =
      ExecSQL(sql, GetLegacyFileEntriesCallback, &old_file_entries);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to query: (" << result.code << ") "
               << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kQueryError);
    return false;
  }
  LOG(INFO) << "Found " << old_file_entries.size() << " entries to migrate";

  std::map<ino64_t, time_t> inode_to_crtime;
  for (const auto& file_id : existing_files) {
    inode_to_crtime[file_id.first] = file_id.second;
  }

  std::vector<FileEntry> entries_to_add;
  for (const auto& [old_file_id, old_entry] : old_file_entries) {
    auto iter = inode_to_crtime.find(old_file_id.first);
    if (iter != inode_to_crtime.end()) {
      FileEntry entry;
      entry.id = {old_file_id.first, iter->second};
      entry.source_url = old_entry.source_url;
      entry.referrer_url = old_entry.referrer_url;
      entries_to_add.emplace_back(entry);
    } else {
      LOG(ERROR) << "Not found file while migrating, inode="
                 << old_file_id.first;
    }
  }

  if (entries_to_add.size() > 0 && !UpsertFileEntries(entries_to_add)) {
    return false;
  }
  LOG(INFO) << "Migrated " << entries_to_add.size() << " entries";

  result = ExecSQL("DROP TABLE file_entries");
  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to delete legacy table: (" << result.code << ") "
               << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kQueryError);
    return false;
  }
  return true;
}

DlpDatabase::Core::ExecResult DlpDatabase::Core::ExecSQL(
    const std::string& sql) const {
  return ExecSQL(sql, nullptr, nullptr);
}

DlpDatabase::Core::ExecResult DlpDatabase::Core::ExecSQL(
    const std::string& sql, SqliteCallback callback, void* data) const {
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

int DlpDatabase::Core::ExecDeleteSQL(const std::string& sql) {
  ExecResult result = ExecSQL(sql);

  if (result.code != SQLITE_OK) {
    LOG(ERROR) << "Failed to delete: (" << result.code << ") "
               << result.error_msg;
    ForwardUMAErrorToParentThread(DatabaseError::kDeleteError);
    return -1;
  }

  return sqlite3_changes(db_.get());
}

void DlpDatabase::Core::ForwardUMAErrorToParentThread(
    DatabaseError error) const {
  parent_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&DlpDatabaseDelegate::OnDatabaseError,
                                base::Unretained(delegate_), error));
}

DlpDatabase::DlpDatabase(const base::FilePath& db_path, Delegate* delegate)
    : database_thread_("dlp_database_thread"), delegate_(delegate) {
  DCHECK(delegate);
  CHECK(database_thread_.Start()) << "Failed to start database thread.";
  task_runner_ = database_thread_.task_runner();

  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  core_ = std::make_unique<Core>(
      db_path, base::SequencedTaskRunner::GetCurrentDefault(), this);
}

DlpDatabase::~DlpDatabase() {
  core_.reset();
  database_thread_.Stop();
}

void DlpDatabase::Init(
    base::OnceCallback<void(std::pair<int, bool>)> callback) {
  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DlpDatabase::Core::Init, base::Unretained(core_.get())),
      std::move(callback));
}

void DlpDatabase::UpsertFileEntry(const FileEntry& file_entry,
                                  base::OnceCallback<void(bool)> callback) {
  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DlpDatabase::Core::UpsertFileEntry,
                     base::Unretained(core_.get()), file_entry),
      std::move(callback));
}

void DlpDatabase::UpsertLegacyFileEntryForTesting(
    const FileEntry& file_entry, base::OnceCallback<void(bool)> callback) {
  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DlpDatabase::Core::UpsertLegacyFileEntryForTesting,
                     base::Unretained(core_.get()), file_entry),
      std::move(callback));
}

void DlpDatabase::UpsertFileEntries(const std::vector<FileEntry>& file_entries,
                                    base::OnceCallback<void(bool)> callback) {
  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DlpDatabase::Core::UpsertFileEntries,
                     base::Unretained(core_.get()), file_entries),
      std::move(callback));
}

void DlpDatabase::GetFileEntriesByIds(
    std::vector<FileId> ids,
    base::OnceCallback<void(std::map<FileId, FileEntry>)> callback) const {
  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DlpDatabase::Core::GetFileEntriesByIds,
                     base::Unretained(core_.get()), std::move(ids)),
      std::move(callback));
}

void DlpDatabase::GetDatabaseEntries(
    base::OnceCallback<void(std::map<FileId, FileEntry>)> callback) const {
  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DlpDatabase::Core::GetAllEntries,
                     base::Unretained(core_.get())),
      std::move(callback));
}

void DlpDatabase::DeleteFileEntryByInode(
    ino64_t inode, base::OnceCallback<void(bool)> callback) {
  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DlpDatabase::Core::DeleteFileEntryByInode,
                     base::Unretained(core_.get()), inode),
      std::move(callback));
}

void DlpDatabase::DeleteFileEntriesWithIdsNotInSet(
    std::set<FileId> ids_to_keep, base::OnceCallback<void(bool)> callback) {
  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DlpDatabase::Core::DeleteFileEntriesWithIdsNotInSet,
                     base::Unretained(core_.get()), ids_to_keep),
      std::move(callback));
}

void DlpDatabase::MigrateDatabase(const std::vector<FileId>& existing_files,
                                  base::OnceCallback<void(bool)> callback) {
  CHECK(!task_runner_->RunsTasksInCurrentSequence());
  task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DlpDatabase::Core::MigrateDatabase,
                     base::Unretained(core_.get()), existing_files),
      std::move(callback));
}

void DlpDatabase::OnDatabaseError(DatabaseError error) {
  delegate_->OnDatabaseError(error);
}

}  // namespace dlp

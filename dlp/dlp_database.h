// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLP_DLP_DATABASE_H_
#define DLP_DLP_DATABASE_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <sqlite3.h>

namespace dlp {

// Database for storing information about DLP-protected files.

// |FileEntry| objects stored in |file_entries| table.
// A file entry represents a DLP-protected file. |inode| identifies the file on
// the user filesystem, |source_url| and |referrer_url| (possibly empty)
// tell where from the file was originated.
struct FileEntry {
  int64_t inode;
  std::string source_url;
  std::string referrer_url;
};

// Provides API to access database and base functions.
class DlpDatabase {
 public:
  // Creates an instance to talk to the database file at |db_path|. Init() must
  // be called to establish connection.
  explicit DlpDatabase(const base::FilePath& db_path);

  // Not copyable or movable.
  DlpDatabase(const DlpDatabase&) = delete;
  DlpDatabase& operator=(const DlpDatabase&) = delete;

  ~DlpDatabase();

  // Initializes database connection. Must be called before any other queries.
  // Returns |SQLITE_OK| if no error occurred.
  int Init();
  // Returns true if the database connection is open.
  bool IsOpen() const;
  // Closes database connection. Returns |SQLITE_OK| if no error occurred.
  // Otherwise SQLite error code is returned.
  int Close();
  // Returns true if file entries table exists.
  bool FileEntriesTableExists() const;
  // Creates new file entries table. Returns true if no error occurred.
  bool CreateFileEntriesTable();
  // Inserts file_entry into database. Returns true if no error occurred.
  bool InsertFileEntry(const FileEntry& file_entry);

  // Gets the file entries by inode. Returns nullopt if any error occurs or not
  // found.
  std::optional<FileEntry> GetFileEntryByInode(int64_t inode) const;
  // Deletes file entry with |inode| from database. Returns true if no error
  // occurred.
  bool DeleteFileEntryByInode(int64_t inode);
  // Filters the file entries table to contain only entries with
  // |inodes_to_keep| inode values.
  bool DeleteFileEntriesWithInodesNotInSet(std::set<ino64_t> inodes_to_keep);

 private:
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

  const base::FilePath db_path_;
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_;
};

}  // namespace dlp

#endif  // DLP_DLP_DATABASE_H_

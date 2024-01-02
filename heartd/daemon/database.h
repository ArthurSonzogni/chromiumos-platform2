// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_DATABASE_H_
#define HEARTD_DAEMON_DATABASE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/time/time.h>
#include <sqlite3.h>

#include "heartd/daemon/boot_record.h"

namespace heartd {

constexpr char kBootRecordTable[] = "boot_record";

class Database {
 public:
  explicit Database(const std::string& db_path = "/var/lib/heartd/database");
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  ~Database();

  void Init() const;
  bool IsOpen() const;
  bool TableExists(const std::string& table_name) const;
  void RemoveOutdatedData(const std::string& table_name) const;

  // Boot metrics related methods.
  void InsertBootRecord(const BootRecord& boot_record) const;
  std::vector<BootRecord> GetBootRecordFromTime(const base::Time& time) const;

 private:
  struct ExecResult {
    int code;
    std::string msg;
  };

 private:
  using SqliteCallback = int (*)(void*, int, char**, char**);
  ExecResult ExecSQL(const std::string& sql) const;
  ExecResult ExecSQL(const std::string& sql,
                     SqliteCallback callback,
                     void* data) const;
  bool CreateBootRecordTableIfNotExist() const;

 private:
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_{nullptr, nullptr};
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_DATABASE_H_

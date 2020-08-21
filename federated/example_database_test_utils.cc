// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/example_database_test_utils.h"

#include <array>
#include <cinttypes>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <sqlite3.h>

namespace federated {
namespace {

constexpr char kCreateDatabaseSql[] =
    "CREATE TABLE test_client_1 ("
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT"
    "                     NOT NULL,"
    "  example    BLOB    NOT NULL,"
    "  timestamp  INTEGER NOT NULL"
    ")";

int ExecSql(const base::FilePath& db_path,
            const std::vector<std::string>& sqls) {
  sqlite3* db;
  int result;
  result = sqlite3_open(db_path.MaybeAsASCII().c_str(), &db);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_ptr(db, &sqlite3_close);
  if (result != SQLITE_OK)
    return result;

  for (const auto& sql : sqls) {
    result = sqlite3_exec(db_ptr.get(), sql.c_str(), nullptr, nullptr, nullptr);
    if (result != SQLITE_OK)
      return result;
  }

  return sqlite3_close(db_ptr.release());
}

}  // namespace

int CreateDatabaseForTesting(const base::FilePath& db_path) {
  std::vector<std::string> create_db_sql;
  create_db_sql.push_back(kCreateDatabaseSql);

  for (int i = 1; i <= 100; i++) {
    create_db_sql.push_back(base::StringPrintf(
        "INSERT INTO test_client_1 (example, timestamp) VALUES ('example_%d', "
        "%" PRId64 ")",
        i, base::Time::Now().ToJavaTime()));
  }

  return ExecSql(db_path, create_db_sql);
}

}  // namespace federated

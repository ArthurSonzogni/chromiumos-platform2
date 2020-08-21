// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_EXAMPLE_DATABASE_H_
#define FEDERATED_EXAMPLE_DATABASE_H_

#include <bits/stdint-intn.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <base/files/file_path.h>
#include <base/optional.h>
#include <base/time/time.h>
#include <sqlite3.h>

namespace federated {

// Example objects stored in corresponding `client_name` tables.
// An example represents a training example of federated computation.
struct ExampleRecord {
  int64_t id;
  std::string client_name;
  std::string serialized_example;
  base::Time timestamp;
};

// Provides access to example database.
// Example usage:
// Construct and initialize:
//    ExampleDatabase db(db_path, kTestClients);
//    if(!db.Init() || !db.IsOpen() || !db.CheckIntegrity()) {
//      // Error handling
//    }
//
// Insert an example:
//    ExampleRecord example_record;
//    example_record.client_name = client_name;
//    example_record.serialized_example = serialized_example;
//    example_record.timestamp = base::Time::Now();
//
//    db.InsertExample(example_record);
//
// Query examples:
//     int limit = 100;
//     if (db.PrepareStreamingForClient(client_name, limit)) {
//       // Error handling
//     } else {
//       // Call GetNextStreamedRecord() repeatedly until it returns
//       // a base::nullopt, then CloseStreaming();
//       while (true) {
//         auto maybe_example_record = db.GetNextStreamedRecord();
//         if (maybe_example_record == base::nullopt) {
//           // end of iterator
//           break;
//         } else {
//           ExampleRecord record = maybe_example_record.value();
//         }
//       }
//       db.CloseStreaming();
//     }
//
// Delete examples with id smaller than the given id from table `client_name`:
//     // Keeps track of last_seen_id when querying examples.
//     if (!db.DeleteExamplesWithSmallerIdForClient(client_name, id)) {
//       // Error handling
//     }
// See example_database_test.cc and storage_manager_impl.cc for more details.

class ExampleDatabase {
 public:
  struct StmtGroup {
    sqlite3_stmt* stmt_for_streaming = nullptr;
    sqlite3_stmt* stmt_for_insert = nullptr;
    sqlite3_stmt* stmt_for_delete = nullptr;
    sqlite3_stmt* stmt_for_check = nullptr;
    // Finalizes the stmts, required before disconnecting the db.
    void Finalize();
  };

  // Creates an instance to talk to the database file at `db_path`. Init() must
  // be called to establish connection.
  explicit ExampleDatabase(const base::FilePath& db_path,
                           const std::unordered_set<std::string>& clients);
  ExampleDatabase(const ExampleDatabase&) = delete;
  ExampleDatabase& operator=(const ExampleDatabase&) = delete;

  virtual ~ExampleDatabase();

  // Initializes database connection. Must be called before any other queries.
  // Returns true if no error occurred.
  virtual bool Init();
  // Returns true if the database connection is open.
  virtual bool IsOpen() const;
  // Closes database connection. Returns true if no error occurred.
  virtual bool Close();
  // Runs sqlite built-in integrity check. Returns true if no error is found.
  virtual bool CheckIntegrity() const;
  // Inserts example into database. Returns true if no error occurred.
  virtual bool InsertExample(const ExampleRecord& example_record);

  // Streaming examples with sqlite3_step, return true if table of client_name
  // has more than a minimum number of examples and the stmt binds values
  // successfully. The minimum number now is kMinExampleCount = 1 defined in
  // utils.h/cc.
  virtual bool PrepareStreamingForClient(const std::string& client_name,
                                         const int32_t limit);
  virtual base::Optional<ExampleRecord> GetNextStreamedRecord();
  virtual void CloseStreaming();

  // Deletes examples with id <= given id from client table. Returns true if no
  // error occurred.
  virtual bool DeleteExamplesWithSmallerIdForClient(
      const std::string& client_name, const int64_t id);

 private:
  // Typedef of sqlite3_exec callback, see sqlite doc:
  // https://sqlite.org/c3ref/exec.html.
  using SqliteCallback = int (*)(void* /*data*/,
                                 int /*count*/,
                                 char** /*row*/,
                                 char** /*names*/);

  // Sqlite error code and error message.
  struct ExecResult {
    int code;
    std::string error_msg;
  };

  // Returns true if the client's table exists.
  bool ClientTableExists(const std::string& client_name) const;
  // Returns true if the client's table is created without error.
  bool CreateClientTable(const std::string& client_name) const;

  // Returns the count of examples in the client's table.
  int32_t ExampleCountOfClientTable(const std::string& client_name);

  // Executes sql.
  ExecResult ExecSql(const std::string& sql) const;
  ExecResult ExecSql(const std::string& sql,
                     SqliteCallback callback,
                     void* data) const;

  const base::FilePath db_path_;
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_;

  // The set of registered client names.
  std::unordered_set<std::string> clients_;
  // Mapping client_name to sqlite prepared statement objects for streaming,
  // inserting and deleting examples.
  std::unordered_map<std::string, StmtGroup> stmts_;
  //  The client with open example stream.
  std::string current_streaming_client_;
  // Whether there is an open streaming.
  bool streaming_open_ = false;
  // Whether current streaming hits SQLITE_DONE, to early return in
  // GetNextStreamedRecord if current streaming already ends but is not closed.
  // Relationship between streaming_open_ and end_of_streaming_:
  // streaming_open_ && !end_of_streaming_: safe to call GetNextStreamedRecord
  // streaming_open_ && end_of_streaming_: should call CloseStreaming
  // !streaming_open_ && !end_of_streaming_: ready to PrepareStreamingForClient
  // !streaming_open_ && end_of_streaming_: invalid, should never happen
  bool end_of_streaming_ = false;
};

}  // namespace federated

#endif  // FEDERATED_EXAMPLE_DATABASE_H_

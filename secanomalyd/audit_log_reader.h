// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// AuditLogReader is used to read audit records from /var/log/audit/audit.log.
// Parser is used to parse and validate various types of records.

#ifndef SECANOMALYD_AUDIT_LOG_READER_H_
#define SECANOMALYD_AUDIT_LOG_READER_H_

#include "secanomalyd/text_file_reader.h"

#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include <asm-generic/errno-base.h>
#include <base/files/file_util.h>
#include <base/time/time.h>
#include <re2/re2.h>

namespace secanomalyd {

const base::FilePath kAuditLogPath("/var/log/audit/audit.log");

// Pattern used for catching audit log records of the type AVC.
// First group captures Unix timestamp.
// Second group captures the log message.
// Example of an AVC log record:
// type=AVC msg=audit(1666373231.610:518): ChromeOS LSM: memfd execution
// attempt, cmd="./memfd_test.execv.elf", filename=/proc/self/fd/3
constexpr char kAVCRecordPattern[] = R"(type=AVC [^(]+\(([\d\.]+)\S+ (.+))";
constexpr char kAVCRecordTag[] = "AVC";

// Represents a record (one entry) in the audit log file.
// |tag| identifies the type of record and the parser that should be used on it.
// |message| holds the content of the log after the type and the timestamp.
// |timestamp| holds the timestamp of the log, converted to base::Time object.
struct LogRecord {
  std::string tag;
  std::string message;
  base::Time timestamp;
};

// Returns true if the log message indicates a memfd execution attempt.
bool IsMemfdExecutionAttempt(const std::string& log_message);

// A Parser object is created for each log record type we are interested in.
// Each parser is uniquely identified by a |tag_| that determines the type of
// record it should be used on, and a |pattern_| which matches the pattern for
// the targeted record type.
class Parser {
 public:
  Parser(std::string tag, std::unique_ptr<RE2> pattern)
      : tag_(tag), pattern_(std::move(pattern)) {}
  ~Parser() = default;

  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  // Determines whether the supplied log line matches the pattern for this
  // parser and parses the log line into the LogRecord data structure.
  bool IsValid(const std::string& line, LogRecord& log_record);

 private:
  const std::string tag_;
  const std::unique_ptr<RE2> pattern_;
};

// AuditLogReader parses newline-delimited log record into structs and uses
// parser objects to determine if the line is valid.
// It uses secanomalyd::TextFileReader for reading lines in the log files and
// handling log rotations.
class AuditLogReader {
 public:
  explicit AuditLogReader(const base::FilePath& path)
      : log_file_path_(path), log_file_(path) {
    parser_map_[kAVCRecordTag] = std::make_unique<Parser>(
        kAVCRecordTag, std::make_unique<RE2>(kAVCRecordPattern));
    log_file_.SeekToEnd();
  }
  ~AuditLogReader() = default;

  AuditLogReader(const AuditLogReader&) = delete;
  AuditLogReader& operator=(const AuditLogReader&) = delete;

  // Returns true while there are log records in the log file.
  bool GetNextEntry(LogRecord* log_record);

 private:
  // Parses a line from log_file_.
  bool ReadLine(const std::string& line, LogRecord& log_record);

  // Moves the position of log_file_ to the beginning.
  // Only used for testing.
  void SeekToBegin();

  const base::FilePath log_file_path_;

  // TextFileReader is defined in text_file_reader.h.
  TextFileReader log_file_;

  // Keeps a map of all the parser objects that should be tested against the log
  // records found in the log file.
  std::map<std::string, std::unique_ptr<Parser>> parser_map_;

  FRIEND_TEST(AuditLogReaderTest, AuditLogReaderTest);
  FRIEND_TEST(AuditLogReaderTest, NoRereadingTest);
};

}  // namespace secanomalyd

#endif  // SECANOMALYD_AUDIT_LOG_READER_H_

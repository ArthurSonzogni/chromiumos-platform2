// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_LOG_LINE_READER_H_
#define CROSLOG_LOG_LINE_READER_H_

#include <memory>
#include <string>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/memory_mapped_file.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"

#include "croslog/file_change_watcher.h"
#include "croslog/log_entry.h"
#include "croslog/log_parser.h"

namespace croslog {

class LogLineReader : public FileChangeWatcher::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnFileChanged(LogLineReader* reader) = 0;
  };

  enum class Backend {
    FILE,
    FILE_FOLLOW,
    MEMORY_FOR_TEST,
  };

  explicit LogLineReader(Backend backend_mode);
  virtual ~LogLineReader();

  // Open the file to read.
  void OpenFile(const base::FilePath& file_path);
  // Open the buffer on memory instead of a file.
  void OpenMemoryBufferForTest(const char* buffer, size_t size);

  // Read the next line from log.
  base::Optional<std::string> Forward();
  // Read the previous line from log.
  base::Optional<std::string> Backward();

  // Set the position to read last.
  void SetPositionLast();
  // Add a observer to retrieve file change events.
  void AddObserver(Observer* obs);
  // Remove a observer to retrieve file change events.
  void RemoveObserver(Observer* obs);

  // Retrieve the current position in bytes.
  off_t position() const { return pos_; }

 private:
  void ReloadRotatedFile();
  void Remap();
  void OnFileContentMaybeChanged() override;
  void OnFileNameMaybeChanged() override;

  std::string GetString(off_t offset, size_t length) const;

  // Information about the target file. These field are initialized by
  // OpenFile() for either FILE or FILE_FOLLOW.
  base::File file_;
  base::FilePath file_path_;
  ino_t file_inode_ = 0;
  std::unique_ptr<base::MemoryMappedFile> mmap_;

  // This is initialized by OpenFile() for FILE_FOLLOW backend.
  FileChangeWatcher* file_change_watcher_ = nullptr;

  const uint8_t* buffer_ = nullptr;
  uint64_t buffer_size_ = 0;
  const Backend backend_mode_;
  bool rotated_ = false;

  // Position must be between [0, buffer_size_]. |buffer_[pos]| might be
  // invalid.
  off_t pos_ = 0;

  base::ObserverList<Observer> observers_;

  DISALLOW_COPY_AND_ASSIGN(LogLineReader);
};

}  // namespace croslog

#endif  // CROSLOG_LOG_LINE_READER_H_

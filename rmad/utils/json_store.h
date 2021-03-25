// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_JSON_STORE_H_
#define RMAD_UTILS_JSON_STORE_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <base/values.h>

namespace rmad {

// A class to store a JSON dictionary and keep in sync with a file.
class JsonStore : public base::RefCounted<JsonStore> {
 public:
  enum ReadError {
    READ_ERROR_NONE = 0,
    READ_ERROR_JSON_PARSE = 1,
    READ_ERROR_JSON_TYPE = 2,
    READ_ERROR_FILE_ACCESS_DENIED = 3,
    READ_ERROR_FILE_OTHER = 4,
    READ_ERROR_FILE_LOCKED = 5,
    READ_ERROR_NO_SUCH_FILE = 6,
    READ_ERROR_MAX_ENUM
  };

  explicit JsonStore(const base::FilePath& file_path);

  // Set a (key, value) pair to the dictionary. Return true if there's no
  // update or the updated data is successfully written to the file, false if
  // the update cannot be written to the file.
  bool SetValue(const std::string& key, base::Value&& value);

  // Get the value associated to the key, and assign its const pointer to
  // `result`. If the key is not found, `result` is not modified by the
  // function. Return true if the key is found in the dictionary, false if the
  // key is not found.
  bool GetValue(const std::string& key, const base::Value** result) const;

  // Get the value associated to the key, and copy to `result`. If the key is
  // not found, `result` is not modified by the function. Return true if the key
  // is found in the dictionary, false if the key is not found.
  bool GetValue(const std::string& key, base::Value* result) const;

  // Get the complete copy of the dictionary.
  base::Value GetValues() const;

  // Get read status of the file.
  ReadError GetReadError() const { return read_error_; }

  // Return true if the file cannot be written, such as access denied, or the
  // file already exists but contains invalid JSON format.
  bool ReadOnly() const { return read_only_; }

 private:
  // Hide the destructor so we don't accidentally delete this while there are
  // references to it.
  friend class base::RefCounted<JsonStore>;
  ~JsonStore() = default;

  // Read result returned from internal read tasks.
  struct ReadResult;

  void InitFromFile();
  std::unique_ptr<JsonStore::ReadResult> ReadFromFile();
  bool WriteToFile();

  const base::FilePath file_path_;
  base::Value data_;
  ReadError read_error_;
  bool read_only_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_JSON_STORE_H_

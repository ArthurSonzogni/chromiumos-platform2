// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_JSON_STORE_H_
#define RMAD_UTILS_JSON_STORE_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

  // Initialize from the file.
  bool InitFromFile();

  // Set a (key, value) pair to the dictionary. Return true if there's no
  // update or the updated data is successfully written to the file, false if
  // the update cannot be written to the file.
  bool SetValue(const std::string& key, base::Value&& value);

  // Set a (key, value) pair to the dictionary for types supported by
  // base::Value (bool, int, double, string), or nested vector of these types.
  // Return true if there's no update or the updated data is successfully
  // written to the file, false if the update cannot be written to the file.
  template <typename T>
  bool SetValue(const std::string& key, const T& value) {
    return SetValue(key, ConvertToValue(value));
  }

  // Get the value associated to the key, and copy to `value` for types
  // supported by base::Value.
  // If value is null then just the existence and type of the key value is
  // checked.
  // If the key is not found, `value` is not modified by the function. Return
  // true if the key is found in the dictionary, false if the key is not found.
  template <typename T>
  bool GetValue(const std::string& key, T* result) const {
    DCHECK(data_.is_dict());
    return GetValueInternal(data_.FindKey(key), result);
  }

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

  // Clear the dictionary. Return true on success, false if failed to write to
  // the file.
  bool Clear();

  // Clear the dictionary and delete the file that stores the data in storage.
  // Return true on success, false if failed to clear or delete the file.
  bool ClearAndDeleteFile();

  // Get read status of the file.
  ReadError GetReadError() const { return read_error_; }

  // Return true if the file existed when read was attempted.
  bool Exists() const { return read_error_ != READ_ERROR_NO_SUCH_FILE; }

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

  // Convert the input type to base::Value. The input type should be supported
  // by base::Value (bool, int, double, string).
  template <typename T>
  base::Value ConvertToValue(const T& value) {
    return base::Value(value);
  }

  // Convert a vector to base::Value. The vector type should be supported
  // by base::Value (bool, int, double, string) or vector/map of these types.
  template <typename T>
  base::Value ConvertToValue(const std::vector<T>& values) {
    base::Value list(base::Value::Type::LIST);
    for (const auto& value : values) {
      list.Append(ConvertToValue(value));
    }
    return list;
  }

  // Convert a map to base::Value. The value type should be supported by
  // base::Value (bool, int, double, string) or vector/map of these types.
  // TODO(chenghan): Support more types, e.g. unordered_map.
  template <typename T>
  base::Value ConvertToValue(const std::map<std::string, T>& values) {
    base::Value dict(base::Value::Type::DICTIONARY);
    for (const auto& [key, value] : values) {
      dict.SetKey(key, ConvertToValue(value));
    }
    return dict;
  }

  static bool GetValueInternal(const base::Value* data, bool* result);
  static bool GetValueInternal(const base::Value* data, int* result);
  static bool GetValueInternal(const base::Value* data, uint64_t* result);
  static bool GetValueInternal(const base::Value* data, double* result);
  static bool GetValueInternal(const base::Value* data, std::string* result);

  template <typename T>
  static bool GetValueInternal(const base::Value* data,
                               std::vector<T>* result) {
    if (!data || !data->is_list()) {
      return false;
    }
    std::vector<T> r;
    for (const auto& child_data : data->GetList()) {
      if (T child_result; GetValueInternal(&child_data, &child_result)) {
        r.push_back(child_result);
      } else {
        return false;
      }
    }
    if (result) {
      *result = std::move(r);
    }
    return true;
  }

  template <typename T>
  static bool GetValueInternal(const base::Value* data,
                               std::map<std::string, T>* result) {
    if (!data || !data->is_dict()) {
      return false;
    }
    std::map<std::string, T> r;
    for (const auto& [key, child_data] : data->DictItems()) {
      if (T child_result; GetValueInternal(&child_data, &child_result)) {
        r.insert({key, child_result});
      } else {
        return false;
      }
    }
    if (result) {
      *result = std::move(r);
    }
    return true;
  }

  std::unique_ptr<JsonStore::ReadResult> ReadFromFile();
  // This function is guarded by |read_only_|, but can be overridden by |force|
  // argument.
  bool WriteToFile(bool force = false);

  const base::FilePath file_path_;
  base::Value data_;
  ReadError read_error_;
  bool read_only_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_JSON_STORE_H_

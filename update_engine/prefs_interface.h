// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_PLATFORM_UPDATE_ENGINE_PREFS_INTERFACE_H__
#define CHROMEOS_PLATFORM_UPDATE_ENGINE_PREFS_INTERFACE_H__

#include <string>

namespace chromeos_update_engine {

// The prefs interface allows access to a persistent preferences
// store. The two reasons for providing this as an interface are
// testing as well as easier switching to a new implementation in the
// future, if necessary.

class PrefsInterface {
 public:
  // Gets a string |value| associated with |key|. Returns true on
  // success, false on failure (including when the |key| is not
  // present in the store).
  virtual bool GetString(const std::string& key, std::string* value) = 0;

  // Associates |key| with a string |value|. Returns true on success,
  // false otherwise.
  virtual bool SetString(const std::string& key, const std::string& value) = 0;

  // Gets an int64 |value| associated with |key|. Returns true on
  // success, false on failure (including when the |key| is not
  // present in the store).
  virtual bool GetInt64(const std::string& key, int64_t* value) = 0;

  // Associates |key| with an int64 |value|. Returns true on success,
  // false otherwise.
  virtual bool SetInt64(const std::string& key, const int64_t value) = 0;

  // Gets a boolean |value| associated with |key|. Returns true on
  // success, false on failure (including when the |key| is not
  // present in the store).
  virtual bool GetBoolean(const std::string& key, bool* value) = 0;

  // Associates |key| with a boolean |value|. Returns true on success,
  // false otherwise.
  virtual bool SetBoolean(const std::string& key, const bool value) = 0;

  // Returns true if the setting exists (i.e. a file with the given key
  // exists in the prefs directory)
  virtual bool Exists(const std::string& key) = 0;

  // Returns true if successfully deleted the file corresponding to
  // this key. Calling with non-existent keys does nothing.
  virtual bool Delete(const std::string& key) = 0;

  virtual ~PrefsInterface() {}
};

}  // namespace chromeos_update_engine

#endif  // CHROMEOS_PLATFORM_UPDATE_ENGINE_PREFS_INTERFACE_H__

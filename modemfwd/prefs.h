// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_PREFS_H_
#define MODEMFWD_PREFS_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>

namespace modemfwd {

// |Prefs| class can be used to persist key value pairs to disk.
class Prefs {
 public:
  // Initializes prefs with root as |prefs_root|.
  explicit Prefs(const base::FilePath& prefs_root);

  // The factory function to create prefs.
  static std::unique_ptr<Prefs> CreatePrefs(base::FilePath root_path);

  // The factory function to create prefs using another prefs's dir as the root
  // dir of the new prefs.
  static std::unique_ptr<Prefs> CreatePrefs(const Prefs& parent,
                                            const std::string& sub_pref);

  // Sets the given |value| for |key|, creating the |key| if it did not exist.
  bool SetKey(const std::string& key, const std::string& value);

  // Gets the given |key|'s value into |value|. Returns false if |key| did not
  // exist.
  bool GetKey(const std::string& key, std::string* value);

  // Reads the given |key|'s value, and checks that |value| matches it.
  // Returns false if |key| did not exist.
  bool KeyValueMatches(const std::string& key, const std::string& value);

  // Creates the given |key| with empty value.
  bool Create(const std::string& key);

  // Returns true if the |key| exists.
  bool Exists(const std::string& key);

  // Returns the root path of the pref directory.
  const base::FilePath& GetPrefRootPath() const { return prefs_root_; }

 private:
  base::FilePath GetKeyPrefPath(const std::string& key);

  base::FilePath prefs_root_;
};

}  // namespace modemfwd

#endif  // MODEMFWD_PREFS_H_

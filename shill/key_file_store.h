// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_KEY_FILE_STORE_H_
#define SHILL_KEY_FILE_STORE_H_

#include <set>
#include <string>
#include <vector>

#include <glib.h>  // Can't forward-declare GKeyFile due to typedef.
#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/crypto_provider.h"
#include "shill/store_interface.h"

namespace shill {

// A key file store implementation of the store interface. See
// http://www.gtk.org/api/2.6/glib/glib-Key-value-file-parser.html for details
// of the key file format.
class KeyFileStore : public StoreInterface {
 public:
  explicit KeyFileStore(const base::FilePath& path);
  ~KeyFileStore() override;

  // Inherited from StoreInterface.
  bool IsNonEmpty() const override;
  bool Open() override;
  bool Close() override;
  bool Flush() override;
  bool MarkAsCorrupted() override;
  std::set<std::string> GetGroups() const override;
  std::set<std::string> GetGroupsWithKey(const std::string& key) const override;
  std::set<std::string> GetGroupsWithProperties(
      const KeyValueStore& properties) const override;
  bool ContainsGroup(const std::string& group) const override;
  bool DeleteKey(const std::string& group, const std::string& key) override;
  bool DeleteGroup(const std::string& group) override;
  bool SetHeader(const std::string& header) override;
  bool GetString(const std::string& group,
                 const std::string& key,
                 std::string* value) const override;
  bool SetString(const std::string& group,
                 const std::string& key,
                 const std::string& value) override;
  bool GetBool(const std::string& group,
               const std::string& key,
               bool* value) const override;
  bool SetBool(const std::string& group,
               const std::string& key,
               bool value) override;
  bool GetInt(const std::string& group,
              const std::string& key,
              int* value) const override;
  bool SetInt(const std::string& group,
              const std::string& key,
              int value) override;
  bool GetUint64(const std::string& group,
                 const std::string& key,
                 uint64_t* value) const override;
  bool SetUint64(const std::string& group,
                 const std::string& key,
                 uint64_t value) override;
  bool GetStringList(const std::string& group,
                     const std::string& key,
                     std::vector<std::string>* value) const override;
  bool SetStringList(const std::string& group,
                     const std::string& key,
                     const std::vector<std::string>& value) override;
  bool GetCryptedString(const std::string& group,
                        const std::string& key,
                        std::string* value) override;
  bool SetCryptedString(const std::string& group,
                        const std::string& key,
                        const std::string& value) override;

 private:
  friend class KeyFileStoreTest;  // For |path_|.
  FRIEND_TEST(KeyFileStoreTest, OpenClose);
  FRIEND_TEST(KeyFileStoreTest, OpenFail);

  static const char kCorruptSuffix[];

  void ReleaseKeyFile();
  bool DoesGroupMatchProperties(const std::string& group,
                                const KeyValueStore& properties) const;

  CryptoProvider crypto_;
  GKeyFile* key_file_;
  base::FilePath path_;

  DISALLOW_COPY_AND_ASSIGN(KeyFileStore);
};

}  // namespace shill

#endif  // SHILL_KEY_FILE_STORE_H_

// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_STORE_FAKE_STORE_H_
#define SHILL_STORE_FAKE_STORE_H_

#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <brillo/variant_dictionary.h>

#include "shill/store/key_value_store.h"
#include "shill/store/store_interface.h"

namespace shill {

// A Fake implementation of StoreInterface. Useful when a unit test
// for another class ("FooClass") a) does not need to FooClass's use
// of StoreInterface, and b) the FooClass test needs a functional
// store.
class FakeStore : public StoreInterface {
 public:
  FakeStore();
  FakeStore(const FakeStore&) = delete;
  FakeStore& operator=(const FakeStore&) = delete;

  // Inherited from StoreInterface.
  bool IsEmpty() const override;
  bool Open() override;
  bool Close() override;
  bool Flush() override;
  bool MarkAsCorrupted() override;
  std::set<std::string> GetGroups() const override;
  std::set<std::string> GetGroupsWithKey(std::string_view key) const override;
  std::set<std::string> GetGroupsWithProperties(
      const KeyValueStore& properties) const override;
  bool ContainsGroup(std::string_view group) const override;
  bool DeleteKey(std::string_view group, std::string_view key) override;
  bool DeleteGroup(std::string_view group) override;
  bool SetHeader(std::string_view header) override;
  bool GetString(std::string_view group,
                 std::string_view key,
                 std::string* value) const override;
  bool SetString(std::string_view group,
                 std::string_view key,
                 std::string_view value) override;
  bool GetBool(std::string_view group,
               std::string_view key,
               bool* value) const override;
  bool SetBool(std::string_view group,
               std::string_view key,
               bool value) override;
  bool GetInt(std::string_view group,
              std::string_view key,
              int* value) const override;
  bool SetInt(std::string_view group, std::string_view key, int value) override;
  bool GetUint64(std::string_view group,
                 std::string_view key,
                 uint64_t* value) const override;
  bool SetUint64(std::string_view group,
                 std::string_view key,
                 uint64_t value) override;
  bool GetInt64(std::string_view group,
                std::string_view key,
                int64_t* value) const override;
  bool SetInt64(std::string_view group,
                std::string_view key,
                int64_t value) override;
  bool GetStringList(std::string_view group,
                     std::string_view key,
                     std::vector<std::string>* value) const override;
  bool SetStringList(std::string_view group,
                     std::string_view key,
                     const std::vector<std::string>& value) override;
  bool GetCryptedString(std::string_view group,
                        std::string_view deprecated_key,
                        std::string_view plaintext_key,
                        std::string* value) const override;
  bool SetCryptedString(std::string_view group,
                        std::string_view deprecated_key,
                        std::string_view plaintext_key,
                        std::string_view value) override;
  bool GetStringmaps(
      std::string_view group,
      std::string_view key,
      std::vector<std::map<std::string, std::string>>* value) const override;
  bool SetStringmaps(
      std::string_view group,
      std::string_view key,
      const std::vector<std::map<std::string, std::string>>& value) override;
  bool GetUint64List(std::string_view group,
                     std::string_view key,
                     std::vector<uint64_t>* value) const override;
  bool SetUint64List(std::string_view group,
                     std::string_view key,
                     const std::vector<uint64_t>& value) override;
  bool PKCS11SetString(std::string_view group,
                       std::string_view key,
                       std::string_view value) override;
  bool PKCS11GetString(std::string_view group,
                       std::string_view key,
                       std::string* value) const override;
  bool PKCS11DeleteGroup(std::string_view group) override;

  void set_writes_fail(bool writes_fail) { writes_fail_ = writes_fail; }

 private:
  template <typename T>
  bool ReadSetting(std::string_view group, std::string_view key, T* out) const;
  template <typename T>
  bool WriteSetting(std::string_view group,
                    std::string_view key,
                    const T& new_value);

  std::map<std::string, brillo::VariantDictionary, std::less<>>
      group_name_to_settings_;
  std::map<std::string,
           std::map<std::string, std::string, std::less<>>,
           std::less<>>
      pkcs11_strings_;
  bool writes_fail_ = false;
};

}  // namespace shill

#endif  // SHILL_STORE_FAKE_STORE_H_

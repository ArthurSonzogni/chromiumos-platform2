// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_STORE_STUB_STORAGE_H_
#define SHILL_STORE_STUB_STORAGE_H_

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "shill/store/store_interface.h"

namespace shill {

// A stub implementation of StoreInterface.
class StubStorage : public StoreInterface {
 public:
  ~StubStorage() override = default;

  bool IsEmpty() const override { return true; }
  bool Open() override { return false; }
  bool Close() override { return false; }
  bool Flush() override { return false; }
  bool MarkAsCorrupted() override { return false; }
  std::set<std::string> GetGroups() const override { return {}; }
  std::set<std::string> GetGroupsWithKey(std::string_view key) const override {
    return {};
  }
  std::set<std::string> GetGroupsWithProperties(
      const KeyValueStore& properties) const override {
    return {};
  }
  bool ContainsGroup(std::string_view group) const override { return false; }
  bool DeleteKey(std::string_view group, std::string_view key) override {
    return false;
  }
  bool DeleteGroup(std::string_view group) override { return false; }
  bool SetHeader(std::string_view header) override { return false; }
  bool GetString(std::string_view group,
                 std::string_view key,
                 std::string* value) const override {
    return false;
  }
  bool SetString(std::string_view group,
                 std::string_view key,
                 std::string_view value) override {
    return false;
  }
  bool GetBool(std::string_view group,
               std::string_view key,
               bool* value) const override {
    return false;
  }
  bool SetBool(std::string_view group,
               std::string_view key,
               bool value) override {
    return false;
  }
  bool GetInt(std::string_view group,
              std::string_view key,
              int* value) const override {
    return false;
  }
  bool SetInt(std::string_view group,
              std::string_view key,
              int value) override {
    return false;
  }
  bool GetUint64(std::string_view group,
                 std::string_view key,
                 uint64_t* value) const override {
    return false;
  }
  bool SetUint64(std::string_view group,
                 std::string_view key,
                 uint64_t value) override {
    return false;
  }
  bool GetInt64(std::string_view group,
                std::string_view key,
                int64_t* value) const override {
    return false;
  }
  bool SetInt64(std::string_view group,
                std::string_view key,
                int64_t value) override {
    return false;
  }
  bool GetStringList(std::string_view group,
                     std::string_view key,
                     std::vector<std::string>* value) const override {
    return false;
  }
  bool SetStringList(std::string_view group,
                     std::string_view key,
                     const std::vector<std::string>& value) override {
    return false;
  }
  bool GetCryptedString(std::string_view group,
                        std::string_view deprecated_key,
                        std::string_view plaintext_key,
                        std::string* value) const override {
    return false;
  }
  bool SetCryptedString(std::string_view group,
                        std::string_view deprecated_key,
                        std::string_view plaintext_key,
                        std::string_view value) override {
    return false;
  }
  bool GetStringmaps(
      std::string_view group,
      std::string_view key,
      std::vector<std::map<std::string, std::string>>* value) const override {
    return false;
  }
  bool SetStringmaps(
      std::string_view group,
      std::string_view key,
      const std::vector<std::map<std::string, std::string>>& value) override {
    return false;
  }
  bool GetUint64List(std::string_view group,
                     std::string_view key,
                     std::vector<uint64_t>* value) const override {
    return false;
  }
  bool SetUint64List(std::string_view group,
                     std::string_view key,
                     const std::vector<uint64_t>& value) override {
    return false;
  }
  bool PKCS11SetString(std::string_view group,
                       std::string_view key,
                       std::string_view value) override {
    return false;
  }
  bool PKCS11GetString(std::string_view group,
                       std::string_view key,
                       std::string* value) const override {
    return false;
  }
  bool PKCS11DeleteGroup(std::string_view group) override { return false; }
};

}  // namespace shill

#endif  // SHILL_STORE_STUB_STORAGE_H_

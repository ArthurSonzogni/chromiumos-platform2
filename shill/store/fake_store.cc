// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/store/fake_store.h"

#include <set>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include "shill/logging.h"

#include <base/logging.h>

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kStorage;
}  // namespace Logging

namespace {

bool DoesGroupContainProperties(
    const brillo::VariantDictionary& group,
    const brillo::VariantDictionary& required_properties) {
  for (const auto& required_property_name_and_value : required_properties) {
    const auto& required_key = required_property_name_and_value.first;
    const auto& required_value = required_property_name_and_value.second;
    const auto& group_it = group.find(required_key);
    if (group_it == group.end() || group_it->second != required_value) {
      return false;
    }
  }
  return true;
}

}  // namespace

FakeStore::FakeStore() = default;

bool FakeStore::IsEmpty() const {
  // For now, the choice for return value is arbitrary. Revisit if we
  // find tests depend on this behaving correctly. (i.e., if any tests
  // require this to return true after a Close().)
  return true;
}

bool FakeStore::Open() {
  return true;
}

bool FakeStore::Close() {
  return true;
}

bool FakeStore::Flush() {
  return true;
}

bool FakeStore::MarkAsCorrupted() {
  return true;
}

std::set<std::string> FakeStore::GetGroups() const {
  std::set<std::string> matching_groups;
  for (const auto& group_name_and_settings : group_name_to_settings_) {
    matching_groups.insert(group_name_and_settings.first);
  }
  return matching_groups;
}

// Returns a set so that caller can easily test whether a particular group
// is contained within this collection.
std::set<std::string> FakeStore::GetGroupsWithKey(std::string_view key) const {
  std::set<std::string> matching_groups;
  // iterate over groups, find ones with matching key
  for (const auto& group_name_and_settings : group_name_to_settings_) {
    const auto& group_name = group_name_and_settings.first;
    const auto& group_settings = group_name_and_settings.second;
    if (group_settings.find(key) != group_settings.end()) {
      matching_groups.insert(group_name);
    }
  }
  return matching_groups;
}

std::set<std::string> FakeStore::GetGroupsWithProperties(
    const KeyValueStore& properties) const {
  std::set<std::string> matching_groups;
  const brillo::VariantDictionary& properties_dict(properties.properties());
  for (const auto& group_name_and_settings : group_name_to_settings_) {
    const auto& group_name = group_name_and_settings.first;
    const auto& group_settings = group_name_and_settings.second;
    if (DoesGroupContainProperties(group_settings, properties_dict)) {
      matching_groups.insert(group_name);
    }
  }
  return matching_groups;
}

bool FakeStore::ContainsGroup(std::string_view group) const {
  const auto& it = group_name_to_settings_.find(group);
  return it != group_name_to_settings_.end();
}

bool FakeStore::DeleteKey(std::string_view group, std::string_view key) {
  const auto& group_name_and_settings = group_name_to_settings_.find(group);
  if (group_name_and_settings == group_name_to_settings_.end()) {
    LOG(ERROR) << "Could not find group |" << group << "|.";
    return false;
  }

  auto& group_settings = group_name_and_settings->second;
  auto property_it = group_settings.find(key);
  if (property_it != group_settings.end()) {
    group_settings.erase(property_it);
  }

  return true;
}

bool FakeStore::DeleteGroup(std::string_view group) {
  auto group_name_and_settings = group_name_to_settings_.find(group);
  if (group_name_and_settings != group_name_to_settings_.end()) {
    group_name_to_settings_.erase(group_name_and_settings);
  }
  return true;
}

bool FakeStore::SetHeader(std::string_view header) {
  return true;
}

bool FakeStore::GetString(std::string_view group,
                          std::string_view key,
                          std::string* value) const {
  return ReadSetting(group, key, value);
}

bool FakeStore::SetString(std::string_view group,
                          std::string_view key,
                          std::string_view value) {
  return WriteSetting(group, key, std::string(value));
}

bool FakeStore::GetBool(std::string_view group,
                        std::string_view key,
                        bool* value) const {
  return ReadSetting(group, key, value);
}

bool FakeStore::SetBool(std::string_view group,
                        std::string_view key,
                        bool value) {
  return WriteSetting(group, key, value);
}

bool FakeStore::GetInt(std::string_view group,
                       std::string_view key,
                       int* value) const {
  return ReadSetting(group, key, value);
}

bool FakeStore::SetInt(std::string_view group,
                       std::string_view key,
                       int value) {
  return WriteSetting(group, key, value);
}

bool FakeStore::GetUint64(std::string_view group,
                          std::string_view key,
                          uint64_t* value) const {
  return ReadSetting(group, key, value);
}

bool FakeStore::SetUint64(std::string_view group,
                          std::string_view key,
                          uint64_t value) {
  return WriteSetting(group, key, value);
}

bool FakeStore::GetInt64(std::string_view group,
                         std::string_view key,
                         int64_t* value) const {
  return ReadSetting(group, key, value);
}

bool FakeStore::SetInt64(std::string_view group,
                         std::string_view key,
                         int64_t value) {
  return WriteSetting(group, key, value);
}
bool FakeStore::GetStringList(std::string_view group,
                              std::string_view key,
                              std::vector<std::string>* value) const {
  return ReadSetting(group, key, value);
}

bool FakeStore::SetStringList(std::string_view group,
                              std::string_view key,
                              const std::vector<std::string>& value) {
  return WriteSetting(group, key, value);
}

bool FakeStore::GetCryptedString(std::string_view group,
                                 std::string_view deprecated_key,
                                 std::string_view plaintext_key,
                                 std::string* value) const {
  return GetString(group, plaintext_key, value);
}

bool FakeStore::SetCryptedString(std::string_view group,
                                 std::string_view deprecated_key,
                                 std::string_view plaintext_key,
                                 std::string_view value) {
  return SetString(group, plaintext_key, value);
}

bool FakeStore::GetStringmaps(
    std::string_view group,
    std::string_view key,
    std::vector<std::map<std::string, std::string>>* value) const {
  return ReadSetting(group, key, value);
}

bool FakeStore::SetStringmaps(
    std::string_view group,
    std::string_view key,
    const std::vector<std::map<std::string, std::string>>& value) {
  return WriteSetting(group, key, value);
}

bool FakeStore::GetUint64List(std::string_view group,
                              std::string_view key,
                              std::vector<uint64_t>* value) const {
  return ReadSetting(group, key, value);
}

bool FakeStore::SetUint64List(std::string_view group,
                              std::string_view key,
                              const std::vector<uint64_t>& value) {
  return WriteSetting(group, key, value);
}

bool FakeStore::PKCS11SetString(std::string_view group,
                                std::string_view key,
                                std::string_view value) {
  const std::string group_str(group.data(), group.size());
  const std::string key_str(key.data(), key.size());
  const std::string value_str(value.data(), value.size());
  pkcs11_strings_[group_str][key_str] = value_str;
  return true;
}

bool FakeStore::PKCS11GetString(std::string_view group,
                                std::string_view key,
                                std::string* value) const {
  const auto group_it = pkcs11_strings_.find(group);
  if (group_it == pkcs11_strings_.end()) {
    return false;
  }
  const auto& group_submap = group_it->second;
  const auto kv_it = group_submap.find(key);
  if (kv_it == group_submap.end()) {
    return false;
  }
  *value = kv_it->second;
  return true;
}

bool FakeStore::PKCS11DeleteGroup(std::string_view group) {
  const std::string group_str(group.data(), group.size());
  pkcs11_strings_.erase(group_str);
  return true;
}

// Private methods.
template <typename T>
bool FakeStore::ReadSetting(std::string_view group,
                            std::string_view key,
                            T* out) const {
  const auto& group_name_and_settings = group_name_to_settings_.find(group);
  if (group_name_and_settings == group_name_to_settings_.end()) {
    SLOG(10) << "Could not find group |" << group << "|.";
    return false;
  }

  const auto& group_settings = group_name_and_settings->second;
  const auto& property_name_and_value = group_settings.find(key);
  if (property_name_and_value == group_settings.end()) {
    SLOG(10) << "Could not find property |" << key << "|.";
    return false;
  }

  if (!property_name_and_value->second.IsTypeCompatible<T>()) {
    // We assume that the reader and the writer agree on the exact
    // type. So we do not allow implicit conversion.
    LOG(ERROR) << "Can not read |" << brillo::GetUndecoratedTypeName<T>()
               << "| from |"
               << property_name_and_value->second.GetUndecoratedTypeName()
               << "|.";
    return false;
  }

  if (out) {
    return property_name_and_value->second.GetValue(out);
  } else {
    return true;
  }
}

template <typename T>
bool FakeStore::WriteSetting(std::string_view group,
                             std::string_view key,
                             const T& new_value) {
  if (writes_fail_) {
    return false;
  }
  auto group_name_and_settings = group_name_to_settings_.find(group);
  if (group_name_and_settings == group_name_to_settings_.end()) {
    group_name_to_settings_[std::string(group)][std::string(key)] = new_value;
    return true;
  }

  auto& group_settings = group_name_and_settings->second;
  auto property_name_and_value = group_settings.find(key);
  if (property_name_and_value == group_settings.end()) {
    group_settings.insert({std::string(key), new_value});
    return true;
  }

  if (!property_name_and_value->second.IsTypeCompatible<T>()) {
    SLOG(10) << "New type |" << brillo::GetUndecoratedTypeName<T>()
             << "| differs from current type |"
             << property_name_and_value->second.GetUndecoratedTypeName()
             << "|.";
    return false;
  } else {
    property_name_and_value->second = new_value;
    return true;
  }
}

}  // namespace shill

// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/store/property_store.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/strcat.h>
#include <dbus/object_path.h>

#include "shill/error.h"
#include "shill/logging.h"
#include "shill/store/property_accessor.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kProperty;
}  // namespace Logging

namespace {
// Helper function encapsulating the property access pattern used for
// implementing PropertyStore::GetProperties()
template <class V>
void CopyReadableProperties(
    brillo::VariantDictionary* out,
    const std::map<std::string,
                   std::unique_ptr<AccessorInterface<V>>,
                   std::less<>>& properties) {
  for (const auto& [key, value] : properties) {
    Error error;
    V v = value.get()->Get(&error);
    if (error.IsSuccess()) {
      (*out)[key] = brillo::Any(v);
    }
  }
}
}  // namespace

PropertyStore::PropertyStore() = default;

PropertyStore::PropertyStore(PropertyChangeCallback on_property_changed)
    : property_changed_callback_(on_property_changed) {}

PropertyStore::~PropertyStore() = default;

bool PropertyStore::Contains(std::string_view prop) const {
  return (base::Contains(bool_properties_, prop) ||
          base::Contains(int16_properties_, prop) ||
          base::Contains(int32_properties_, prop) ||
          base::Contains(key_value_store_properties_, prop) ||
          base::Contains(key_value_stores_properties_, prop) ||
          base::Contains(string_properties_, prop) ||
          base::Contains(stringmap_properties_, prop) ||
          base::Contains(stringmaps_properties_, prop) ||
          base::Contains(strings_properties_, prop) ||
          base::Contains(uint8_properties_, prop) ||
          base::Contains(bytearray_properties_, prop) ||
          base::Contains(uint16_properties_, prop) ||
          base::Contains(uint16s_properties_, prop) ||
          base::Contains(uint32_properties_, prop) ||
          base::Contains(uint64_properties_, prop) ||
          base::Contains(rpc_identifier_properties_, prop) ||
          base::Contains(rpc_identifiers_properties_, prop));
}

void PropertyStore::SetAnyProperty(std::string_view name,
                                   const brillo::Any& value,
                                   Error* error) {
  if (value.IsTypeCompatible<bool>()) {
    SetBoolProperty(name, value.Get<bool>(), error);
  } else if (value.IsTypeCompatible<uint8_t>()) {
    SetUint8Property(name, value.Get<uint8_t>(), error);
  } else if (value.IsTypeCompatible<int16_t>()) {
    SetInt16Property(name, value.Get<int16_t>(), error);
  } else if (value.IsTypeCompatible<int32_t>()) {
    SetInt32Property(name, value.Get<int32_t>(), error);
  } else if (value.IsTypeCompatible<dbus::ObjectPath>()) {
    SetRpcIdentifierProperty(name, value.Get<dbus::ObjectPath>(), error);
  } else if (value.IsTypeCompatible<std::string>()) {
    SetStringProperty(name, value.Get<std::string>(), error);
  } else if (value.IsTypeCompatible<Stringmap>()) {
    SetStringmapProperty(name, value.Get<Stringmap>(), error);
  } else if (value.IsTypeCompatible<Stringmaps>()) {
    SetStringmapsProperty(name, value.Get<Stringmaps>(), error);
  } else if (value.IsTypeCompatible<Strings>()) {
    SetStringsProperty(name, value.Get<Strings>(), error);
  } else if (value.IsTypeCompatible<ByteArray>()) {
    SetByteArrayProperty(name, value.Get<ByteArray>(), error);
  } else if (value.IsTypeCompatible<uint16_t>()) {
    SetUint16Property(name, value.Get<uint16_t>(), error);
  } else if (value.IsTypeCompatible<Uint16s>()) {
    SetUint16sProperty(name, value.Get<Uint16s>(), error);
  } else if (value.IsTypeCompatible<uint32_t>()) {
    SetUint32Property(name, value.Get<uint32_t>(), error);
  } else if (value.IsTypeCompatible<uint64_t>()) {
    SetUint64Property(name, value.Get<uint64_t>(), error);
  } else if (value.IsTypeCompatible<brillo::VariantDictionary>()) {
    KeyValueStore store = KeyValueStore::ConvertFromVariantDictionary(
        value.Get<brillo::VariantDictionary>());
    SetKeyValueStoreProperty(name, store, error);
  } else if (value.IsTypeCompatible<std::vector<brillo::VariantDictionary>>()) {
    KeyValueStores dicts;
    for (const auto& d : value.Get<std::vector<brillo::VariantDictionary>>()) {
      KeyValueStore store = KeyValueStore::ConvertFromVariantDictionary(d);
      dicts.push_back(store);
    }
    SetKeyValueStoresProperty(name, dicts, error);
  } else {
    NOTREACHED() << " unknown type: " << value.GetUndecoratedTypeName();
    error->Populate(Error::kInternalError);
  }
}

void PropertyStore::SetProperties(const brillo::VariantDictionary& in,
                                  Error* error) {
  for (const auto& kv : in) {
    SetAnyProperty(kv.first, kv.second, error);
  }
}

bool PropertyStore::GetProperties(brillo::VariantDictionary* out,
                                  Error* ignored) const {
  CopyReadableProperties(out, bool_properties_);
  CopyReadableProperties(out, int16_properties_);
  CopyReadableProperties(out, int32_properties_);
  CopyReadableProperties(out, rpc_identifier_properties_);
  CopyReadableProperties(out, rpc_identifiers_properties_);
  CopyReadableProperties(out, string_properties_);
  CopyReadableProperties(out, strings_properties_);
  CopyReadableProperties(out, stringmap_properties_);
  CopyReadableProperties(out, stringmaps_properties_);
  CopyReadableProperties(out, uint8_properties_);
  CopyReadableProperties(out, bytearray_properties_);
  CopyReadableProperties(out, uint16_properties_);
  CopyReadableProperties(out, uint16s_properties_);
  CopyReadableProperties(out, uint32_properties_);
  CopyReadableProperties(out, uint64_properties_);
  for (const auto& [key, value] : key_value_store_properties_) {
    Error error;
    auto v =
        KeyValueStore::ConvertToVariantDictionary(value.get()->Get(&error));
    if (error.IsSuccess()) {
      (*out)[key] = brillo::Any(v);
    }
  }
  for (const auto& [key, value] : key_value_stores_properties_) {
    Error error;
    std::vector<brillo::VariantDictionary> dicts;
    auto stores = value.get()->Get(&error);
    if (error.IsSuccess()) {
      for (const auto& store : stores) {
        dicts.push_back(KeyValueStore::ConvertToVariantDictionary(store));
      }
      (*out)[key] = dicts;
    }
  }
  return true;
}

bool PropertyStore::GetBoolProperty(std::string_view name,
                                    bool* value,
                                    Error* error) const {
  return GetProperty(name, value, error, bool_properties_, "a bool");
}

bool PropertyStore::GetInt16Property(std::string_view name,
                                     int16_t* value,
                                     Error* error) const {
  return GetProperty(name, value, error, int16_properties_, "an int16_t");
}

bool PropertyStore::GetInt32Property(std::string_view name,
                                     int32_t* value,
                                     Error* error) const {
  return GetProperty(name, value, error, int32_properties_, "an int32_t");
}

bool PropertyStore::GetKeyValueStoreProperty(std::string_view name,
                                             KeyValueStore* value,
                                             Error* error) const {
  return GetProperty(name, value, error, key_value_store_properties_,
                     "a key value store");
}

bool PropertyStore::GetKeyValueStoresProperty(std::string_view name,
                                              KeyValueStores* value,
                                              Error* error) const {
  return GetProperty(name, value, error, key_value_stores_properties_,
                     "a key value stores");
}

bool PropertyStore::GetRpcIdentifierProperty(std::string_view name,
                                             RpcIdentifier* value,
                                             Error* error) const {
  return GetProperty(name, value, error, rpc_identifier_properties_,
                     "an rpc_identifier");
}

bool PropertyStore::GetStringProperty(std::string_view name,
                                      std::string* value,
                                      Error* error) const {
  return GetProperty(name, value, error, string_properties_, "a string");
}

bool PropertyStore::GetStringmapProperty(std::string_view name,
                                         Stringmap* values,
                                         Error* error) const {
  return GetProperty(name, values, error, stringmap_properties_,
                     "a string map");
}

bool PropertyStore::GetStringmapsProperty(std::string_view name,
                                          Stringmaps* values,
                                          Error* error) const {
  return GetProperty(name, values, error, stringmaps_properties_,
                     "a string map list");
}

bool PropertyStore::GetStringsProperty(std::string_view name,
                                       Strings* values,
                                       Error* error) const {
  return GetProperty(name, values, error, strings_properties_, "a string list");
}

bool PropertyStore::GetUint8Property(std::string_view name,
                                     uint8_t* value,
                                     Error* error) const {
  return GetProperty(name, value, error, uint8_properties_, "a uint8_t");
}

bool PropertyStore::GetByteArrayProperty(std::string_view name,
                                         ByteArray* value,
                                         Error* error) const {
  return GetProperty(name, value, error, bytearray_properties_, "a byte array");
}

bool PropertyStore::GetUint16Property(std::string_view name,
                                      uint16_t* value,
                                      Error* error) const {
  return GetProperty(name, value, error, uint16_properties_, "a uint16_t");
}

bool PropertyStore::GetUint16sProperty(std::string_view name,
                                       Uint16s* value,
                                       Error* error) const {
  return GetProperty(name, value, error, uint16s_properties_,
                     "a uint16_t list");
}

bool PropertyStore::GetUint32Property(std::string_view name,
                                      uint32_t* value,
                                      Error* error) const {
  return GetProperty(name, value, error, uint32_properties_, "a uint32_t");
}

bool PropertyStore::GetUint64Property(std::string_view name,
                                      uint64_t* value,
                                      Error* error) const {
  return GetProperty(name, value, error, uint64_properties_, "a uint64_t");
}

void PropertyStore::SetBoolProperty(std::string_view name,
                                    bool value,
                                    Error* error) {
  SetProperty(name, value, error, &bool_properties_, "a bool");
}

void PropertyStore::SetInt16Property(std::string_view name,
                                     int16_t value,
                                     Error* error) {
  SetProperty(name, value, error, &int16_properties_, "an int16_t");
}

void PropertyStore::SetInt32Property(std::string_view name,
                                     int32_t value,
                                     Error* error) {
  SetProperty(name, value, error, &int32_properties_, "an int32_t.");
}

void PropertyStore::SetKeyValueStoreProperty(std::string_view name,
                                             const KeyValueStore& value,
                                             Error* error) {
  SetProperty(name, value, error, &key_value_store_properties_,
              "a key value store");
}

void PropertyStore::SetKeyValueStoresProperty(std::string_view name,
                                              const KeyValueStores& value,
                                              Error* error) {
  SetProperty(name, value, error, &key_value_stores_properties_,
              "a key value stores");
}

void PropertyStore::SetStringProperty(std::string_view name,
                                      const std::string& value,
                                      Error* error) {
  SetProperty(name, value, error, &string_properties_, "a string");
}

void PropertyStore::SetStringmapProperty(
    std::string_view name,
    const std::map<std::string, std::string>& values,
    Error* error) {
  SetProperty(name, values, error, &stringmap_properties_, "a string map");
}

void PropertyStore::SetStringmapsProperty(
    std::string_view name,
    const std::vector<std::map<std::string, std::string>>& values,
    Error* error) {
  SetProperty(name, values, error, &stringmaps_properties_, "a stringmaps");
}

void PropertyStore::SetStringsProperty(std::string_view name,
                                       const std::vector<std::string>& values,
                                       Error* error) {
  SetProperty(name, values, error, &strings_properties_, "a string list");
}

void PropertyStore::SetUint8Property(std::string_view name,
                                     uint8_t value,
                                     Error* error) {
  SetProperty(name, value, error, &uint8_properties_, "a uint8_t");
}

void PropertyStore::SetByteArrayProperty(std::string_view name,
                                         const ByteArray& value,
                                         Error* error) {
  SetProperty(name, value, error, &bytearray_properties_, "a byte array");
}

void PropertyStore::SetUint16Property(std::string_view name,
                                      uint16_t value,
                                      Error* error) {
  SetProperty(name, value, error, &uint16_properties_, "a uint16_t");
}

void PropertyStore::SetUint16sProperty(std::string_view name,
                                       const std::vector<uint16_t>& value,
                                       Error* error) {
  SetProperty(name, value, error, &uint16s_properties_, "a uint16_t list");
}

void PropertyStore::SetUint32Property(std::string_view name,
                                      uint32_t value,
                                      Error* error) {
  SetProperty(name, value, error, &uint32_properties_, "a uint32_t");
}

void PropertyStore::SetUint64Property(std::string_view name,
                                      uint64_t value,
                                      Error* error) {
  SetProperty(name, value, error, &uint64_properties_, "a uint64_t");
}

void PropertyStore::SetRpcIdentifierProperty(std::string_view name,
                                             const RpcIdentifier& value,
                                             Error* error) {
  SetProperty(name, value, error, &rpc_identifier_properties_,
              "an rpc_identifier");
}

namespace {
// Helper function used in ClearProperty(). Returns true if |name| is found in
// |property_map|.
template <class V>
bool TryClearProperty(std::string_view name, Error* error, V* property_map) {
  const auto it = property_map->find(name);
  if (it == property_map->end()) {
    return false;
  }
  it->second->Clear(error);
  return true;
}
}  // namespace

bool PropertyStore::ClearProperty(std::string_view name, Error* error) {
  SLOG(2) << "Clearing " << name << ".";

  if (!(TryClearProperty(name, error, &bool_properties_) ||
        TryClearProperty(name, error, &int16_properties_) ||
        TryClearProperty(name, error, &int32_properties_) ||
        TryClearProperty(name, error, &key_value_store_properties_) ||
        TryClearProperty(name, error, &key_value_stores_properties_) ||
        TryClearProperty(name, error, &string_properties_) ||
        TryClearProperty(name, error, &stringmap_properties_) ||
        TryClearProperty(name, error, &stringmaps_properties_) ||
        TryClearProperty(name, error, &strings_properties_) ||
        TryClearProperty(name, error, &uint8_properties_) ||
        TryClearProperty(name, error, &uint16_properties_) ||
        TryClearProperty(name, error, &uint16s_properties_) ||
        TryClearProperty(name, error, &uint32_properties_) ||
        TryClearProperty(name, error, &uint64_properties_) ||
        TryClearProperty(name, error, &rpc_identifier_properties_) ||
        TryClearProperty(name, error, &rpc_identifiers_properties_))) {
    error->Populate(Error::kInvalidProperty,
                    base::StrCat({"Property ", name, " does not exist."}));
  }
  if (error->IsSuccess()) {
    if (!property_changed_callback_.is_null()) {
      property_changed_callback_.Run(name);
    }
  }
  return error->IsSuccess();
}

void PropertyStore::RegisterBool(std::string_view name, bool* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  bool_properties_[std::string(name)].reset(new PropertyAccessor<bool>(prop));
}

void PropertyStore::RegisterConstBool(std::string_view name, const bool* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  bool_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<bool>(prop));
}

void PropertyStore::RegisterWriteOnlyBool(std::string_view name, bool* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  bool_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<bool>(prop));
}

void PropertyStore::RegisterInt16(std::string_view name, int16_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  int16_properties_[std::string(name)].reset(
      new PropertyAccessor<int16_t>(prop));
}

void PropertyStore::RegisterConstInt16(std::string_view name,
                                       const int16_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  int16_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<int16_t>(prop));
}

void PropertyStore::RegisterWriteOnlyInt16(std::string_view name,
                                           int16_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  int16_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<int16_t>(prop));
}
void PropertyStore::RegisterInt32(std::string_view name, int32_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  int32_properties_[std::string(name)].reset(
      new PropertyAccessor<int32_t>(prop));
}

void PropertyStore::RegisterConstInt32(std::string_view name,
                                       const int32_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  int32_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<int32_t>(prop));
}

void PropertyStore::RegisterWriteOnlyInt32(std::string_view name,
                                           int32_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  int32_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<int32_t>(prop));
}

void PropertyStore::RegisterUint64(std::string_view name, uint64_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint64_properties_[std::string(name)].reset(
      new PropertyAccessor<uint64_t>(prop));
}

void PropertyStore::RegisterString(std::string_view name, std::string* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  string_properties_[std::string(name)].reset(
      new PropertyAccessor<std::string>(prop));
}

void PropertyStore::RegisterConstString(std::string_view name,
                                        const std::string* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  string_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<std::string>(prop));
}

void PropertyStore::RegisterWriteOnlyString(std::string_view name,
                                            std::string* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  string_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<std::string>(prop));
}

void PropertyStore::RegisterStringmap(std::string_view name, Stringmap* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  stringmap_properties_[std::string(name)].reset(
      new PropertyAccessor<Stringmap>(prop));
}

void PropertyStore::RegisterConstStringmap(std::string_view name,
                                           const Stringmap* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  stringmap_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<Stringmap>(prop));
}

void PropertyStore::RegisterWriteOnlyStringmap(std::string_view name,
                                               Stringmap* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  stringmap_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<Stringmap>(prop));
}

void PropertyStore::RegisterStringmaps(std::string_view name,
                                       Stringmaps* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  stringmaps_properties_[std::string(name)].reset(
      new PropertyAccessor<Stringmaps>(prop));
}

void PropertyStore::RegisterConstStringmaps(std::string_view name,
                                            const Stringmaps* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  stringmaps_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<Stringmaps>(prop));
}

void PropertyStore::RegisterWriteOnlyStringmaps(std::string_view name,
                                                Stringmaps* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  stringmaps_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<Stringmaps>(prop));
}

void PropertyStore::RegisterStrings(std::string_view name, Strings* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  strings_properties_[std::string(name)].reset(
      new PropertyAccessor<Strings>(prop));
}

void PropertyStore::RegisterConstStrings(std::string_view name,
                                         const Strings* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  strings_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<Strings>(prop));
}

void PropertyStore::RegisterWriteOnlyStrings(std::string_view name,
                                             Strings* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  strings_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<Strings>(prop));
}

void PropertyStore::RegisterUint8(std::string_view name, uint8_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint8_properties_[std::string(name)].reset(
      new PropertyAccessor<uint8_t>(prop));
}

void PropertyStore::RegisterConstUint8(std::string_view name,
                                       const uint8_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint8_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<uint8_t>(prop));
}

void PropertyStore::RegisterWriteOnlyUint8(std::string_view name,
                                           uint8_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint8_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<uint8_t>(prop));
}

void PropertyStore::RegisterByteArray(std::string_view name, ByteArray* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  bytearray_properties_[std::string(name)].reset(
      new PropertyAccessor<ByteArray>(prop));
}

void PropertyStore::RegisterConstByteArray(std::string_view name,
                                           const ByteArray* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  bytearray_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<ByteArray>(prop));
}

void PropertyStore::RegisterWriteOnlyByteArray(std::string_view name,
                                               ByteArray* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  bytearray_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<ByteArray>(prop));
}

void PropertyStore::RegisterKeyValueStore(std::string_view name,
                                          KeyValueStore* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  key_value_store_properties_[std::string(name)].reset(
      new PropertyAccessor<KeyValueStore>(prop));
}

void PropertyStore::RegisterConstKeyValueStore(std::string_view name,
                                               const KeyValueStore* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  key_value_store_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<KeyValueStore>(prop));
}

void PropertyStore::RegisterKeyValueStores(std::string_view name,
                                           KeyValueStores* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  key_value_stores_properties_[std::string(name)].reset(
      new PropertyAccessor<KeyValueStores>(prop));
}

void PropertyStore::RegisterConstKeyValueStores(std::string_view name,
                                                const KeyValueStores* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  key_value_stores_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<KeyValueStores>(prop));
}

void PropertyStore::RegisterUint16(std::string_view name, uint16_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint16_properties_[std::string(name)].reset(
      new PropertyAccessor<uint16_t>(prop));
}

void PropertyStore::RegisterUint16s(std::string_view name, Uint16s* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint16s_properties_[std::string(name)].reset(
      new PropertyAccessor<Uint16s>(prop));
}

void PropertyStore::RegisterUint32(std::string_view name, uint32_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint32_properties_[std::string(name)].reset(
      new PropertyAccessor<uint32_t>(prop));
}

void PropertyStore::RegisterConstUint32(std::string_view name,
                                        const uint32_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint32_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<uint32_t>(prop));
}

void PropertyStore::RegisterConstUint16(std::string_view name,
                                        const uint16_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint16_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<uint16_t>(prop));
}

void PropertyStore::RegisterConstUint16s(std::string_view name,
                                         const Uint16s* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint16s_properties_[std::string(name)].reset(
      new ConstPropertyAccessor<Uint16s>(prop));
}

void PropertyStore::RegisterWriteOnlyUint16(std::string_view name,
                                            uint16_t* prop) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint16_properties_[std::string(name)].reset(
      new WriteOnlyPropertyAccessor<uint16_t>(prop));
}

void PropertyStore::RegisterDerivedBool(std::string_view name,
                                        BoolAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  bool_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedInt32(std::string_view name,
                                         Int32Accessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  int32_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedKeyValueStore(
    std::string_view name, KeyValueStoreAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  key_value_store_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedKeyValueStores(
    std::string_view name, KeyValueStoresAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  key_value_stores_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedRpcIdentifier(
    std::string_view name, RpcIdentifierAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  rpc_identifier_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedRpcIdentifiers(
    std::string_view name, RpcIdentifiersAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  rpc_identifiers_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedString(std::string_view name,
                                          StringAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  string_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedStrings(std::string_view name,
                                           StringsAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  strings_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedStringmap(std::string_view name,
                                             StringmapAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  stringmap_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedStringmaps(std::string_view name,
                                              StringmapsAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  stringmaps_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedUint16(std::string_view name,
                                          Uint16Accessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint16_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedUint32(std::string_view name,
                                          Uint32Accessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint32_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedUint64(std::string_view name,
                                          Uint64Accessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint64_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedUint16s(std::string_view name,
                                           Uint16sAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  uint16s_properties_[std::string(name)] = std::move(accessor);
}

void PropertyStore::RegisterDerivedByteArray(std::string_view name,
                                             ByteArrayAccessor accessor) {
  DCHECK(!Contains(name)) << "(Already registered " << name << ")";
  bytearray_properties_[std::string(name)] = std::move(accessor);
}

// private methods

template <class V>
bool PropertyStore::GetProperty(std::string_view name,
                                V* value,
                                Error* error,
                                const AccessorMap<V>& collection,
                                std::string_view value_type_english) const {
  SLOG(2) << "Getting " << name << " as " << value_type_english << ".";
  auto it = collection.find(name);
  if (it != collection.end()) {
    V val = it->second->Get(error);
    if (error->IsSuccess()) {
      *value = val;
    }
  } else {
    if (Contains(name)) {
      error->Populate(Error::kInvalidArguments,
                      base::StrCat({"Property ", name, " is not ",
                                    value_type_english, "."}));
    } else {
      error->Populate(Error::kInvalidProperty,
                      base::StrCat({"Property ", name, " does not exist."}));
    }
  }
  return error->IsSuccess();
}

template <class V>
bool PropertyStore::SetProperty(std::string_view name,
                                const V& value,
                                Error* error,
                                AccessorMap<V>* collection,
                                std::string_view value_type_english) {
  SLOG(2) << "Setting " << name << " as " << value_type_english << ".";
  if (const auto it = collection->find(name); it != collection->end()) {
    bool ret = it->second->Set(value, error);
    if (!ret) {
      return false;
    }
    if (!property_changed_callback_.is_null()) {
      property_changed_callback_.Run(name);
    }
    return true;
  }

  if (Contains(name)) {
    error->Populate(
        Error::kInvalidArguments,
        base::StrCat({"Property ", name, " is not ", value_type_english, "."}));
  } else {
    error->Populate(Error::kInvalidProperty,
                    base::StrCat({"Property ", name, " does not exist."}));
  }
  return false;
}

}  // namespace shill

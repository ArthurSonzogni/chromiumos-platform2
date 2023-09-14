// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_STORE_PROPERTY_STORE_H_
#define SHILL_STORE_PROPERTY_STORE_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/functional/callback.h>
#include <brillo/any.h>
#include <brillo/variant_dictionary.h>

#include "shill/store/accessor_interface.h"
#include "shill/store/key_value_store.h"

namespace shill {

class Error;

class PropertyStore {
 public:
  using PropertyChangeCallback =
      base::RepeatingCallback<void(std::string_view)>;
  PropertyStore();
  explicit PropertyStore(PropertyChangeCallback property_change_callback);
  PropertyStore(const PropertyStore&) = delete;
  PropertyStore& operator=(const PropertyStore&) = delete;

  ~PropertyStore();

  bool Contains(std::string_view property) const;

  // Setting properties using brillo::Any variant type.
  void SetAnyProperty(std::string_view name,
                      const brillo::Any& value,
                      Error* error);
  void SetProperties(const brillo::VariantDictionary& in, Error* error);

  // Retrieve all properties and store them in a brillo::VariantDictionary
  // (std::map<std::string, brillo::Any>).
  bool GetProperties(brillo::VariantDictionary* out, Error* error) const;

  // Methods to allow the getting of properties stored in the referenced
  // |store_| by name. Upon success, these methods return true and return the
  // property value in |value|. Upon failure, they return false and
  // leave |value| untouched.
  bool GetBoolProperty(std::string_view name, bool* value, Error* error) const;
  bool GetInt16Property(std::string_view name,
                        int16_t* value,
                        Error* error) const;
  bool GetInt32Property(std::string_view name,
                        int32_t* value,
                        Error* error) const;
  bool GetKeyValueStoreProperty(std::string_view name,
                                KeyValueStore* value,
                                Error* error) const;
  bool GetKeyValueStoresProperty(std::string_view name,
                                 KeyValueStores* value,
                                 Error* error) const;
  bool GetStringProperty(std::string_view name,
                         std::string* value,
                         Error* error) const;
  bool GetStringmapProperty(std::string_view name,
                            Stringmap* values,
                            Error* error) const;
  bool GetStringmapsProperty(std::string_view name,
                             Stringmaps* values,
                             Error* error) const;
  bool GetStringsProperty(std::string_view name,
                          Strings* values,
                          Error* error) const;
  bool GetUint8Property(std::string_view name,
                        uint8_t* value,
                        Error* error) const;
  bool GetByteArrayProperty(std::string_view name,
                            ByteArray* value,
                            Error* error) const;
  bool GetUint16Property(std::string_view name,
                         uint16_t* value,
                         Error* error) const;
  bool GetUint16sProperty(std::string_view name,
                          Uint16s* value,
                          Error* error) const;
  bool GetUint32Property(std::string_view name,
                         uint32_t* value,
                         Error* error) const;
  bool GetUint64Property(std::string_view name,
                         uint64_t* value,
                         Error* error) const;
  bool GetRpcIdentifierProperty(std::string_view name,
                                RpcIdentifier* value,
                                Error* error) const;

  // Methods to allow the setting, by name, of properties stored in this object.
  // The property names are declared in chromeos/dbus/service_constants.h,
  // so that they may be shared with libcros.
  // If the property is successfully changed, these methods leave |error|
  // untouched.
  // If the property is unchanged because it already has the desired value,
  // these methods leave |error| untouched.
  // If the property change fails, these methods update |error|. However,
  // updating |error| is skipped if |error| is NULL.
  void SetBoolProperty(std::string_view name, bool value, Error* error);

  void SetInt16Property(std::string_view name, int16_t value, Error* error);

  void SetInt32Property(std::string_view name, int32_t value, Error* error);

  void SetKeyValueStoreProperty(std::string_view name,
                                const KeyValueStore& value,
                                Error* error);

  void SetKeyValueStoresProperty(std::string_view name,
                                 const KeyValueStores& value,
                                 Error* error);

  void SetStringProperty(std::string_view name,
                         const std::string& value,
                         Error* error);

  void SetStringmapProperty(std::string_view name,
                            const std::map<std::string, std::string>& values,
                            Error* error);

  void SetStringmapsProperty(
      std::string_view name,
      const std::vector<std::map<std::string, std::string>>& values,
      Error* error);

  void SetStringsProperty(std::string_view name,
                          const std::vector<std::string>& values,
                          Error* error);

  void SetUint8Property(std::string_view name, uint8_t value, Error* error);

  void SetByteArrayProperty(std::string_view name,
                            const ByteArray& value,
                            Error* error);

  void SetUint16Property(std::string_view name, uint16_t value, Error* error);

  void SetUint16sProperty(std::string_view name,
                          const std::vector<uint16_t>& value,
                          Error* error);

  void SetUint32Property(std::string_view name, uint32_t value, Error* error);

  void SetUint64Property(std::string_view name, uint64_t value, Error* error);

  void SetRpcIdentifierProperty(std::string_view name,
                                const RpcIdentifier& value,
                                Error* error);

  // Clearing a property resets it to its "factory" value. This value
  // is generally the value that it (the property) had when it was
  // registered with PropertyStore.
  //
  // The exception to this rule is write-only derived properties. For
  // such properties, the property owner explicitly provides a
  // "factory" value at registration time. This is necessary because
  // PropertyStore can't read the current value at registration time.
  //
  // |name| is the key used to access the property. If the property
  // cannot be cleared, |error| is set, and the method returns false.
  // Otherwise, |error| is unchanged, and the method returns true.
  bool ClearProperty(std::string_view name, Error* error);

  // Methods for registering a property.
  //
  // It is permitted to re-register a property (in which case the old
  // binding is forgotten). However, the newly bound object must be of
  // the same type.
  //
  // Note that types do not encode read-write permission.  Hence, it
  // is possible to change permissions by rebinding a property to the
  // same object.
  //
  // (Corollary of the rebinding-to-same-type restriction: a
  // PropertyStore cannot hold two properties of the same name, but
  // differing types.)
  void RegisterBool(std::string_view name, bool* prop);
  void RegisterConstBool(std::string_view name, const bool* prop);
  void RegisterWriteOnlyBool(std::string_view name, bool* prop);
  void RegisterInt16(std::string_view name, int16_t* prop);
  void RegisterConstInt16(std::string_view name, const int16_t* prop);
  void RegisterWriteOnlyInt16(std::string_view name, int16_t* prop);
  void RegisterInt32(std::string_view name, int32_t* prop);
  void RegisterConstInt32(std::string_view name, const int32_t* prop);
  void RegisterWriteOnlyInt32(std::string_view name, int32_t* prop);
  void RegisterUint32(std::string_view name, uint32_t* prop);
  void RegisterConstUint32(std::string_view name, const uint32_t* prop);
  void RegisterUint64(std::string_view name, uint64_t* prop);
  void RegisterString(std::string_view name, std::string* prop);
  void RegisterConstString(std::string_view name, const std::string* prop);
  void RegisterWriteOnlyString(std::string_view name, std::string* prop);
  void RegisterStringmap(std::string_view name, Stringmap* prop);
  void RegisterConstStringmap(std::string_view name, const Stringmap* prop);
  void RegisterWriteOnlyStringmap(std::string_view name, Stringmap* prop);
  void RegisterStringmaps(std::string_view name, Stringmaps* prop);
  void RegisterConstStringmaps(std::string_view name, const Stringmaps* prop);
  void RegisterWriteOnlyStringmaps(std::string_view name, Stringmaps* prop);
  void RegisterStrings(std::string_view name, Strings* prop);
  void RegisterConstStrings(std::string_view name, const Strings* prop);
  void RegisterWriteOnlyStrings(std::string_view name, Strings* prop);
  void RegisterUint8(std::string_view name, uint8_t* prop);
  void RegisterConstUint8(std::string_view name, const uint8_t* prop);
  void RegisterWriteOnlyUint8(std::string_view name, uint8_t* prop);
  void RegisterUint16(std::string_view name, uint16_t* prop);
  void RegisterUint16s(std::string_view name, Uint16s* prop);
  void RegisterConstUint16(std::string_view name, const uint16_t* prop);
  void RegisterConstUint16s(std::string_view name, const Uint16s* prop);
  void RegisterWriteOnlyUint16(std::string_view name, uint16_t* prop);
  void RegisterByteArray(std::string_view name, ByteArray* prop);
  void RegisterConstByteArray(std::string_view name, const ByteArray* prop);
  void RegisterWriteOnlyByteArray(std::string_view name, ByteArray* prop);
  void RegisterKeyValueStore(std::string_view name, KeyValueStore* prop);
  void RegisterConstKeyValueStore(std::string_view name,
                                  const KeyValueStore* prop);
  void RegisterKeyValueStores(std::string_view name, KeyValueStores* prop);
  void RegisterConstKeyValueStores(std::string_view name,
                                   const KeyValueStores* prop);

  void RegisterDerivedBool(std::string_view name, BoolAccessor accessor);
  void RegisterDerivedInt32(std::string_view name, Int32Accessor accessor);
  void RegisterDerivedKeyValueStore(std::string_view name,
                                    KeyValueStoreAccessor accessor);
  void RegisterDerivedKeyValueStores(std::string_view name,
                                     KeyValueStoresAccessor accessor);
  void RegisterDerivedRpcIdentifier(std::string_view name,
                                    RpcIdentifierAccessor acc);
  void RegisterDerivedRpcIdentifiers(std::string_view name,
                                     RpcIdentifiersAccessor accessor);
  void RegisterDerivedString(std::string_view name, StringAccessor accessor);
  void RegisterDerivedStringmap(std::string_view name,
                                StringmapAccessor accessor);
  void RegisterDerivedStringmaps(std::string_view name,
                                 StringmapsAccessor accessor);
  void RegisterDerivedStrings(std::string_view name, StringsAccessor accessor);
  void RegisterDerivedUint16(std::string_view name, Uint16Accessor accessor);
  void RegisterDerivedUint64(std::string_view name, Uint64Accessor accessor);
  void RegisterDerivedUint16s(std::string_view name, Uint16sAccessor accessor);
  void RegisterDerivedByteArray(std::string_view name,
                                ByteArrayAccessor accessor);

 private:
  template <class V>
  bool GetProperty(std::string_view name,
                   V* value,
                   Error* error,
                   const AccessorMap<V>& collection,
                   std::string_view value_type_english) const;

  template <class V>
  bool SetProperty(std::string_view name,
                   const V& value,
                   Error* error,
                   AccessorMap<V>* collection,
                   std::string_view value_type_english);

  // These are std::maps instead of something cooler because the common
  // operation is iterating through them and returning all properties.
  std::map<std::string, BoolAccessor, std::less<>> bool_properties_;
  std::map<std::string, Int16Accessor, std::less<>> int16_properties_;
  std::map<std::string, Int32Accessor, std::less<>> int32_properties_;
  std::map<std::string, KeyValueStoreAccessor, std::less<>>
      key_value_store_properties_;
  std::map<std::string, KeyValueStoresAccessor, std::less<>>
      key_value_stores_properties_;
  std::map<std::string, RpcIdentifierAccessor, std::less<>>
      rpc_identifier_properties_;
  std::map<std::string, RpcIdentifiersAccessor, std::less<>>
      rpc_identifiers_properties_;
  std::map<std::string, StringAccessor, std::less<>> string_properties_;
  std::map<std::string, StringmapAccessor, std::less<>> stringmap_properties_;
  std::map<std::string, StringmapsAccessor, std::less<>> stringmaps_properties_;
  std::map<std::string, StringsAccessor, std::less<>> strings_properties_;
  std::map<std::string, Uint8Accessor, std::less<>> uint8_properties_;
  std::map<std::string, ByteArrayAccessor, std::less<>> bytearray_properties_;
  std::map<std::string, Uint16Accessor, std::less<>> uint16_properties_;
  std::map<std::string, Uint16sAccessor, std::less<>> uint16s_properties_;
  std::map<std::string, Uint32Accessor, std::less<>> uint32_properties_;
  std::map<std::string, Uint64Accessor, std::less<>> uint64_properties_;

  PropertyChangeCallback property_changed_callback_;
};

}  // namespace shill

#endif  // SHILL_STORE_PROPERTY_STORE_H_

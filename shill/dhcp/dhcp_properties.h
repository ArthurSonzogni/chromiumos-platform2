// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_DHCP_PROPERTIES_H_
#define SHILL_DHCP_DHCP_PROPERTIES_H_

#include <memory>
#include <string>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/accessor_interface.h"
#include "shill/key_value_store.h"

namespace shill {

class Error;
class PropertyStore;
class StoreInterface;

class DhcpProperties {
 public:
  static const char kHostnameProperty[];
  static const char kVendorClassProperty[];

  DhcpProperties();

  virtual ~DhcpProperties();

  // Adds property accessors to the DhcpProperty parameters in |this|
  // to |store|.
  void InitPropertyStore(PropertyStore* store);

  // Loads DHCP properties from |storage| in group |id|.
  virtual void Load(StoreInterface* store, const std::string& id);

  // Saves DHCP properties to |storage| in group |id|.
  virtual void Save(StoreInterface* store, const std::string& id) const;

  // Combines two DHCP property objects and returns a
  // std::unique_ptr<DhcpProperties>.  The new DhcpProperties instance is the
  // union of the key-value pairs in |base| and |to_merge|.  For keys which
  // exist in both |base| and |to_merge|, the value is taken from |to_merge|.
  // EX:  |base| stores {"VendorClass": "v1", "Hostname": "host1"}
  //      |to_merge| stores {"Hostname": "differentname"}
  //      returned DhcpProperties will store:
  //          {"VendorClass": "v1", "Hostname": "differentname"}
  // EX:  |base| stores {"Hostname": "host1"}
  //      |to_merge| stores {"Hostname": "differentname", "VendorClass": "vc"}
  //      returned DhcpProperties will store:
  //          {"Hostname": "differentname", "VendorClass": "vc"}
  static std::unique_ptr<DhcpProperties> Combine(
      const DhcpProperties& base,
      const DhcpProperties& to_merge);

  // Retrieves the value for a property with |name| in |value| if it is set.
  // Returns true if the property was found.
  bool GetValueForProperty(const std::string& name, std::string* value) const;

  const KeyValueStore& properties() const { return properties_; }

 private:
  FRIEND_TEST(DhcpPropertiesTest, ClearMappedStringPropertyNoExistingValue);
  FRIEND_TEST(DhcpPropertiesTest, ClearMappedStringPropertyWithSetValue);
  FRIEND_TEST(DhcpPropertiesTest, CombineIntoEmpty);
  FRIEND_TEST(DhcpPropertiesTest, CombineEmptyIntoExisting);
  FRIEND_TEST(DhcpPropertiesTest, CombineConflicting);
  FRIEND_TEST(DhcpPropertiesTest, Ctor);
  FRIEND_TEST(DhcpPropertiesTest, GetMappedStringPropertyNoExistingValue);
  FRIEND_TEST(DhcpPropertiesTest, GetMappedStringPropertyWithSetValue);
  FRIEND_TEST(DhcpPropertiesTest, GetValueForProperty);
  FRIEND_TEST(DhcpPropertiesTest, Load);
  FRIEND_TEST(DhcpPropertiesTest, LoadEmpty);
  FRIEND_TEST(DhcpPropertiesTest, LoadWithValuesSetAndClearRequired);
  FRIEND_TEST(DhcpPropertiesTest, SaveWithValuesSet);
  FRIEND_TEST(DhcpPropertiesTest, SavePropertyNotSetShouldBeDeleted);
  FRIEND_TEST(DhcpPropertiesTest, SetMappedStringPropertyNoExistingValue);
  FRIEND_TEST(DhcpPropertiesTest, SetMappedStringPropertyOverrideExisting);
  FRIEND_TEST(DhcpPropertiesTest, SetMappedStringPropertySameAsExistingValue);

  void ClearMappedStringProperty(const size_t& index, Error* error);
  std::string GetMappedStringProperty(const size_t& index, Error* error);
  bool SetMappedStringProperty(
      const size_t& index, const std::string& value, Error* error);

  // KeyValueStore tracking values for DhcpProperties settings.
  KeyValueStore properties_;

  DISALLOW_COPY_AND_ASSIGN(DhcpProperties);
};

}  // namespace shill

#endif  // SHILL_DHCP_DHCP_PROPERTIES_H_

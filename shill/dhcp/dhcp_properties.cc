// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp/dhcp_properties.h"

#include <iterator>
#include <memory>
#include <string>

#include <base/check.h>
#include <base/logging.h>
#include <base/macros.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/property_accessor.h"
#include "shill/property_store.h"
#include "shill/store/key_value_store.h"
#include "shill/store/store_interface.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
static std::string ObjectID(const DhcpProperties* d) {
  return "(dhcp_properties)";
}
}  // namespace Logging

namespace {

const char* const kPropertyNames[] = {DhcpProperties::kHostnameProperty,
                                      DhcpProperties::kVendorClassProperty};

std::string GetFullPropertyName(const std::string& property_name) {
  return DhcpProperties::kPropertyPrefix + property_name;
}

}  // namespace

// Prefix used for DhcpProperties in the PropertyStore and D-Bus interface.
const char DhcpProperties::kPropertyPrefix[] = "DHCPProperty.";
const char DhcpProperties::kHostnameProperty[] = "Hostname";
const char DhcpProperties::kVendorClassProperty[] = "VendorClass";

DhcpProperties::DhcpProperties(Manager* manager) : manager_(manager) {}

void DhcpProperties::InitPropertyStore(PropertyStore* store) {
  SLOG(this, 2) << __func__;
  int i = 0;
  for (const auto& name : kPropertyNames) {
    store->RegisterDerivedString(
        GetFullPropertyName(name),
        StringAccessor(
            new CustomMappedAccessor<DhcpProperties, std::string, size_t>(
                this, &DhcpProperties::ClearMappedStringProperty,
                &DhcpProperties::GetMappedStringProperty,
                &DhcpProperties::SetMappedStringProperty, i)));
    ++i;
  }
}

void DhcpProperties::Load(const StoreInterface* storage,
                          const std::string& id) {
  SLOG(this, 2) << __func__;
  properties_.Clear();
  for (const auto& name : kPropertyNames) {
    std::string property_value;
    if (storage->GetString(id, GetFullPropertyName(name), &property_value)) {
      properties_.Set<std::string>(name, property_value);
      SLOG(this, 3) << "found DhcpProperty: setting " << name;
    }
  }
}

void DhcpProperties::Save(StoreInterface* storage,
                          const std::string& id) const {
  SLOG(this, 2) << __func__;
  for (const auto& name : kPropertyNames) {
    std::string property_value;
    if (properties_.ContainsVariant(name)) {
      // The property is in the property store and it may have a setting or be
      // set to an empty string.  This setting should be saved to the profile.
      property_value = properties_.Get<std::string>(name);
      storage->SetString(id, GetFullPropertyName(name), property_value);
      SLOG(this, 3) << "saved " << GetFullPropertyName(name);
    } else {
      // The property is not found and should be deleted from the property store
      // if it was there.
      storage->DeleteKey(id, GetFullPropertyName(name));
    }
  }
}

DhcpProperties DhcpProperties::Combine(const DhcpProperties& base,
                                       const DhcpProperties& to_merge) {
  SLOG(nullptr, 2) << __func__;
  DhcpProperties to_return(base.manager_);
  to_return.properties_ = base.properties_;
  for (const auto& it : to_merge.properties_.properties()) {
    const auto& name = it.first;
    const brillo::Any& value = it.second;
    to_return.properties_.SetVariant(name, value);
  }
  return to_return;
}

bool DhcpProperties::GetValueForProperty(const std::string& name,
                                         std::string* value) const {
  if (properties_.Contains<std::string>(name)) {
    *value = properties_.Get<std::string>(name);
    return true;
  }
  return false;
}

void DhcpProperties::ClearMappedStringProperty(const size_t& index,
                                               Error* error) {
  CHECK(index < std::size(kPropertyNames));
  if (properties_.Contains<std::string>(kPropertyNames[index])) {
    properties_.Remove(kPropertyNames[index]);
  } else {
    error->Populate(Error::kNotFound, "Property is not set");
  }
}

std::string DhcpProperties::GetMappedStringProperty(const size_t& index,
                                                    Error* error) {
  CHECK(index < std::size(kPropertyNames));
  const std::string& key = kPropertyNames[index];
  SLOG(this, 3) << __func__ << ": " << key;
  if (properties_.Contains<std::string>(key)) {
    return properties_.Get<std::string>(key);
  }
  error->Populate(Error::kNotFound, "Property is not set");
  return std::string();
}

bool DhcpProperties::SetMappedStringProperty(const size_t& index,
                                             const std::string& value,
                                             Error* error) {
  CHECK(index < std::size(kPropertyNames));
  const std::string& key = kPropertyNames[index];
  SLOG(this, 3) << __func__ << ": " << key << " = " << value;
  if (properties_.Contains<std::string>(key) &&
      properties_.Get<std::string>(key) == value) {
    return false;
  }
  properties_.Set<std::string>(key, value);
  if (manager_)
    manager_->OnDhcpPropertyChanged(kPropertyPrefix + key, value);
  return true;
}

}  // namespace shill

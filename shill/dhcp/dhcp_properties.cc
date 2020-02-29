// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp/dhcp_properties.h"

#include <memory>
#include <string>

#include <base/macros.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/key_value_store.h"
#include "shill/logging.h"
#include "shill/property_accessor.h"
#include "shill/property_store.h"
#include "shill/store_interface.h"

using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
static string ObjectID(const DhcpProperties* d) {
  return "(dhcp_properties)";
}
}  // namespace Logging

namespace {

// Prefix used for DhcpProperties in the PropertyStore and D-Bus interface.
const char kPropertyPrefix[] = "DHCPProperty.";

const char* const kPropertyNames[] = {DhcpProperties::kHostnameProperty,
                                      DhcpProperties::kVendorClassProperty};

std::string GetFullPropertyName(const std::string& property_name) {
  return kPropertyPrefix + property_name;
}

}  // namespace

const char DhcpProperties::kHostnameProperty[] = "Hostname";
const char DhcpProperties::kVendorClassProperty[] = "VendorClass";

DhcpProperties::DhcpProperties() = default;

DhcpProperties::~DhcpProperties() = default;

void DhcpProperties::InitPropertyStore(PropertyStore* store) {
  SLOG(this, 2) << __func__;
  int i = 0;
  for (const auto& name : kPropertyNames) {
    store->RegisterDerivedString(
        GetFullPropertyName(name),
        StringAccessor(new CustomMappedAccessor<DhcpProperties, string, size_t>(
            this, &DhcpProperties::ClearMappedStringProperty,
            &DhcpProperties::GetMappedStringProperty,
            &DhcpProperties::SetMappedStringProperty, i)));
    ++i;
  }
}

void DhcpProperties::Load(StoreInterface* storage, const string& id) {
  SLOG(this, 2) << __func__;
  properties_.Clear();
  for (const auto& name : kPropertyNames) {
    string property_value;
    if (storage->GetString(id, GetFullPropertyName(name), &property_value)) {
      properties_.SetString(name, property_value);
      SLOG(this, 3) << "found DhcpProperty: setting " << name;
    }
  }
}

void DhcpProperties::Save(StoreInterface* storage, const string& id) const {
  SLOG(this, 2) << __func__;
  for (const auto& name : kPropertyNames) {
    string property_value;
    if (properties_.Contains(name)) {
      // The property is in the property store and it may have a setting or be
      // set to an empty string.  This setting should be saved to the profile.
      property_value = properties_.GetString(name);
      storage->SetString(id, GetFullPropertyName(name), property_value);
      SLOG(this, 3) << "saved " << GetFullPropertyName(name);
    } else {
      // The property is not found and should be deleted from the property store
      // if it was there.
      storage->DeleteKey(id, GetFullPropertyName(name));
    }
  }
}

std::unique_ptr<DhcpProperties> DhcpProperties::Combine(
    const DhcpProperties& base, const DhcpProperties& to_merge) {
  SLOG(nullptr, 2) << __func__;
  auto to_return = std::make_unique<DhcpProperties>();
  to_return->properties_ = base.properties_;
  for (const auto& it : to_merge.properties_.properties()) {
    const string& name = it.first;
    const brillo::Any& value = it.second;
    to_return->properties_.Set(name, value);
  }
  return to_return;
}

bool DhcpProperties::GetValueForProperty(const string& name,
                                         string* value) const {
  if (properties_.ContainsString(name)) {
    *value = properties_.GetString(name);
    return true;
  }
  return false;
}

void DhcpProperties::ClearMappedStringProperty(const size_t& index,
                                               Error* error) {
  CHECK(index < arraysize(kPropertyNames));
  if (properties_.ContainsString(kPropertyNames[index])) {
    properties_.Remove(kPropertyNames[index]);
  } else {
    error->Populate(Error::kNotFound, "Property is not set");
  }
}

string DhcpProperties::GetMappedStringProperty(const size_t& index,
                                               Error* error) {
  CHECK(index < arraysize(kPropertyNames));
  if (properties_.ContainsString(kPropertyNames[index])) {
    return properties_.GetString(kPropertyNames[index]);
  }
  error->Populate(Error::kNotFound, "Property is not set");
  return string();
}

bool DhcpProperties::SetMappedStringProperty(const size_t& index,
                                             const string& value,
                                             Error* error) {
  CHECK(index < arraysize(kPropertyNames));
  if (properties_.ContainsString(kPropertyNames[index]) &&
      properties_.GetString(kPropertyNames[index]) == value) {
    return false;
  }
  properties_.SetString(kPropertyNames[index], value);
  return true;
}

}  // namespace shill

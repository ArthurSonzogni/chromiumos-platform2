// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/profile_dbus_property_exporter.h"

#include <string>

#include <chromeos/dbus/service_constants.h>
#include <dbus-c++/dbus.h>

#include "shill/dbus_adaptor.h"
#include "shill/eap_credentials.h"
#include "shill/error.h"
#include "shill/service.h"
#include "shill/store_interface.h"
#include "shill/technology.h"
#include "shill/wifi_service.h"

using std::string;

namespace shill {

ProfileDBusPropertyExporter::ProfileDBusPropertyExporter(
    const StoreInterface *storage, const string &entry_name)
    : storage_(storage), entry_name_(entry_name) {}

ProfileDBusPropertyExporter::~ProfileDBusPropertyExporter() {}

bool ProfileDBusPropertyExporter::LoadServiceProperties(
    PropertyList *properties, Error *error) {
  if (!storage_->ContainsGroup(entry_name_)) {
    Error::PopulateAndLog(
        error, Error::kNotFound,
        "Could not find profile entry: " + entry_name_);
    return false;
  }

  Technology::Identifier technology =
      Technology::IdentifierFromStorageGroup(entry_name_);

  if (technology == Technology::kUnknown) {
    Error::PopulateAndLog(
        error, Error::kInternalError,
        "Could not determine technology for entry: " + entry_name_);
    return false;
  }

  if (technology == Technology::kWifi) {
    LoadWiFiServiceProperties(properties, error);
  }

  LoadBool(properties, Service::kStorageAutoConnect, kAutoConnectProperty);
  LoadString(properties, Service::kStorageError, kErrorProperty);
  LoadString(properties, Service::kStorageGUID, kGuidProperty);
  LoadString(properties, Service::kStorageName, kNameProperty);
  if (!LoadString(properties, Service::kStorageType, kTypeProperty)) {
    SetString(properties, kTypeProperty,
              Technology::NameFromIdentifier(technology));
  }
  LoadString(properties, Service::kStorageUIData, kUIDataProperty);
  return true;
}

bool ProfileDBusPropertyExporter::LoadEapServiceProperties(
    PropertyList *properties,
    Error */*error*/) {
  LoadString(properties, EapCredentials::kStorageEapCACertID,
             kEapCaCertIdProperty);
  LoadString(properties, EapCredentials::kStorageEapCertID, kEapCertIdProperty);
  LoadString(properties, EapCredentials::kStorageEapKeyID, kEapKeyIdProperty);
  return true;
}

bool ProfileDBusPropertyExporter::LoadWiFiServiceProperties(
    PropertyList *properties,
    Error */*error*/) {
  LoadBool(properties, WiFiService::kStorageHiddenSSID, kWifiHiddenSsid);

  // Support the old and busted technique for storing "Mode" and "Security"
  // within the entry name.
  string address;
  string mode;
  string security;
  bool parsed_identifier = WiFiService::ParseStorageIdentifier(
      entry_name_, &address, &mode, &security);

  if (!LoadString(properties, WiFiService::kStorageMode, kModeProperty) &&
      parsed_identifier) {
    SetString(properties, kModeProperty, mode);
  }

  if (!LoadString(properties, WiFiService::kStorageSecurity,
                  kSecurityProperty) && parsed_identifier) {
    SetString(properties, kSecurityProperty, security);
  }

  LoadEapServiceProperties(properties, NULL);

  return true;
}

bool ProfileDBusPropertyExporter::LoadBool(PropertyList *properties,
                                           const string &storage_name,
                                           const string &dbus_name) {
  bool value;
  if (!storage_->GetBool(entry_name_, storage_name, &value)) {
    return false;
  }

  SetBool(properties, dbus_name, value);
  return true;
}

bool ProfileDBusPropertyExporter::LoadString(PropertyList *properties,
                                             const string &storage_name,
                                             const string &dbus_name) {
  string value;
  if (!storage_->GetString(entry_name_, storage_name, &value)) {
    return false;
  }

  SetString(properties, dbus_name, value);
  return true;
}

void ProfileDBusPropertyExporter::SetBool(PropertyList *properties,
                                          const string &dbus_name,
                                          bool value) {
  (*properties)[dbus_name] = DBusAdaptor::BoolToVariant(value);
}

void ProfileDBusPropertyExporter::SetString(PropertyList *properties,
                                            const string &dbus_name,
                                            const string &value) {
  (*properties)[dbus_name] = DBusAdaptor::StringToVariant(value);
}

}  // namespace shill

// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_PROFILE_DBUS_ADAPTOR_H_
#define SHILL_DBUS_CHROMEOS_PROFILE_DBUS_ADAPTOR_H_

#include <string>

#include <base/macros.h>

#include "shill/adaptor_interfaces.h"
#include "shill/chromeos_dbus_adaptors/org.chromium.flimflam.Profile.h"
#include "shill/dbus/chromeos_dbus_adaptor.h"

namespace shill {

class Profile;

// Subclass of DBusAdaptor for Profile objects
// There is a 1:1 mapping between Profile and ChromeosProfileDBusAdaptor
// instances.  Furthermore, the Profile owns the ChromeosProfileDBusAdaptor
// and manages its lifetime, so we're OK with ChromeosProfileDBusAdaptor
// having a bare pointer to its owner profile.
//
// A Profile is a collection of Entry structures (which we will define later).
class ChromeosProfileDBusAdaptor
    : public org::chromium::flimflam::ProfileAdaptor,
      public org::chromium::flimflam::ProfileInterface,
      public ChromeosDBusAdaptor,
      public ProfileAdaptorInterface {
 public:
  static const char kPath[];

  ChromeosProfileDBusAdaptor(
      const base::WeakPtr<chromeos::dbus_utils::ExportedObjectManager>&
          object_manager,
      const scoped_refptr<dbus::Bus>& bus,
      Profile* profile);
  ~ChromeosProfileDBusAdaptor() override;

  // Implementation of ProfileAdaptorInterface.
  const std::string& GetRpcIdentifier() override { return dbus_path().value(); }
  void EmitBoolChanged(const std::string& name, bool value) override;
  void EmitUintChanged(const std::string& name, uint32_t value) override;
  void EmitIntChanged(const std::string& name, int value) override;
  void EmitStringChanged(const std::string& name,
                         const std::string& value) override;

  // Implementation of ProfileAdaptor
  bool GetProperties(chromeos::ErrorPtr* error,
                     chromeos::VariantDictionary* properties) override;
  bool SetProperty(chromeos::ErrorPtr* error,
                   const std::string& name,
                   const chromeos::Any& value) override;

  // Gets an "Entry", which is apparently a different set of properties than
  // those returned by GetProperties.
  bool GetEntry(chromeos::ErrorPtr* error,
                const std::string& name,
                chromeos::VariantDictionary* entry_properties) override;

  // Deletes an Entry.
  bool DeleteEntry(chromeos::ErrorPtr* error, const std::string& name) override;

 private:
  Profile* profile_;

  DISALLOW_COPY_AND_ASSIGN(ChromeosProfileDBusAdaptor);
};

}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_PROFILE_DBUS_ADAPTOR_H_

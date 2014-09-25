// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_STATES_STATE_PACKAGE_H_
#define BUFFET_STATES_STATE_PACKAGE_H_

#include <map>
#include <memory>
#include <string>

#include <base/macros.h>
#include <chromeos/any.h>
#include <chromeos/errors/error.h>

#include "buffet/commands/object_schema.h"
#include "buffet/commands/prop_values.h"

namespace base {
class DictionaryValue;
}  // namespace base

namespace buffet {

// A package is a set of related state properties. GCD specification defines
// a number of standard state properties in "base" package such as
// "base.manufacturer", "base.model", "base.firmwareVersion" and so on.
class StatePackage final {
 public:
  explicit StatePackage(const std::string& name);

  // Loads state property definitions from a JSON object and adds them
  // to the current package.
  bool AddSchemaFromJson(const base::DictionaryValue* json,
                         chromeos::ErrorPtr* error);
  // Loads a set of state property values from a JSON object and assigns them
  // to existing properties.  A property must be defined prior to loading its
  // value.  We use this when we load default values during buffet startup.
  bool AddValuesFromJson(const base::DictionaryValue* json,
                         chromeos::ErrorPtr* error);

  // Returns a set of state properties and their values as a JSON object.
  // After being aggregated across multiple packages, this becomes the device
  // state object passed to the GCD server or a local client in the format
  // described by GCD specification, e.g.:
  //  {
  //    "base": {
  //      "manufacturer":"...",
  //      "model":"..."
  //    },
  //    "printer": {
  //      "message": "Printer low on cyan ink"
  //    }
  //  }
  std::unique_ptr<base::DictionaryValue> GetValuesAsJson(
        chromeos::ErrorPtr* error) const;

  // Gets the value for a specific state property. |property_name| must not
  // include the package name as part of the property name.
  chromeos::Any GetPropertyValue(const std::string& property_name,
                                 chromeos::ErrorPtr* error) const;
  // Sets the value for a specific state property. |property_name| must not
  // include the package name as part of the property name.
  bool SetPropertyValue(const std::string& property_name,
                        const chromeos::Any& value,
                        chromeos::ErrorPtr* error);

  // Returns the name of the this package.
  const std::string& GetName() const { return name_; }

 private:
  std::string name_;
  ObjectSchema types_;
  native_types::Object values_;

  friend class StatePackageTestHelper;
  DISALLOW_COPY_AND_ASSIGN(StatePackage);
};

}  // namespace buffet

#endif  // BUFFET_STATES_STATE_PACKAGE_H_

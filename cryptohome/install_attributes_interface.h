// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// InstallAttributesInterface - interface for managing install-time system
// attributes.

#ifndef CRYPTOHOME_INSTALL_ATTRIBUTES_INTERFACE_H_
#define CRYPTOHOME_INSTALL_ATTRIBUTES_INTERFACE_H_

#include <device_management/proto_bindings/device_management_interface.pb.h>
#include <device_management-client/device_management/dbus-proxies.h>

#include <memory>
#include <string>

#include <brillo/secure_blob.h>

namespace cryptohome {
class InstallAttributesInterface {
 public:
  virtual ~InstallAttributesInterface() = default;
  enum class Status {
    kUnknown,       // Not initialized yet.
    kTpmNotOwned,   // TPM not owned yet.
    kFirstInstall,  // Allows writing.
    kValid,         // Validated successfully.
    kInvalid,       // Not valid, e.g. clobbered, absent.
    COUNT,          // This is unused, just for counting the number of elements.
                    // Note that COUNT should always be the last element.
  };
  // Prepares the class for use including instantiating a new environment
  // if needed.
  virtual bool Init() = 0;

  // Populates |value| based on the content referenced by |name|.
  //
  // Parameters
  // - name: addressable name of the entry to retrieve
  // - value: pointer to a Blob to populate with the value, if found.
  // Returns true if |name| exists in the store and |value| will be populated.
  // Returns false if the |name| does not exist.
  virtual bool Get(const std::string& name, brillo::Blob* value) const = 0;

  // Appends |name| and |value| as an attribute pair to the internal store.
  //
  // Parameters
  // - name: attribute name to associate |value| with in the store
  // - value: Blob of data to store with |name|.
  // Returns true if the association can be stored, and false if it can't.
  // If the given |name| already exists, it will be replaced.
  virtual bool Set(const std::string& name, const brillo::Blob& value) = 0;

  // Finalizes the install-time attributes making them tamper-evident.
  virtual bool Finalize() = 0;

  // Returns the number of entries in the Lockbox.
  virtual int Count() const = 0;

  // Indicates if there is hardware protection or not.
  virtual bool IsSecure() = 0;

  // Returns the current status of install_attributes.
  virtual Status status() = 0;

  // Sets the device_management proxy for forwarding requests to
  // device_management service. This is a no-op for legacy install_attributes.
  virtual void SetDeviceManagementProxy(
      std::unique_ptr<org::chromium::DeviceManagementProxy> proxy) = 0;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_INSTALL_ATTRIBUTES_INTERFACE_H_

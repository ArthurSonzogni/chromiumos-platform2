// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// InstallAttributes - class for managing install-time system attributes.

#ifndef CRYPTOHOME_INSTALL_ATTRIBUTES_H_
#define CRYPTOHOME_INSTALL_ATTRIBUTES_H_

#include <string>

#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <base/observer_list.h>
#include <base/values.h>
#include <chromeos/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/lockbox.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"

#include "install_attributes.pb.h"  // NOLINT(build/include)

namespace cryptohome {

// InstallAttributes - manages secure, install-time attributes
//
// Provides setting and getting of tamper-evident install-time
// attributes.  Upon finalization, the underlying tamper-evident
// store will "lock" the attributes such that they become read-only
// until the next install.
//
// InstallAttributes is not thread-safe and should not be accessed in parallel.
class InstallAttributes {
 public:
  class Observer {
   public:
    virtual void OnFinalized() = 0;
  };

  // Creates an instance of install attributes that will use the |tpm|. If |tpm|
  // is NULL, InstallAttributes will proceed insecurely (unless it is set with
  // SetTpm at a later time).
  explicit InstallAttributes(Tpm* tpm);
  virtual ~InstallAttributes();

  // Prepares the underlying system for use on first-install only.
  // It will only pre-configure the TPM if authorization is possible.
  // If needed, this should be called before Init.
  virtual bool PrepareSystem();

  // Updates the TPM used by Lockbox or disables the use of the TPM.
  // This does NOT take ownership of the pointer.
  virtual void SetTpm(Tpm* tpm);

  // Prepares the class for use including instantiating a new environment
  // if needed. If initialization completes, |tpm_init| will be used to remove
  // this instance's dependency on the TPM ownership.
  virtual bool Init(TpmInit* tpm_init);

  // Determines if the instance can provide consistent responses to Get*(),
  // Set(), Finalize(), and Count().  Repeated calls to Init() may change this.
  virtual bool IsReady() const { return (is_initialized() || is_invalid()); }

  // Populates |value| based on the content referenced by |name|.
  //
  // Parameters
  // - name: addressable name of the entry to retrieve
  // - value: pointer to a Blob to populate with the value, if found.
  // Returns true if |name| exists in the store and |value| will be populated.
  // Returns false if the |name| does not exist.
  virtual bool Get(const std::string& name, chromeos::Blob* value) const;

  // Populates |name| and |value| based on the content referenced by |index|.
  //
  // Parameters
  // - index: 0-addressable index of the desired entry.
  // - name: addressable name of the entry to retrieve
  // - value: pointer to a Blob to populate with the value, if found.
  // Returns true if |index| exists in the store.
  // Returns false if the |index| does not exist.
  virtual bool GetByIndex(int index,
                          std::string* name,
                          chromeos::Blob* value) const;

  // Appends |name| and |value| as an attribute pair to the internal store.
  //
  // Parameters
  // - name: attribute name to associate |value| with in the store
  // - value: Blob of data to store with |name|.
  // Returns true if the association can be stored, and false if it can't.
  // If the given |name| already exists, it will be replaced.
  virtual bool Set(const std::string& name, const chromeos::Blob& value);

  // Finalizes the install-time attributes making them tamper-evident.
  virtual bool Finalize();

  // Returns the number of entries in the Lockbox.
  virtual int Count() const;

  // Sets the instance to invalid and clears any active data.
  //
  // Parameters
  // - is_invalid: whether the instance should be invalidated until next Init()
  virtual void SetIsInvalid(bool is_invalid);

  // Return InstallAttributes version.
  // This is populated from the default value in install_attributes.proto and
  // should be incremented there when behavior vesioning is needed.
  virtual uint64_t version() const { return version_; }

  // Allows overriding the version, often for testing.
  virtual void set_version(uint64_t version) { version_ = version; }

  // Returns true if Init() was called successfully.
  virtual bool is_initialized() const { return is_initialized_; }

  // Allows overriding the is_initialized state.
  virtual void set_is_initialized(bool is_initialized) {
    is_initialized_ = is_initialized;
  }

  // Returns true if the attributes could not be restored.
  // This variable is valid after a successful or unsuccessful call to Init().
  virtual bool is_invalid() const { return is_invalid_; }

  // Returns true if the attribute storage is securely stored.  It does not
  // indicate if the store has been finalized, just if the system TPM/Lockbox
  // is being used.
  virtual bool is_secure() const { return is_secure_; }

  virtual void set_is_secure(bool is_secure) { is_secure_ = is_secure; }

  // Returns the path where the data is persisted.
  virtual const char* data_file() { return data_file_.c_str(); }
  virtual void set_data_file(const char* data_file) {
    data_file_.assign(data_file);
  }

  // Returns the path of the world-readable cache file.
  virtual const char* cache_file() { return cache_file_.c_str(); }
  virtual void set_cache_file(const char* cache_file) {
    cache_file_.assign(cache_file);
  }

  // Allows replacement of the underlying lockbox.
  // This does NOT take ownership of the pointer.
  virtual void set_lockbox(Lockbox* lockbox) { lockbox_ = lockbox; }

  virtual Lockbox* lockbox() { return lockbox_; }

  // Replaces the platform implementation.
  // Does NOT take ownership of the pointer.
  virtual void set_platform(Platform* platform) { platform_ = platform; }

  virtual Platform* platform() { return platform_; }

  // Returns whether this is still a first install. Upon finalization,
  // this becomes false.
  virtual bool is_first_install() const { return is_first_install_; }

  virtual void set_is_first_install(bool first) { is_first_install_ = first; }

  // Returns a description of the system's install attributes as a Value.
  //
  // The Value is a DictionaryValue, with keys "initialized", "version",
  // "lockbox_index", "secure", "invalid", "first_install" and "size".
  virtual base::Value* GetStatus();

  void AddObserver(Observer* obs) {
    observer_list_.AddObserver(obs);
  }

  void RemoveObserver(Observer* obs) {
    observer_list_.RemoveObserver(obs);
  }

  void NotifyFinalized() {
    FOR_EACH_OBSERVER(Observer, observer_list_, OnFinalized());
  }

  // Provides the TPM NVRAM index to be used by the underlying Lockbox instance.
  static const uint32_t kLockboxIndex;
  // Provides the default location for the attributes data file.
  static const char* kDefaultDataFile;
  // Provides the default location for the cache file.
  static const char* kDefaultCacheFile;

 protected:
  // Helper to find a given entry index using its name.
  virtual int FindIndexByName(const std::string& name) const;
  // Convert the current attributes to a byte stream and write it
  // to |out_bytes|.
  virtual bool SerializeAttributes(chromeos::Blob* out_bytes);

 private:
  bool is_first_install_;  // PrepareSystem sets this.
  bool is_secure_;  // Indicates if there is hardware protection (TPM).
  bool is_invalid_;  // Indicates tampered/corrupted data.
  bool is_initialized_;  // Indicates a successful, valid instance.
  std::string data_file_;  // Location data is persisted to.
  std::string cache_file_;  // World-readable data cache file.
  uint64_t version_;  // Default implementation version.
  // Default implementations of dependencies
  scoped_ptr<SerializedInstallAttributes> default_attributes_;
  scoped_ptr<Lockbox> default_lockbox_;
  scoped_ptr<Platform> default_platform_;
  // Overridable dependency pointer which allow for easy injection.
  SerializedInstallAttributes* attributes_;
  Lockbox* lockbox_;
  Platform* platform_;
  ObserverList<Observer> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(InstallAttributes);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_INSTALL_ATTRIBUTES_H_

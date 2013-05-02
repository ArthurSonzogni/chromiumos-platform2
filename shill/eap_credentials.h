// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_EAP_CREDENTIALS_H_
#define SHILL_EAP_CREDENTIALS_H_

#include <map>
#include <string>
#include <vector>

#include <base/file_path.h>
#include <dbus-c++/dbus.h>

#include "shill/technology.h"

namespace shill {

class CertificateFile;
class Error;
class KeyValueStore;
class Metrics;
class NSS;
class PropertyStore;
class StoreInterface;

class EapCredentials {
 public:
  // TODO(pstew): Storage constants shouldn't need to be public
  // crosbug.com/25813
  static const char kStorageEapAnonymousIdentity[];
  static const char kStorageEapCACert[];
  static const char kStorageEapCACertID[];
  static const char kStorageEapCACertNSS[];
  static const char kStorageEapCACertPEM[];
  static const char kStorageEapCertID[];
  static const char kStorageEapClientCert[];
  static const char kStorageEapEap[];
  static const char kStorageEapIdentity[];
  static const char kStorageEapInnerEap[];
  static const char kStorageEapKeyID[];
  static const char kStorageEapKeyManagement[];
  static const char kStorageEapPIN[];
  static const char kStorageEapPassword[];
  static const char kStorageEapPrivateKey[];
  static const char kStorageEapPrivateKeyPassword[];
  static const char kStorageEapSubjectMatch[];
  static const char kStorageEapUseSystemCAs[];

  EapCredentials();
  virtual ~EapCredentials();

  // Add property accessors to the EAP credential parameters in |this| to
  // |store|.
  void InitPropertyStore(PropertyStore *store);

  // Returns true if |property| is used for authentication in EapCredentials.
  static bool IsEapAuthenticationProperty(const std::string property);

  // Returns true if a connection can be made with |this| credentials using
  // either passphrase or certificates.
  virtual bool IsConnectable() const;

  // Returns true if a connection can be made with |this| credentials using
  // only passphrase properties.
  virtual bool IsConnectableUsingPassphrase() const;

  // Loads EAP properties from |storage| in group |id|.
  virtual void Load(StoreInterface *store, const std::string &id);

  // Output metrics about this EAP connection to |metrics| with technology
  // |technology|.
  virtual void OutputConnectionMetrics(Metrics *metrics,
                                       Technology::Identifier technology) const;

  // Populate the wpa_supplicant DBus parameter map |params| with the
  // credentials in |this|.  To do so, this function may use |certificate_file|
  // or |nss| to export CA certificates to be passed to wpa_supplicant.
  virtual void PopulateSupplicantProperties(
      CertificateFile *certificate_file,
      NSS *nss,
      const std::vector<char> nss_identifier,
      std::map<std::string, DBus::Variant> *params) const;

  // Populate the WiMax connection parameters |params| with the
  // credentials in |this|.
  virtual void PopulateWiMaxProperties(
      KeyValueStore *params) const;

  // Save EAP properties to |storage| in group |id|.  If |save_credentials|
  // is true, passwords and identities that are a part of the credentials are
  // also saved.
  virtual void Save(StoreInterface *store, const std::string &id,
                    bool save_credentials) const;

  // Restore EAP properties to their initial state.
  virtual void Reset();

  // Setter that guards against emptying the "Key Management" value.
  virtual bool SetKeyManagement(const std::string &key_management,
                                Error *error);

  // Getters and setters.
  virtual const std::string &identity() const { return identity_; }
  void set_identity(const std::string &identity) {
    identity_ = identity;
  }
  virtual const std::string &key_management() const { return key_management_; }
  virtual void set_password(const std::string &password) {
    password_ = password;
  }

 private:
  friend class EapCredentialsTest;

  // Expose a property in |store|, with the name |name|.
  //
  // Reads of the property will be handled by invoking |get|.
  // Writes to the property will be handled by invoking |set|.
  void HelpRegisterDerivedString(
      PropertyStore *store,
      const std::string &name,
      std::string(EapCredentials::*get)(Error *error),
      bool(EapCredentials::*set)(const std::string &value, Error *error));

  // Expose a property in |store|, with the name |name|.
  //
  // Reads of the property will be handled by invoking |get|.
  //
  // Clearing the property will be handled by invoking |clear|, or
  // calling |set| with |default_value| (whichever is non-NULL).  It
  // is an error to call this method with both |clear| and
  // |default_value| non-NULL.
  void HelpRegisterWriteOnlyDerivedString(
      PropertyStore *store,
      const std::string &name,
      bool(EapCredentials::*set)(const std::string &value, Error *error),
      void(EapCredentials::*clear)(Error *error),
      const std::string *default_value);

  // Assigns |value| to |key| in |storage| if |value| is non-empty and |save| is
  // true. Otherwise, removes |key| from |storage|. If |crypted| is true, the
  // value is encrypted.
  static void SaveString(StoreInterface *storage,
                         const std::string &id,
                         const std::string &key,
                         const std::string &value,
                         bool crypted,
                         bool save);

  // Setters for write-only RPC properties.
  bool SetEapPassword(const std::string &password, Error *error);
  bool SetEapPrivateKeyPassword(const std::string &password, Error *error);

  // RPC getter for key_management_.
  std::string GetKeyManagement(Error *error);

  // When there is an inner EAP type, use this identity for the outer.
  std::string anonymous_identity_;
  // Locator for the client certificate within the security token.
  std::string cert_id_;
  // Filename of the client certificate.
  std::string client_cert_;
  // Who we identify ourselves as to the EAP authenticator.
  std::string identity_;
  // Locator for the client private key within the security token.
  std::string key_id_;
  // Key management algorithm to use after EAP succeeds.
  std::string key_management_;
  // Password to use for EAP methods which require one.
  std::string password_;
  // PIN code for accessing the security token.
  std::string pin_;
  // Filename of the client private key.
  std::string private_key_;
  // Password for decrypting the client private key file.
  std::string private_key_password_;

  // Filename of the certificate authority (CA) certificate.
  std::string ca_cert_;
  // Locator for the CA certificate within the security token.
  std::string ca_cert_id_;
  // Locator for the CA certificate within the user NSS database.
  std::string ca_cert_nss_;
  // Raw PEM contents of the CA certificate.
  std::string ca_cert_pem_;
  // The outer or only EAP authetnication type.
  std::string eap_;
  // The inner EAP authentication type.
  std::string inner_eap_;
  // If non-empty, string to match remote subject against before connecting.
  std::string subject_match_;
  // If true, use the system-wide CA database to authenticate the remote.
  bool use_system_cas_;

  DISALLOW_COPY_AND_ASSIGN(EapCredentials);
};

}  // namespace shill

#endif  // SHILL_EAP_CREDENTIALS_H_

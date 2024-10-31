// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMINT_CONTEXT_ARC_KEYMINT_CONTEXT_H_
#define ARC_KEYMINT_CONTEXT_ARC_KEYMINT_CONTEXT_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <brillo/secure_blob.h>
#include <debugd/dbus-proxies.h>
#include <hardware/keymaster_defs.h>
#include <keymaster/attestation_context.h>
#include <keymaster/authorization_set.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>
#include <keymaster/key.h>
#include <keymaster/key_factory.h>
#include <keymaster/UniquePtr.h>
#include <libcrossystem/crossystem.h>
#include <mojo/cert_store.mojom.h>

#include "arc/keymint/context/arc_attestation_context.h"
#include "arc/keymint/context/arc_enforcement_policy.h"
#include "arc/keymint/context/arc_remote_provisioning_context.h"
#include "arc/keymint/context/context_adaptor.h"
#include "arc/keymint/context/cros_key.h"
#include "arc/keymint/key_data.pb.h"

namespace arc::keymint::context {

// Defines specific behavior for ARC KeyMint in ChromeOS.
class ArcKeyMintContext : public ::keymaster::PureSoftKeymasterContext {
 public:
  // Disable default constructor.
  ArcKeyMintContext() = delete;
  explicit ArcKeyMintContext(::keymaster::KmVersion version);
  ~ArcKeyMintContext() override;
  // Not copyable nor assignable.
  ArcKeyMintContext(const ArcKeyMintContext&) = delete;
  ArcKeyMintContext& operator=(const ArcKeyMintContext&) = delete;

  // Replaces the list of placeholders for Chrome OS keys.
  void set_placeholder_keys(
      std::vector<arc::keymint::mojom::ChromeOsKeyPtr> keys);

  // Returns the Chrome OS key corresponding to the given key blob, if any.
  std::optional<arc::keymint::mojom::ChromeOsKeyPtr> FindPlaceholderKey(
      const ::keymaster::KeymasterKeyBlob& key_material) const;

  // PureSoftKeymasterContext overrides.
  keymaster_error_t CreateKeyBlob(
      const ::keymaster::AuthorizationSet& key_description,
      keymaster_key_origin_t origin,
      const ::keymaster::KeymasterKeyBlob& key_material,
      ::keymaster::KeymasterKeyBlob* key_blob,
      ::keymaster::AuthorizationSet* hw_enforced,
      ::keymaster::AuthorizationSet* sw_enforced) const override;
  keymaster_error_t ParseKeyBlob(
      const ::keymaster::KeymasterKeyBlob& key_blob,
      const ::keymaster::AuthorizationSet& additional_params,
      ::keymaster::UniquePtr<::keymaster::Key>* key) const override;
  keymaster_error_t UpgradeKeyBlob(
      const ::keymaster::KeymasterKeyBlob& key_to_upgrade,
      const ::keymaster::AuthorizationSet& upgrade_params,
      ::keymaster::KeymasterKeyBlob* upgraded_key) const override;
  // TODO(b/353381387): override SetVerifiedBootInfo from ChromeOS context.
  keymaster_error_t SetVerifiedBootParams(
      std::string_view boot_state,
      std::string_view bootloader_state,
      const std::vector<uint8_t>& vbmeta_digest);
  keymaster_error_t SetVendorPatchlevel(uint32_t vendor_patchlevel) override;
  keymaster_error_t SetBootPatchlevel(uint32_t boot_patchlevel) override;
  std::optional<uint32_t> GetVendorPatchlevel() const override;
  std::optional<uint32_t> GetBootPatchlevel() const override;

  // Expose SerializeAuthorizationSetToBlob for tests.
  brillo::Blob TestSerializeAuthorizationSetToBlob(
      const ::keymaster::AuthorizationSet& authorization_set);
  keymaster_error_t SetSystemVersion(uint32_t os_version,
                                     uint32_t os_patchlevel) override;
  /* To avoid replay attacks, Android provides an input challenge for generating
   certificate request. This method sets the same challenge in
   ARC Remote Provisioning Context, where it will also be used in getting a
   ChromeOS quoted blob from libarc-attestation.
  */
  keymaster_error_t SetChallengeForCertificateRequest(
      std::vector<uint8_t>& challenge);

  /* Creates a unique ID based on hardware bound key of the device and other
     parameters. Should follow guidelines in
     hardware/interfaces/security/keymint/aidl/android/hardware/security/
     keymint/Tag.aidl.
  */
  keymaster::Buffer GenerateUniqueId(uint64_t creation_date_time,
                                     const keymaster_blob_t& application_id,
                                     bool reset_since_rotation,
                                     keymaster_error_t* error) const override;
  /* Verifies the Device IDs from build properties and adds them to the
   list of attested parameters.
  */
  keymaster_error_t VerifyAndCopyDeviceIds(
      const ::keymaster::AuthorizationSet& attestation_params,
      ::keymaster::AuthorizationSet* attestation) const override;

  AttestationContext* attestation_context() override;

  const ::keymaster::AttestationContext::VerifiedBootParams*
  GetVerifiedBootParams(keymaster_error_t* error) const override;

  ::keymaster::KeymasterEnforcement* enforcement_policy() override;

 private:
  // If |key_blob| contains an ARC owned key, deserialize it into |key_material|
  // and auth sets. Otherwise it is a CrOS owned key, deserialized into |key|.
  //
  // Can also deserialize insecure blobs.
  keymaster_error_t DeserializeBlob(
      const ::keymaster::KeymasterKeyBlob& key_blob,
      const ::keymaster::AuthorizationSet& hidden,
      ::keymaster::KeymasterKeyBlob* key_material,
      ::keymaster::AuthorizationSet* hw_enforced,
      ::keymaster::AuthorizationSet* sw_enforced,
      ::keymaster::UniquePtr<::keymaster::Key>* key) const;

  // Serialize the given key data info the output |key_blob|.
  keymaster_error_t SerializeKeyDataBlob(
      const ::keymaster::KeymasterKeyBlob& key_material,
      const ::keymaster::AuthorizationSet& hidden,
      const ::keymaster::AuthorizationSet& hw_enforced,
      const ::keymaster::AuthorizationSet& sw_enforced,
      ::keymaster::KeymasterKeyBlob* key_blob) const;

  // If |key_blob| contains an ARC owned key, deserialize it into |key_material|
  // and auth sets. Otherwise it is a CrOS owned key, deserialized into |key|.
  //
  // Only handles key blobs serialized by |SerializeKeyDataBlob|.
  keymaster_error_t DeserializeKeyDataBlob(
      const ::keymaster::KeymasterKeyBlob& key_blob,
      const ::keymaster::AuthorizationSet& hidden,
      ::keymaster::KeymasterKeyBlob* key_material,
      ::keymaster::AuthorizationSet* hw_enforced,
      ::keymaster::AuthorizationSet* sw_enforced,
      ::keymaster::UniquePtr<::keymaster::Key>* key) const;

  // Constructs a new Chrome OS |key|.
  keymaster_error_t LoadKey(
      KeyData&& key_data,
      ::keymaster::AuthorizationSet&& hw_enforced,
      ::keymaster::AuthorizationSet&& sw_enforced,
      ::keymaster::UniquePtr<::keymaster::Key>* key) const;

  // Serializes |key_data| into |key_blob|.
  bool SerializeKeyData(const KeyData& key_data,
                        const ::keymaster::AuthorizationSet& hidden,
                        ::keymaster::KeymasterKeyBlob* key_blob) const;

  // Deserializes the contents of |key_blob| into |key_data|.
  std::optional<KeyData> DeserializeKeyData(
      const ::keymaster::KeymasterKeyBlob& key_blob,
      const ::keymaster::AuthorizationSet& hidden) const;

  // Parses the given parameter into an instance of KeyData.
  //
  // May return |std::nullopt| when the placeholder key correspponding to this
  // |key_material| is invalid.
  std::optional<KeyData> PackToKeyData(
      const ::keymaster::KeymasterKeyBlob& key_material,
      const ::keymaster::AuthorizationSet& hw_enforced,
      const ::keymaster::AuthorizationSet& sw_enforced) const;

  // Removes the given |key| from the list of |placeholder_keys_|.
  void DeletePlaceholderKey(
      const arc::keymint::mojom::ChromeOsKeyPtr& key) const;

  // Derive values for verified boot parameters.
  // Returns the value of Verified Boot State from Bootloader state.
  // Locked bootloaderstate maps to Verified boot state and vice-versa.
  std::string DeriveVerifiedBootStateFromBootloaderState(
      const std::string bootloader_state) const;
  std::string DeriveBootloaderState() const;
  std::optional<std::vector<uint8_t>> GetVbMetaDigestFromFile() const;

  // Get boot key from ChromeOS verified boot debug logs and set the
  // |boot_key_|.
  void GetAndSetBootKeyFromLogs();

  void set_cros_system_for_tests(
      std::unique_ptr<crossystem::Crossystem> cros_system);
  void set_vbmeta_digest_file_dir_for_tests(
      base::FilePath& vbmeta_digest_file_dir);
  void set_dbus_for_tests(scoped_refptr<dbus::Bus> bus);

  // Since the initialization of |rsa_key_factory_| uses
  // |context_adaptor_|, hence |context_adaptor_| must
  // be declared before |rsa_key_factory_|.
  mutable ContextAdaptor context_adaptor_;
  mutable CrosKeyFactory rsa_key_factory_;

  mutable std::vector<arc::keymint::mojom::ChromeOsKeyPtr> placeholder_keys_;

  std::unique_ptr<crossystem::Crossystem> cros_system_;
  base::FilePath vbmeta_digest_file_dir_;
  std::optional<std::vector<uint8_t>> boot_key_;
  scoped_refptr<dbus::Bus> bus_;
  std::optional<uint32_t> vendor_patchlevel_;
  std::optional<uint32_t> boot_patchlevel_;
  std::unique_ptr<ArcAttestationContext> arc_attestation_context_;
  std::unique_ptr<ArcEnforcementPolicy> arc_enforcement_policy_;

  friend class ContextTestPeer;
};

}  // namespace arc::keymint::context

#endif  // ARC_KEYMINT_CONTEXT_ARC_KEYMINT_CONTEXT_H_

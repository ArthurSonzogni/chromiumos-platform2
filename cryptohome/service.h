// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SERVICE_H_
#define CRYPTOHOME_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/gtest_prod_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/threading/thread.h>
#include <brillo/glib/abstract_dbus_service.h>
#include <brillo/glib/dbus.h>
#include <brillo/glib/object.h>
#include <brillo/secure_blob.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dbus-glib.h>

#include "cryptohome/cryptohome_event_source.h"
#include "cryptohome/dbus_transition.h"
#include "cryptohome/firmware_management_parameters.h"
#include "cryptohome/install_attributes.h"
#include "cryptohome/migration_type.h"
#include "cryptohome/mount.h"
#include "cryptohome/mount_factory.h"
#include "cryptohome/mount_task.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/tpm_init.h"

#include "rpc.pb.h"  // NOLINT(build/include)

namespace chaps {
class TokenManagerClient;
}

namespace cryptohome {
namespace gobject {

struct Cryptohome;
}  // namespace gobject

class BootAttributes;
class BootLockbox;
// Wrapper for all timers used by the cryptohome daemon.
class TimerCollection;

// Bridges between the MountTaskObserver callback model and the
// CryptohomeEventSource callback model. This class forwards MountTaskObserver
// events to a CryptohomeEventSource. An instance of this class is single-use
// (i.e., will be freed after it has observed one event).
class MountTaskObserverBridge : public MountTaskObserver {
 public:
  explicit MountTaskObserverBridge(cryptohome::Mount* mount,
                                   CryptohomeEventSource* source)
    : mount_(mount), source_(source) { }
  virtual ~MountTaskObserverBridge() { }
  virtual bool MountTaskObserve(const MountTaskResult& result) {
    MountTaskResult *r = new MountTaskResult(result);
    r->set_mount(mount_);
    source_->AddEvent(r);
    return true;
  }

 protected:
  scoped_refptr<cryptohome::Mount> mount_;
  CryptohomeEventSource* source_;
};

// Service
// Provides a wrapper for exporting CryptohomeInterface to
// D-Bus and entering the glib run loop.
class Service : public brillo::dbus::AbstractDbusService,
                public CryptohomeEventSourceSink {
 public:
  Service();
  virtual ~Service();

  // Create the right Service based on command-line flags and TPM version.
  static Service* CreateDefault(const std::string& abe_data);

  // From brillo::dbus::AbstractDbusService
  // Setup the wrapped GObject and the GMainLoop
  virtual bool Initialize();
  virtual bool SeedUrandom();
  virtual void InitializeInstallAttributes(bool first_time);
  virtual void InitializePkcs11(cryptohome::Mount* mount);
  virtual bool Reset();

  // Used internally during registration to set the
  // proper service information.
  virtual const char *service_name() const {
    return kCryptohomeServiceName;
  }
  virtual const char *service_path() const {
    return kCryptohomeServicePath;
  }
  virtual const char *service_interface() const {
    return kCryptohomeInterface;
  }
  virtual GObject* service_object() const {
    return G_OBJECT(cryptohome_);
  }
  virtual void set_tpm(Tpm* tpm) {
    tpm_ = tpm;
  }
  virtual void set_tpm_init(TpmInit* tpm_init) {
    tpm_init_ = tpm_init;
  }
  virtual void set_initialize_tpm(bool value) {
    initialize_tpm_ = value;
  }
  virtual void set_auto_cleanup_period(int value) {
    auto_cleanup_period_ = value;
  }
  virtual void set_install_attrs(InstallAttributes* install_attrs) {
    install_attrs_ = install_attrs;
  }
  virtual void set_update_user_activity_period(int value) {
    update_user_activity_period_ = value;
  }
  virtual void set_mount_for_user(const std::string& username,
                                  cryptohome::Mount* m) {
    mounts_[username] = m;
  }
  virtual void set_crypto(Crypto* crypto) {
      crypto_ = crypto;
  }
  virtual void set_mount_factory(cryptohome::MountFactory* mf) {
    mount_factory_ = mf;
  }
  virtual void set_reply_factory(cryptohome::DBusReplyFactory* rf) {
    reply_factory_ = rf;
  }
  virtual void set_use_tpm(bool value) {
    use_tpm_ = value;
  }

  // Overrides the Platform implementation for Service.
  virtual void set_platform(cryptohome::Platform *platform) {
    platform_ = platform;
  }

  virtual cryptohome::Crypto* crypto() { return crypto_; }

  virtual void set_homedirs(cryptohome::HomeDirs* value) { homedirs_ = value; }

  virtual cryptohome::HomeDirs* homedirs() { return homedirs_; }

  virtual void set_chaps_client(chaps::TokenManagerClient* chaps_client) {
    chaps_client_ = chaps_client;
  }

  virtual void set_event_source_sink(CryptohomeEventSourceSink* sink) {
    event_source_sink_ = sink;
  }

  // Checks if the given user is the system owner.
  virtual bool IsOwner(const std::string &userid);

  // Returns the base directory of the eCryptfs destination, containing
  // the "user" and "root" directories.
  virtual bool GetMountPointForUser(const std::string& username,
                                    base::FilePath* path);

  // CryptohomeEventSourceSink
  virtual void NotifyEvent(CryptohomeEventBase* event);

  // TpmInit::OwnershipCallback
  virtual void OwnershipCallback(bool status, bool took_ownership);

  // Finalize TPM initialization after taking ownership:
  // - initialize & finalize install attributes
  // - send TpmInitStatus event
  // - prepare for enrollment
  // Posted on mount_thread_ by OwnershipCallback.
  virtual void ConfigureOwnedTpm(bool status, bool took_ownership);

  // Called during initialization (and on mount events) to ensure old mounts
  // are marked for unmount when possible by the kernel.  Returns true if any
  // mounts were stale and not cleaned up (because of open files).
  //
  // Parameters
  // - force: if true, unmounts all existing shadow mounts.
  //          if false, unmounts shadows mounts with no open files.
  virtual bool CleanUpStaleMounts(bool force);

  // Called during mount requests to ensure old hidden mounts are unmount.
  // Note that this only cleans up |mounts_| entries which were mounted with
  // the hidden_mount=true parameter, as these are supposed to be temporary.
  // Old mounts from another cryptohomed run (e.g. after a crash) are cleaned up
  // in CleanUpStaleMounts.
  virtual bool CleanUpHiddenMounts();

  void set_legacy_mount(bool legacy) { legacy_mount_ = legacy; }
  void set_force_ecryptfs(bool force_ecryptfs) {
    force_ecryptfs_ = force_ecryptfs;
  }

  virtual void set_boot_lockbox(BootLockbox* boot_lockbox) {
    boot_lockbox_ = boot_lockbox;
  }

  virtual void set_boot_attributes(BootAttributes* boot_attributes) {
    boot_attributes_ = boot_attributes;
  }

  virtual void set_firmware_management_parameters(
      FirmwareManagementParameters* fwmp) {
    firmware_management_parameters_ = fwmp;
  }

  virtual void set_low_disk_notification_period_ms(int value) {
    low_disk_notification_period_ms_ = value;
  }

  // Service implementation functions as wrapped in interface.cc
  // and defined in cryptohome.xml.
  virtual gboolean CheckKey(gchar *user,
                            gchar *key,
                            gboolean *OUT_result,
                            GError **error);
  virtual gboolean AsyncCheckKey(gchar *user,
                                 gchar *key,
                                 gint *OUT_async_id,
                                 GError **error);
  virtual gboolean MigrateKey(gchar *user,
                              gchar *from_key,
                              gchar *to_key,
                              gboolean *OUT_result,
                              GError **error);
  virtual gboolean AsyncMigrateKey(gchar *user,
                                   gchar *from_key,
                                   gchar *to_key,
                                   gint *OUT_async_id,
                                   GError **error);
  virtual gboolean AddKey(gchar *user,
                          gchar *key,
                          gchar *new_key,
                          gint *OUT_key_id,
                          gboolean *OUT_result,
                          GError **error);
  virtual gboolean AsyncAddKey(gchar *user,
                               gchar *key,
                               gchar *new_key,
                               gint *OUT_async_id,
                               GError **error);
  virtual void DoAddKeyEx(AccountIdentifier* account_id,
                          AuthorizationRequest*  authorization_request,
                          AddKeyRequest* add_key_request,
                          DBusGMethodInvocation* context);
  virtual gboolean AddKeyEx(GArray* account_id,
                            GArray* authorization_request,
                            GArray* add_key_request,
                            DBusGMethodInvocation* context);
  virtual void DoUpdateKeyEx(AccountIdentifier* account_id,
                             AuthorizationRequest*  authorization_request,
                             UpdateKeyRequest* update_key_request,
                             DBusGMethodInvocation* context);
  virtual gboolean UpdateKeyEx(GArray *account_id,
                               GArray *authorization_request,
                               GArray *update_key_request,
                               DBusGMethodInvocation *response);
  virtual void DoCheckKeyEx(AccountIdentifier* account_id,
                            AuthorizationRequest*  authorization_request,
                            CheckKeyRequest* check_key_request,
                            DBusGMethodInvocation* context);
  virtual gboolean CheckKeyEx(GArray *account_id,
                              GArray *authorization_request,
                              GArray *check_key_request,
                              DBusGMethodInvocation *context);
  virtual void DoRemoveKeyEx(AccountIdentifier* account_id,
                             AuthorizationRequest*  authorization_request,
                             RemoveKeyRequest* remove_key_request,
                             DBusGMethodInvocation* context);
  virtual gboolean RemoveKeyEx(GArray *account_id,
                               GArray *authorization_request,
                               GArray *remove_key_request,
                               DBusGMethodInvocation *context);
  virtual void DoGetKeyDataEx(AccountIdentifier* account_id,
                              AuthorizationRequest*  authorization_request,
                              GetKeyDataRequest* get_key_data_request,
                              DBusGMethodInvocation* context);
  virtual gboolean GetKeyDataEx(GArray *account_id,
                                GArray *authorization_request,
                                GArray *get_key_data_request,
                                DBusGMethodInvocation *context);
  virtual void DoListKeysEx(AccountIdentifier* account_id,
                            AuthorizationRequest*  authorization_request,
                            ListKeysRequest* list_keys_request,
                            DBusGMethodInvocation* context);
  virtual gboolean ListKeysEx(GArray *account_id,
                              GArray *authorization_request,
                              GArray *list_keys_request,
                              DBusGMethodInvocation *context);
  virtual gboolean Remove(gchar *user,
                          gboolean *OUT_result,
                          GError **error);
  virtual gboolean AsyncRemove(gchar *user,
                               gint *OUT_async_id,
                               GError **error);
  virtual gboolean RenameCryptohome(const GArray* account_id_from,
                                    const GArray* account_id_to,
                                    DBusGMethodInvocation* response);
  virtual gboolean GetAccountDiskUsage(const GArray* account_id,
                                       DBusGMethodInvocation* response);
  virtual gboolean GetSystemSalt(GArray **OUT_salt, GError **error);
  virtual gboolean GetSanitizedUsername(gchar *username,
                                        gchar **OUT_sanitized,
                                        GError **error);
  virtual gboolean IsMountedForUser(gchar *user,
                                    gboolean *OUT_is_mounted,
                                    gboolean *OUT_is_ephemeral_mount,
                                    GError **error);
  virtual gboolean IsMounted(gboolean *OUT_is_mounted, GError **error);
  virtual gboolean Mount(const gchar *user,
                         const gchar *key,
                         gboolean create_if_missing,
                         gboolean ensure_ephemeral,
                         gint *OUT_error_code,
                         gboolean *OUT_result,
                         GError **error);

  // mount_thread_ executed handler for AsyncMount DBus calls.
  // All real work is done here, while the DBus thread merely generates
  // an async_id in |mount_task| and returns it to the caller.
  virtual void  DoAsyncMount(const std::string& userid,
                             SecureBlob *key,
                             bool public_mount,
                             MountTaskMount* mount_task);
  virtual gboolean AsyncMount(
      const gchar *user,
      const gchar *key,
      gboolean create_if_missing,
      gboolean ensure_ephemeral,
      DBusGMethodInvocation *context);
  virtual void DoMountEx(
      AccountIdentifier* identifier,
      AuthorizationRequest* authorization,
      MountRequest* request,
      DBusGMethodInvocation *response);
  virtual void DoRenameCryptohome(AccountIdentifier* id_from,
                                  AccountIdentifier* id_to,
                                  DBusGMethodInvocation* context);
  virtual void DoGetAccountDiskUsage(AccountIdentifier* id,
                                     DBusGMethodInvocation* context);
  virtual gboolean MountEx(
      const GArray *account_id,
      const GArray *authorization_request,
      const GArray *mount_request,
      DBusGMethodInvocation *response);
  virtual gboolean MountGuest(gint *OUT_error_code,
                              gboolean *OUT_result,
                              GError **error);
  virtual gboolean AsyncMountGuest(gint *OUT_async_id,
                                   GError **error);
  virtual gboolean MountPublic(const gchar* public_mount_id,
                               gboolean create_if_missing,
                               gboolean ensure_ephemeral,
                               gint* OUT_error_code,
                               gboolean* OUT_result,
                               GError** error);
  virtual gboolean AsyncMountPublic(const gchar* public_mount_id,
                                    gboolean create_if_missing,
                                    gboolean ensure_ephemeral,
                                    DBusGMethodInvocation* context);
  virtual gboolean Unmount(gboolean *OUT_result, GError **error);
  virtual gboolean UnmountForUser(const gchar* userid, gboolean *OUT_result,
                                  GError **error);
  virtual gboolean DoAutomaticFreeDiskSpaceControl(gboolean *OUT_result,
                                                   GError **error);
  virtual gboolean AsyncDoAutomaticFreeDiskSpaceControl(gint *OUT_async_id,
                                                        GError **error);
  virtual gboolean UpdateCurrentUserActivityTimestamp(gint time_shift_sec,
                                                      GError **error);

  virtual gboolean TpmIsReady(gboolean* OUT_ready, GError** error);
  virtual gboolean TpmIsEnabled(gboolean* OUT_enabled, GError** error);
  virtual gboolean TpmGetPassword(gchar** OUT_password, GError** error);
  virtual gboolean TpmIsOwned(gboolean* OUT_owned, GError** error);
  virtual gboolean TpmIsBeingOwned(gboolean* OUT_owning, GError** error);
  virtual gboolean TpmCanAttemptOwnership(GError** error);
  virtual gboolean TpmClearStoredPassword(GError** error);

  virtual gboolean MigrateToDircrypto(const GArray* account_id,
                                      const GArray* migrate_request,
                                      GError** error);
  // Runs on the mount thread.
  virtual void DoMigrateToDircrypto(
      AccountIdentifier* identifier,
      MigrationType migration_type);

  virtual gboolean NeedsDircryptoMigration(const GArray* account_id,
                                           gboolean* OUT_needs_migration,
                                           GError** error);

  // Attestation functionality is implemented in descendant classes

  // Attestation-related hooks.
  // Called from Service::Initialize() before any other attestation calls.
  virtual void AttestationInitialize() = 0;
  // Called from Service::Initialize() if initialize_tpm_ is true.
  virtual void AttestationInitializeTpm() = 0;
  // Called from Service::OwnershipCallback().
  virtual void AttestationInitializeTpmComplete() = 0;
  // Called from Service::DoGetTpmStatus to fill attestation-related fields.
  virtual void AttestationGetTpmStatus(GetTpmStatusReply* reply) = 0;
  // Called from Service::ResetDictionaryAttackMitigation()
  // Provides the owner delegate credentials normally used for AIK activation.
  // Returns true on success.
  virtual bool AttestationGetDelegateCredentials(
      brillo::SecureBlob* blob,
      brillo::SecureBlob* secret,
      bool* has_reset_lock_permissions) = 0;

  // Attestation-related DBus calls.
  virtual gboolean TpmIsAttestationPrepared(gboolean* OUT_prepared,
                                            GError** error) = 0;
  virtual gboolean TpmVerifyAttestationData(gboolean is_cros_core,
                                            gboolean* OUT_verified,
                                            GError** error) = 0;
  virtual gboolean TpmVerifyEK(gboolean is_cros_core,
                               gboolean* OUT_verified,
                               GError** error) = 0;
  virtual gboolean TpmAttestationCreateEnrollRequest(gint pca_type,
                                                     GArray** OUT_pca_request,
                                                     GError** error) = 0;
  virtual gboolean AsyncTpmAttestationCreateEnrollRequest(gint pca_type,
                                                          gint* OUT_async_id,
                                                          GError** error) = 0;
  virtual gboolean TpmAttestationEnroll(gint pca_type,
                                        GArray* pca_response,
                                        gboolean* OUT_success,
                                        GError** error) = 0;
  virtual gboolean AsyncTpmAttestationEnroll(gint pca_type,
                                             GArray* pca_response,
                                             gint* OUT_async_id,
                                             GError** error) = 0;
  virtual gboolean TpmAttestationCreateCertRequest(
      gint pca_type,
      gint certificate_profile,
      gchar* username,
      gchar* request_origin,
      GArray** OUT_pca_request,
      GError** error) = 0;
  virtual gboolean AsyncTpmAttestationCreateCertRequest(
      gint pca_type,
      gint certificate_profile,
      gchar* username,
      gchar* request_origin,
      gint* OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmAttestationFinishCertRequest(GArray* pca_response,
                                                   gboolean is_user_specific,
                                                   gchar* username,
                                                   gchar* key_name,
                                                   GArray** OUT_cert,
                                                   gboolean* OUT_success,
                                                   GError** error) = 0;
  virtual gboolean AsyncTpmAttestationFinishCertRequest(
      GArray* pca_response,
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gint* OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmIsAttestationEnrolled(gboolean* OUT_is_enrolled,
                                            GError** error) = 0;
  virtual gboolean TpmAttestationDoesKeyExist(gboolean is_user_specific,
                                              gchar* username,
                                              gchar* key_name,
                                              gboolean *OUT_exists,
                                              GError** error) = 0;
  virtual gboolean TpmAttestationGetCertificate(gboolean is_user_specific,
                                                gchar* username,
                                                gchar* key_name,
                                                GArray **OUT_certificate,
                                                gboolean* OUT_success,
                                                GError** error) = 0;
  virtual gboolean TpmAttestationGetPublicKey(gboolean is_user_specific,
                                              gchar* username,
                                              gchar* key_name,
                                              GArray **OUT_public_key,
                                              gboolean* OUT_success,
                                              GError** error) = 0;
  virtual gboolean TpmAttestationRegisterKey(gboolean is_user_specific,
                                             gchar* username,
                                             gchar* key_name,
                                             gint *OUT_async_id,
                                             GError** error) = 0;
  virtual gboolean TpmAttestationSignEnterpriseChallenge(
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gchar* domain,
      GArray* device_id,
      gboolean include_signed_public_key,
      GArray* challenge,
      gint *OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmAttestationSignSimpleChallenge(
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      GArray* challenge,
      gint *OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmAttestationGetKeyPayload(gboolean is_user_specific,
                                               gchar* username,
                                               gchar* key_name,
                                               GArray** OUT_payload,
                                               gboolean* OUT_success,
                                               GError** error) = 0;
  virtual gboolean TpmAttestationSetKeyPayload(gboolean is_user_specific,
                                               gchar* username,
                                               gchar* key_name,
                                               GArray* payload,
                                               gboolean* OUT_success,
                                               GError** error) = 0;
  virtual gboolean TpmAttestationDeleteKeys(gboolean is_user_specific,
                                            gchar* username,
                                            gchar* key_prefix,
                                            gboolean* OUT_success,
                                            GError** error) = 0;
  virtual gboolean TpmAttestationGetEK(gchar** ek_info,
                                       gboolean* OUT_success,
                                       GError** error) = 0;
  virtual gboolean TpmAttestationResetIdentity(gchar* reset_token,
                                               GArray** OUT_reset_request,
                                               gboolean* OUT_success,
                                               GError** error) = 0;
  virtual gboolean TpmGetVersion(gchar** OUT_result,
                                 GError** error);
  virtual gboolean TpmGetVersionStructured(guint32* OUT_family,
                                           guint64* OUT_spec_level,
                                           guint32* OUT_manufacturer,
                                           guint32* OUT_tpm_model,
                                           guint64* OUT_firmware_version,
                                           gchar** OUT_vendor_specific,
                                           GError** error);
  virtual gboolean GetEndorsementInfo(const GArray* request,
                                      DBusGMethodInvocation* context) = 0;
  virtual gboolean InitializeCastKey(const GArray* request,
                                     DBusGMethodInvocation* context) = 0;

  // Returns the label of the TPM token along with its user PIN.
  virtual gboolean Pkcs11GetTpmTokenInfo(gchar** OUT_label,
                                         gchar** OUT_user_pin,
                                         gint* OUT_slot,
                                         GError** error);
  // Returns the label of the TPM token along with its user PIN.
  virtual gboolean Pkcs11GetTpmTokenInfoForUser(gchar *username,
                                                gchar** OUT_label,
                                                gchar** OUT_user_pin,
                                                gint* OUT_slot,
                                                GError** error);

  // Returns in |OUT_ready| whether the TPM token is ready for use.
  virtual gboolean Pkcs11IsTpmTokenReady(gboolean* OUT_ready, GError** error);
  virtual gboolean Pkcs11IsTpmTokenReadyForUser(gchar *username,
                                                gboolean* OUT_ready,
                                                GError** error);
  virtual gboolean Pkcs11Terminate(gchar* username, GError** error);
  virtual gboolean GetStatusString(gchar** OUT_status, GError** error);

  // InstallAttributes methods
  virtual gboolean InstallAttributesGet(gchar* name,
                              GArray** OUT_value,
                              gboolean* OUT_successful,
                              GError** error);
  virtual gboolean InstallAttributesSet(gchar* name,
                              GArray* value,
                              gboolean* OUT_successful,
                              GError** error);
  virtual gboolean InstallAttributesFinalize(gboolean* OUT_finalized,
                                             GError** error);
  virtual gboolean InstallAttributesCount(gint* OUT_count, GError** error);
  virtual gboolean InstallAttributesIsReady(gboolean* OUT_ready,
                                            GError** error);
  virtual gboolean InstallAttributesIsSecure(gboolean* OUT_secure,
                                             GError** error);
  virtual gboolean InstallAttributesIsInvalid(gboolean* OUT_invalid,
                                              GError** error);
  virtual gboolean InstallAttributesIsFirstInstall(gboolean* OUT_first_install,
                                                   GError** error);

  virtual gboolean StoreEnrollmentState(GArray* enrollment_state,
                                        gboolean* OUT_success,
                                        GError** error);

  virtual gboolean LoadEnrollmentState(GArray** OUT_enrollment_state,
                                       gboolean* OUT_success,
                                       GError** error);
  // Runs on the mount thread.
  virtual void DoSignBootLockbox(const brillo::SecureBlob& request,
                                 DBusGMethodInvocation* context);
  virtual gboolean SignBootLockbox(const GArray* request,
                                   DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoVerifyBootLockbox(const brillo::SecureBlob& request,
                                   DBusGMethodInvocation* context);
  virtual gboolean VerifyBootLockbox(const GArray* request,
                                     DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoFinalizeBootLockbox(const brillo::SecureBlob& request,
                                     DBusGMethodInvocation* context);
  virtual gboolean FinalizeBootLockbox(const GArray* request,
                                       DBusGMethodInvocation* context);

  // Runs on the mount thread.
  virtual void DoGetBootAttribute(const brillo::SecureBlob& request,
                                  DBusGMethodInvocation* context);
  virtual gboolean GetBootAttribute(const GArray* request,
                                    DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoSetBootAttribute(const brillo::SecureBlob& request,
                                  DBusGMethodInvocation* context);
  virtual gboolean SetBootAttribute(const GArray* request,
                                    DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoFlushAndSignBootAttributes(const brillo::SecureBlob& request,
                                            DBusGMethodInvocation* context);
  virtual gboolean FlushAndSignBootAttributes(const GArray* request,
                                              DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoGetLoginStatus(const brillo::SecureBlob& request,
                                DBusGMethodInvocation* context);
  virtual gboolean GetLoginStatus(const GArray* request,
                                  DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoGetTpmStatus(const brillo::SecureBlob& request,
                              DBusGMethodInvocation* context);
  virtual gboolean GetTpmStatus(const GArray* request,
                                DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoGetFirmwareManagementParameters(
      const brillo::SecureBlob& request,
      DBusGMethodInvocation* context);
  virtual gboolean GetFirmwareManagementParameters(const GArray* request,
                           DBusGMethodInvocation* context);

  // Runs on the mount thread.
  virtual void DoSetFirmwareManagementParameters(
      const brillo::SecureBlob& request,
      DBusGMethodInvocation* context);
  virtual gboolean SetFirmwareManagementParameters(const GArray* request,
                           DBusGMethodInvocation* context);

  // Runs on the mount thread.
  virtual void DoRemoveFirmwareManagementParameters(
      const brillo::SecureBlob& request,
      DBusGMethodInvocation* context);
  virtual gboolean RemoveFirmwareManagementParameters(const GArray* request,
                              DBusGMethodInvocation* context);

 protected:
  FRIEND_TEST(ServiceTestNotInitialized, CheckAsyncTestCredentials);
  FRIEND_TEST(ServiceTest, StoreEnrollmentState);
  FRIEND_TEST(ServiceTest, LoadEnrollmentState);
  FRIEND_TEST(ServiceTest, NoDeadlocksInInitializeTpmComplete);

  bool use_tpm_;

  GMainLoop* loop_;
  // Can't use unique_ptr for cryptohome_ because memory is allocated by glib.
  gobject::Cryptohome* cryptohome_;
  brillo::SecureBlob system_salt_;
  std::unique_ptr<cryptohome::Platform> default_platform_;
  cryptohome::Platform* platform_;
  std::unique_ptr<cryptohome::Crypto> default_crypto_;
  cryptohome::Crypto* crypto_;
  // TPM doesn't use the unique_ptr for default pattern, since the tpm is a
  // singleton - we don't want it getting destroyed when we are.
  Tpm* tpm_;
  std::unique_ptr<TpmInit> default_tpm_init_;
  TpmInit* tpm_init_;
  std::unique_ptr<Pkcs11Init> default_pkcs11_init_;
  Pkcs11Init* pkcs11_init_;
  bool initialize_tpm_;
  base::Thread mount_thread_;
  guint async_complete_signal_;
  // A completion signal for async calls that return data.
  guint async_data_complete_signal_;
  guint tpm_init_signal_;
  guint low_disk_space_signal_;
  guint dircrypto_migration_progress_signal_;
  CryptohomeEventSource event_source_;
  CryptohomeEventSourceSink* event_source_sink_;
  int auto_cleanup_period_;
  std::unique_ptr<cryptohome::InstallAttributes> default_install_attrs_;
  cryptohome::InstallAttributes* install_attrs_;
  int update_user_activity_period_;
  // Keeps track of whether a failure on PKCS#11 initialization was reported
  // during this user login. We use this not to report a same failure multiple
  // times.
  bool reported_pkcs11_init_fail_;
  // Keeps track of whether the device is enterprise-owned.
  bool enterprise_owned_;

  virtual GMainLoop *main_loop() { return loop_; }

  // Called periodically on Mount thread to initiate automatic disk
  // cleanup if needed.
  virtual void AutoCleanupCallback();
  // Called periodically on Mount thread to detect low disk space and emit a
  // signal if detected.
  virtual void LowDiskCallback();
  // Returns true if there are any existing mounts and populates
  // |mounts| with the mount point.
  virtual bool GetExistingMounts(
      std::multimap<const base::FilePath, const base::FilePath>* mounts);

  // Checks if the machine is enterprise owned and report to mount_ then.
  virtual void DetectEnterpriseOwnership();

  // Runs the event loop once. Only for testing.
  virtual void DispatchEvents();

  virtual scoped_refptr<cryptohome::Mount> GetMountForUser(
      const std::string& username);

  // Ensures only one Mount is ever created per username.
  virtual scoped_refptr<cryptohome::Mount> GetOrCreateMountForUser(
      const std::string& username);

  // Safely removes the MountMap reference for the given Mount.
  virtual bool RemoveMountForUser(const std::string& username);

  // Safelt removes the given Mount from MountMap.
  virtual void RemoveMount(cryptohome::Mount* mount);

  // Safely empties the MountMap and may request unmounting. If |unmount| is
  // true, the return value will reflect if all mounts unmounted cleanly or not.
  virtual bool RemoveAllMounts(bool unmount);

  // Unload any pkcs11 tokens _not_ belonging to one of the mounts in |exclude|.
  // This is used to clean up any stale loaded tokens after a cryptohome crash.
  virtual bool UnloadPkcs11Tokens(const std::vector<base::FilePath>& exclude);

  // Posts a message back from the mount_thread_ to the main thread to
  // reply to a DBus message.  Only call from mount_thread_-based
  // functions!
  virtual void SendReply(DBusGMethodInvocation* context,
                         const BaseReply& reply);

  // Helper methods that post a message back to the main thread where
  // a DBus InvalidArgs GError is returned to the caller.
  // Only call from mount_thread_-based functions!
  virtual void SendDBusErrorReply(DBusGMethodInvocation* context,
                                  GQuark domain,
                                  gint code,
                                  const gchar* message);
  virtual void SendInvalidArgsReply(DBusGMethodInvocation* context,
                                    const char* message) {
    SendDBusErrorReply(context,
                       DBUS_GERROR, DBUS_GERROR_INVALID_ARGS,
                       message);
  }
  virtual void SendFailureReply(DBusGMethodInvocation* context,
                                const char* message) {
    SendDBusErrorReply(context,
                       DBUS_GERROR, DBUS_GERROR_FAILED,
                       message);
  }
  virtual void SendNotSupportedReply(DBusGMethodInvocation* context,
                                     const char* message) {
    SendDBusErrorReply(context,
                       DBUS_GERROR, DBUS_GERROR_NOT_SUPPORTED,
                       message);
  }

  // Returns a CryptohomeErrorCode for an internal Mount::MountError code.
  virtual CryptohomeErrorCode MountErrorToCryptohomeError(
      const MountError code) const;

  // Posts a message back from the mount_thread_ to the main thread to
  // reply to a DBus message that still uses async_id-based responses.
  // Only call from mount_thread_ and do not add new DBus methods using
  // async_ids.
  virtual void SendLegacyAsyncReply(MountTaskMount* mount_task,
                                    MountError return_code,
                                    bool return_status);

  // Sends a signal for notifying the migration progress.
  // Runs on the mount thread.
  virtual void SendDircryptoMigrationProgressSignal(
      DircryptoMigrationStatus status,
      uint64_t current_bytes,
      uint64_t total_bytes);

  // Stop processing tasks on dbus and mount threads.
  // Must be called from derived destructors. Otherwise, after derived
  // destructor, all pure virtual functions from Service overloaded there and
  // all members defined for that class will be gone, while mount_thread_
  // will continue running tasks until stopped in ~Service.
  void StopTasks();

  // Get system salt (create, if doesn't exist yet)
  bool GetSystemSalt(brillo::SecureBlob* system_salt) {
    return homedirs_->GetSystemSalt(system_salt);
  }

 private:
  FRIEND_TEST(ServiceTest, GetPublicMountPassKey);

  std::unique_ptr<SecureBlob> GetAttestationBasedEnrollmentData();

  bool CreateSystemSaltIfNeeded();
  bool CreatePublicMountSaltIfNeeded();

  // Gets passkey for |public_mount_id|. Returns true if a passkey is generated
  // successfully. Otherwise, returns false.
  bool GetPublicMountPassKey(const std::string& public_mount_id,
                             std::string* public_mount_passkey);

  // Creates a MountTaskNop that uses |bridge| to return |return_code| and
  // |return_status| for async calls. Returns the sequence id of the created
  // MountTaskNop.
  int PostAsyncCallResult(MountTaskObserver* bridge,
                          MountError return_code,
                          bool return_status);

  // Posts the mount_task and failure code back to the main
  // thread for migrated legacy calls.
  void PostAsyncCallResultForUser(const std::string& user_id,
                                  MountTaskMount* mount_task,
                                  MountError return_code,
                                  bool return_status);

  // Called on Mount thread. This method calls ReportDictionaryAttackResetStatus
  // exactly once (i.e. records one sample) with the status of the operation.
  void ResetDictionaryAttackMitigation();

  // Sets and gets global flag indicating if EULA has been accepted and
  // cryptohomed can attempt TPM ownership. This is a one-time decision
  // until the owner is changed, so the flag is preserved over reboots and
  // is cleared by the powerwash.
  void SetCanAttemptOwnership();
  bool CanAttemptOwnership() const;

  // Tracks Mount objects for each user by username.
  typedef std::map<const std::string, scoped_refptr<cryptohome::Mount>>
      MountMap;
  MountMap mounts_;
  base::Lock mounts_lock_;  // Protects against parallel insertions only.
  std::unique_ptr<UserOldestActivityTimestampCache> user_timestamp_cache_;
  std::unique_ptr<cryptohome::MountFactory> default_mount_factory_;
  cryptohome::MountFactory* mount_factory_;
  std::unique_ptr<cryptohome::DBusReplyFactory> default_reply_factory_;
  cryptohome::DBusReplyFactory* reply_factory_;

  typedef std::map<int, scoped_refptr<MountTaskPkcs11Init>> Pkcs11TaskMap;
  Pkcs11TaskMap pkcs11_tasks_;
  std::unique_ptr<HomeDirs> default_homedirs_;
  HomeDirs* homedirs_;
  std::string guest_user_;
  bool force_ecryptfs_;
  bool legacy_mount_;
  brillo::SecureBlob public_mount_salt_;
  std::unique_ptr<chaps::TokenManagerClient> default_chaps_client_;
  chaps::TokenManagerClient* chaps_client_;
  std::unique_ptr<BootLockbox> default_boot_lockbox_;
  // After construction, this should only be used on the mount thread.
  BootLockbox* boot_lockbox_;
  std::unique_ptr<BootAttributes> default_boot_attributes_;
  // After construction, this should only be used on the mount thread.
  BootAttributes* boot_attributes_;
  std::unique_ptr<FirmwareManagementParameters>
      default_firmware_management_params_;
  FirmwareManagementParameters* firmware_management_parameters_;
  int low_disk_notification_period_ms_;

  DISALLOW_COPY_AND_ASSIGN(Service);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SERVICE_H_

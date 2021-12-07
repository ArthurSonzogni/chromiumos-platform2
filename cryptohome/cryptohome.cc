// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Cryptohome client that uses the dbus client interface

#include <inttypes.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <attestation/proto_bindings/interface.pb.h>
#include <attestation-client/attestation/dbus-proxies.h>
#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <chromeos/constants/cryptohome.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <google/protobuf/message_lite.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "cryptohome/attestation.pb.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/platform.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/signed_secret.pb.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/timestamp.pb.h"
#include "cryptohome/UserDataAuth.pb.h"
#include "cryptohome/vault_keyset.pb.h"
#include "user_data_auth/dbus-proxies.h"
// The dbus_adaptor and proxy include must happen after the protobuf include

using base::FilePath;
using base::StringPrintf;
using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace {
// Number of days that the set_current_user_old action uses when updating the
// home directory timestamp.  ~3 months should be old enough for test purposes.
const int kSetCurrentUserOldOffsetInDays = 92;

// Five minutes is enough to wait for any TPM operations, sync() calls, etc.
const int kDefaultTimeoutMs = 300000;

// We've 100 seconds to wait for TakeOwnership(), should be rather generous.
constexpr int kWaitOwnershipTimeoutInSeconds = 100;

// Poll once every 0.2s.
constexpr int kWaitOwnershipPollIntervalInMs = 200;

// Converts a brillo::Error* to string for printing.
std::string BrilloErrorToString(brillo::Error* err) {
  std::string result;
  if (err) {
    result = "(" + err->GetDomain() + ", " + err->GetCode() + ", " +
             err->GetMessage() + ")";
  } else {
    result = "(null)";
  }
  return result;
}

}  // namespace

namespace switches {
static const char kSyslogSwitch[] = "syslog";
static const char kAttestationServerSwitch[] = "attestation-server";
static struct {
  const char* name;
  const attestation::ACAType aca_type;
} kAttestationServers[] = {{"default", attestation::DEFAULT_ACA},
                           {"test", attestation::TEST_ACA}};
static const char kVaServerSwitch[] = "va-server";
static struct {
  const char* name;
  const attestation::VAType va_type;
} kVaServers[] = {{"default", attestation::DEFAULT_VA},
                  {"test", attestation::TEST_VA}};
static const char kWaitOwnershipTimeoutSwitch[] = "wait-ownership-timeout";
static const char kActionSwitch[] = "action";
static const char* kActions[] = {"mount_ex",
                                 "mount_guest_ex",
                                 "unmount",
                                 "is_mounted",
                                 "check_key_ex",
                                 "remove_key_ex",
                                 "get_key_data_ex",
                                 "list_keys_ex",
                                 "migrate_key_ex",
                                 "add_key_ex",
                                 "mass_remove_keys",
                                 "update_key_ex",
                                 "remove",
                                 "obfuscate_user",
                                 "get_system_salt",
                                 "dump_keyset",
                                 "dump_last_activity",
                                 "tpm_status",
                                 "tpm_more_status",
                                 "status",
                                 "set_current_user_old",
                                 "tpm_take_ownership",
                                 "tpm_clear_stored_password",
                                 "tpm_wait_ownership",
                                 "install_attributes_set",
                                 "install_attributes_get",
                                 "install_attributes_finalize",
                                 "install_attributes_count",
                                 "install_attributes_get_status",
                                 "install_attributes_is_ready",
                                 "install_attributes_is_secure",
                                 "install_attributes_is_invalid",
                                 "install_attributes_is_first_install",
                                 "pkcs11_get_user_token_info",
                                 "pkcs11_get_system_token_info",
                                 "pkcs11_is_user_token_ok",
                                 "pkcs11_terminate",
                                 "pkcs11_restore_tpm_tokens",
                                 "tpm_verify_attestation",
                                 "tpm_verify_ek",
                                 "tpm_attestation_status",
                                 "tpm_attestation_more_status",
                                 "tpm_attestation_start_enroll",
                                 "tpm_attestation_finish_enroll",
                                 "tpm_attestation_enroll",
                                 "tpm_attestation_start_cert_request",
                                 "tpm_attestation_finish_cert_request",
                                 "tpm_attestation_get_certificate",
                                 "tpm_attestation_key_status",
                                 "tpm_attestation_register_key",
                                 "tpm_attestation_enterprise_challenge",
                                 "tpm_attestation_simple_challenge",
                                 "tpm_attestation_get_key_payload",
                                 "tpm_attestation_set_key_payload",
                                 "tpm_attestation_delete_keys",
                                 "tpm_attestation_delete_key",
                                 "tpm_attestation_get_ek",
                                 "tpm_attestation_reset_identity",
                                 "tpm_attestation_reset_identity_result",
                                 "sign_lockbox",
                                 "verify_lockbox",
                                 "finalize_lockbox",
                                 "get_boot_attribute",
                                 "set_boot_attribute",
                                 "flush_and_sign_boot_attributes",
                                 "get_login_status",
                                 "initialize_cast_key",
                                 "get_firmware_management_parameters",
                                 "set_firmware_management_parameters",
                                 "remove_firmware_management_parameters",
                                 "migrate_to_dircrypto",
                                 "needs_dircrypto_migration",
                                 "get_enrollment_id",
                                 "get_supported_key_policies",
                                 "get_account_disk_usage",
                                 "lock_to_single_user_mount_until_reboot",
                                 "get_rsu_device_id",
                                 "check_health",
                                 "start_fingerprint_auth_session",
                                 "end_fingerprint_auth_session",
                                 "start_auth_session",
                                 "add_credentials",
                                 "authenticate_auth_session",
                                 "invalidate_auth_session",
                                 "extend_auth_session",
                                 "create_persistent_user",
                                 "prepare_guest_vault",
                                 "prepare_ephemeral_vault",
                                 "prepare_persistent_vault",
                                 "prepare_vault_for_migration",
                                 "add_auth_factor",
                                 "authenticate_auth_factor",
                                 NULL};
enum ActionEnum {
  ACTION_MOUNT_EX,
  ACTION_MOUNT_GUEST_EX,
  ACTION_UNMOUNT,
  ACTION_MOUNTED,
  ACTION_CHECK_KEY_EX,
  ACTION_REMOVE_KEY_EX,
  ACTION_GET_KEY_DATA_EX,
  ACTION_LIST_KEYS_EX,
  ACTION_MIGRATE_KEY_EX,
  ACTION_ADD_KEY_EX,
  ACTION_MASS_REMOVE_KEYS,
  ACTION_UPDATE_KEY_EX,
  ACTION_REMOVE,
  ACTION_OBFUSCATE_USER,
  ACTION_GET_SYSTEM_SALT,
  ACTION_DUMP_KEYSET,
  ACTION_DUMP_LAST_ACTIVITY,
  ACTION_TPM_STATUS,
  ACTION_TPM_MORE_STATUS,
  ACTION_STATUS,
  ACTION_SET_CURRENT_USER_OLD,
  ACTION_TPM_TAKE_OWNERSHIP,
  ACTION_TPM_CLEAR_STORED_PASSWORD,
  ACTION_TPM_WAIT_OWNERSHIP,
  ACTION_INSTALL_ATTRIBUTES_SET,
  ACTION_INSTALL_ATTRIBUTES_GET,
  ACTION_INSTALL_ATTRIBUTES_FINALIZE,
  ACTION_INSTALL_ATTRIBUTES_COUNT,
  ACTION_INSTALL_ATTRIBUTES_GET_STATUS,
  ACTION_INSTALL_ATTRIBUTES_IS_READY,
  ACTION_INSTALL_ATTRIBUTES_IS_SECURE,
  ACTION_INSTALL_ATTRIBUTES_IS_INVALID,
  ACTION_INSTALL_ATTRIBUTES_IS_FIRST_INSTALL,
  ACTION_PKCS11_GET_USER_TOKEN_INFO,
  ACTION_PKCS11_GET_SYSTEM_TOKEN_INFO,
  ACTION_PKCS11_IS_USER_TOKEN_OK,
  ACTION_PKCS11_TERMINATE,
  ACTION_PKCS11_RESTORE_TPM_TOKENS,
  ACTION_TPM_VERIFY_ATTESTATION,
  ACTION_TPM_VERIFY_EK,
  ACTION_TPM_ATTESTATION_STATUS,
  ACTION_TPM_ATTESTATION_MORE_STATUS,
  ACTION_TPM_ATTESTATION_START_ENROLL,
  ACTION_TPM_ATTESTATION_FINISH_ENROLL,
  ACTION_TPM_ATTESTATION_ENROLL,
  ACTION_TPM_ATTESTATION_START_CERTREQ,
  ACTION_TPM_ATTESTATION_FINISH_CERTREQ,
  ACTION_TPM_ATTESTATION_GET_CERTIFICATE,
  ACTION_TPM_ATTESTATION_KEY_STATUS,
  ACTION_TPM_ATTESTATION_REGISTER_KEY,
  ACTION_TPM_ATTESTATION_ENTERPRISE_CHALLENGE,
  ACTION_TPM_ATTESTATION_SIMPLE_CHALLENGE,
  ACTION_TPM_ATTESTATION_GET_KEY_PAYLOAD,
  ACTION_TPM_ATTESTATION_SET_KEY_PAYLOAD,
  ACTION_TPM_ATTESTATION_DELETE_KEYS,
  ACTION_TPM_ATTESTATION_DELETE_KEY,
  ACTION_TPM_ATTESTATION_GET_EK,
  ACTION_TPM_ATTESTATION_RESET_IDENTITY,
  ACTION_TPM_ATTESTATION_RESET_IDENTITY_RESULT,
  ACTION_SIGN_LOCKBOX,
  ACTION_VERIFY_LOCKBOX,
  ACTION_FINALIZE_LOCKBOX,
  ACTION_GET_BOOT_ATTRIBUTE,
  ACTION_SET_BOOT_ATTRIBUTE,
  ACTION_FLUSH_AND_SIGN_BOOT_ATTRIBUTES,
  ACTION_GET_LOGIN_STATUS,
  ACTION_INITIALIZE_CAST_KEY,
  ACTION_GET_FIRMWARE_MANAGEMENT_PARAMETERS,
  ACTION_SET_FIRMWARE_MANAGEMENT_PARAMETERS,
  ACTION_REMOVE_FIRMWARE_MANAGEMENT_PARAMETERS,
  ACTION_MIGRATE_TO_DIRCRYPTO,
  ACTION_NEEDS_DIRCRYPTO_MIGRATION,
  ACTION_GET_ENROLLMENT_ID,
  ACTION_GET_SUPPORTED_KEY_POLICIES,
  ACTION_GET_ACCOUNT_DISK_USAGE,
  ACTION_LOCK_TO_SINGLE_USER_MOUNT_UNTIL_REBOOT,
  ACTION_GET_RSU_DEVICE_ID,
  ACTION_CHECK_HEALTH,
  ACTION_START_FINGERPRINT_AUTH_SESSION,
  ACTION_END_FINGERPRINT_AUTH_SESSION,
  ACTION_START_AUTH_SESSION,
  ACTION_ADD_CREDENTIALS,
  ACTION_AUTHENTICATE_AUTH_SESSION,
  ACTION_INVALIDATE_AUTH_SESSION,
  ACTION_EXTEND_AUTH_SESSION,
  ACTION_CREATE_PERSISTENT_USER,
  ACTION_PREPARE_GUEST_VAULT,
  ACTION_PREPARE_EPHEMERAL_VAULT,
  ACTION_PREPARE_PERSISTENT_VAULT,
  ACTION_PREPARE_VAULT_FOR_MIGRATION,
  ACTION_ADD_AUTH_FACTOR,
  ACTION_AUTHENTICATE_AUTH_FACTOR
};
static const char kUserSwitch[] = "user";
static const char kPasswordSwitch[] = "password";
static const char kFingerprintSwitch[] = "fingerprint";
static const char kKeyLabelSwitch[] = "key_label";
static const char kNewKeyLabelSwitch[] = "new_key_label";
static const char kRemoveKeyLabelSwitch[] = "remove_key_label";
static const char kOldPasswordSwitch[] = "old_password";
static const char kNewPasswordSwitch[] = "new_password";
static const char kForceSwitch[] = "force";
static const char kCreateSwitch[] = "create";
static const char kAttrNameSwitch[] = "name";
static const char kAttrPrefixSwitch[] = "prefix";
static const char kAttrValueSwitch[] = "value";
static const char kFileSwitch[] = "file";
static const char kInputFileSwitch[] = "input";
static const char kOutputFileSwitch[] = "output";
static const char kEnsureEphemeralSwitch[] = "ensure_ephemeral";
static const char kCrosCoreSwitch[] = "cros_core";
static const char kFlagsSwitch[] = "flags";
static const char kDevKeyHashSwitch[] = "developer_key_hash";
static const char kEcryptfsSwitch[] = "ecryptfs";
static const char kToMigrateFromEcryptfsSwitch[] = "to_migrate_from_ecryptfs";
static const char kMinimalMigration[] = "minimal_migration";
static const char kPublicMount[] = "public_mount";
static const char kKeyPolicySwitch[] = "key_policy";
static const char kKeyPolicyLECredential[] = "le";
static const char kProfileSwitch[] = "profile";
static const char kIgnoreCache[] = "ignore_cache";
static const char kRestoreKeyInHexSwitch[] = "restore_key_in_hex";
static const char kMassRemoveExemptLabelsSwitch[] = "exempt_key_labels";
static const char kUseDBus[] = "use_dbus";
static const char kAuthSessionId[] = "auth_session_id";
static const char kChallengeAlgorithm[] = "challenge_alg";
static const char kChallengeSPKI[] = "challenge_spki";
static const char kKeyDelegateName[] = "key_delegate_name";
static const char kKeyDelegatePath[] = "key_delegate_path";
static const char kExtensionDuration[] = "extension_duration";
}  // namespace switches

brillo::SecureBlob GetSystemSalt(
    org::chromium::CryptohomeMiscInterfaceProxy* proxy) {
  user_data_auth::GetSystemSaltRequest req;
  user_data_auth::GetSystemSaltReply reply;
  brillo::ErrorPtr error;
  if (!proxy->GetSystemSalt(req, &reply, &error, kDefaultTimeoutMs) || error) {
    LOG(ERROR) << "GetSystemSalt failed: " << BrilloErrorToString(error.get());
    return brillo::SecureBlob();
  }
  brillo::SecureBlob system_salt(reply.salt());
  return system_salt;
}

bool GetAttrName(const base::CommandLine* cl, std::string* name_out) {
  *name_out = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);

  if (name_out->length() == 0) {
    printf("No install attribute name specified (--name=<name>)\n");
    return false;
  }
  return true;
}

bool GetAttrValue(const base::CommandLine* cl, std::string* value_out) {
  *value_out = cl->GetSwitchValueASCII(switches::kAttrValueSwitch);

  if (value_out->length() == 0) {
    printf("No install attribute value specified (--value=<value>)\n");
    return false;
  }
  return true;
}

bool GetAccountId(const base::CommandLine* cl, std::string* user_out) {
  *user_out = cl->GetSwitchValueASCII(switches::kUserSwitch);

  if (user_out->length() == 0) {
    printf("No user specified (--user=<account_id>)\n");
    return false;
  }
  return true;
}

bool GetAuthSessionId(const base::CommandLine* cl,
                      std::string* session_id_out) {
  *session_id_out = cl->GetSwitchValueASCII(switches::kAuthSessionId);

  if (session_id_out->length() == 0) {
    printf(
        "No auth_session_id specified (--auth_session_id=<auth_session_id>)\n");
    return false;
  }
  return true;
}

bool GetPassword(org::chromium::CryptohomeMiscInterfaceProxy* proxy,
                 const base::CommandLine* cl,
                 const std::string& cl_switch,
                 const std::string& prompt,
                 std::string* password_out) {
  std::string password = cl->GetSwitchValueASCII(cl_switch);

  if (password.length() == 0) {
    char buffer[256];
    struct termios original_attr;
    struct termios new_attr;
    tcgetattr(0, &original_attr);
    memcpy(&new_attr, &original_attr, sizeof(new_attr));
    new_attr.c_lflag &= ~(ECHO);
    tcsetattr(0, TCSANOW, &new_attr);
    printf("%s: ", prompt.c_str());
    fflush(stdout);
    if (fgets(buffer, std::size(buffer), stdin))
      password = buffer;
    printf("\n");
    tcsetattr(0, TCSANOW, &original_attr);
  }

  std::string trimmed_password;
  base::TrimString(password, "\r\n", &trimmed_password);
  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(trimmed_password.c_str(),
                                        GetSystemSalt(proxy), &passkey);
  *password_out = passkey.to_string();

  return true;
}

bool IsMixingOldAndNewFileSwitches(const base::CommandLine* cl) {
  return cl->HasSwitch(switches::kFileSwitch) &&
         (cl->HasSwitch(switches::kInputFileSwitch) ||
          cl->HasSwitch(switches::kOutputFileSwitch));
}

FilePath GetFile(const base::CommandLine* cl) {
  const char kDefaultFilePath[] = "/tmp/__cryptohome";
  FilePath file_path(cl->GetSwitchValueASCII(switches::kFileSwitch));
  if (file_path.empty()) {
    return FilePath(kDefaultFilePath);
  }
  return file_path;
}

FilePath GetInputFile(const base::CommandLine* cl) {
  FilePath file_path(cl->GetSwitchValueASCII(switches::kInputFileSwitch));
  if (file_path.empty()) {
    return GetFile(cl);
  }
  return file_path;
}

FilePath GetOutputFile(const base::CommandLine* cl) {
  FilePath file_path(cl->GetSwitchValueASCII(switches::kOutputFileSwitch));
  if (file_path.empty()) {
    return GetFile(cl);
  }
  return file_path;
}

bool GetProfile(const base::CommandLine* cl,
                attestation::CertificateProfile* profile) {
  const std::string profile_str =
      cl->GetSwitchValueASCII(switches::kProfileSwitch);
  if (profile_str.empty() || profile_str == "enterprise_user" ||
      profile_str == "user" || profile_str == "u") {
    *profile = attestation::CertificateProfile::ENTERPRISE_USER_CERTIFICATE;
  } else if (profile_str == "enterprise_machine" || profile_str == "machine" ||
             profile_str == "m") {
    *profile = attestation::CertificateProfile::ENTERPRISE_MACHINE_CERTIFICATE;
  } else if (profile_str == "enterprise_enrollment" ||
             profile_str == "enrollment" || profile_str == "e") {
    *profile =
        attestation::CertificateProfile::ENTERPRISE_ENROLLMENT_CERTIFICATE;
  } else if (profile_str == "content_protection" || profile_str == "content" ||
             profile_str == "c") {
    *profile = attestation::CertificateProfile::CONTENT_PROTECTION_CERTIFICATE;
  } else if (profile_str == "content_protection_with_stable_id" ||
             profile_str == "cpsi") {
    *profile = attestation::CertificateProfile::
        CONTENT_PROTECTION_CERTIFICATE_WITH_STABLE_ID;
  } else if (profile_str == "cast") {
    *profile = attestation::CertificateProfile::CAST_CERTIFICATE;
  } else if (profile_str == "gfsc") {
    *profile = attestation::CertificateProfile::GFSC_CERTIFICATE;
  } else if (profile_str == "jetstream") {
    *profile = attestation::CertificateProfile::JETSTREAM_CERTIFICATE;
  } else if (profile_str == "soft_bind") {
    *profile = attestation::CertificateProfile::SOFT_BIND_CERTIFICATE;
  } else {
    printf("Unknown certificate profile: %s.\n", profile_str.c_str());
    return false;
  }
  return true;
}

bool ConfirmRemove(const std::string& user) {
  printf("!!! Are you sure you want to remove the user's cryptohome?\n");
  printf("!!!\n");
  printf("!!! Re-enter the username at the prompt to remove the\n");
  printf("!!! cryptohome for the user.\n");
  printf("Enter the username <%s>: ", user.c_str());
  fflush(stdout);

  char buffer[256];
  if (!fgets(buffer, std::size(buffer), stdin)) {
    printf("Error while reading username.\n");
    return false;
  }
  std::string verification = buffer;
  // fgets will append the newline character, remove it.
  base::TrimWhitespaceASCII(verification, base::TRIM_ALL, &verification);
  if (user != verification) {
    printf("Usernames do not match.\n");
    return false;
  }
  return true;
}

bool BuildAccountId(base::CommandLine* cl, cryptohome::AccountIdentifier* id) {
  std::string account_id;
  if (!GetAccountId(cl, &account_id)) {
    printf("No account_id specified.\n");
    return false;
  }
  id->set_account_id(account_id);
  return true;
}

bool BuildAuthorization(base::CommandLine* cl,
                        org::chromium::CryptohomeMiscInterfaceProxy* proxy,
                        bool need_credential,
                        cryptohome::AuthorizationRequest* auth) {
  if (need_credential) {
    if (cl->HasSwitch(switches::kChallengeAlgorithm) ||
        cl->HasSwitch(switches::kChallengeSPKI) ||
        cl->HasSwitch(switches::kKeyDelegateName) ||
        cl->HasSwitch(switches::kKeyDelegatePath)) {
      // We're doing challenge response auth.
      // Parameters for challenge response auth:
      // --challenge_alg=<Algorithm>: See ChallengeSignatureAlgorithm in
      //   key.proto for valid values.
      //   Example: "CHALLENGE_RSASSA_PKCS1_V1_5_SHA1".
      // --challenge_spki=<DER Encoded SPKI Public Key in hex>
      // --key_delegate_name=<Key Delegate DBus Service Name>
      // --key_delegate_path=<Key Delegate DBus Object Path>

      // Check that all parameters are supplied.
      if (!(cl->HasSwitch(switches::kChallengeAlgorithm) &&
            cl->HasSwitch(switches::kChallengeSPKI) &&
            cl->HasSwitch(switches::kKeyDelegateName) &&
            cl->HasSwitch(switches::kKeyDelegatePath))) {
        printf(
            "One or more of the switches for challenge response auth is "
            "missing.\n");
        return false;
      }

      auth->mutable_key()->mutable_data()->set_type(
          cryptohome::KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

      cryptohome::ChallengeSignatureAlgorithm challenge_alg;
      if (!ChallengeSignatureAlgorithm_Parse(
              cl->GetSwitchValueASCII(switches::kChallengeAlgorithm),
              &challenge_alg)) {
        printf("Invalid challenge response algorithm.\n");
        return false;
      }
      auto* challenge_response_key = auth->mutable_key()
                                         ->mutable_data()
                                         ->mutable_challenge_response_key()
                                         ->Add();
      challenge_response_key->add_signature_algorithm(challenge_alg);

      std::string challenge_spki;
      if (!base::HexStringToString(
              cl->GetSwitchValueASCII(switches::kChallengeSPKI),
              &challenge_spki)) {
        printf("Challenge SPKI Public Key DER is not hex encoded.\n");
        return false;
      }
      challenge_response_key->set_public_key_spki_der(challenge_spki);

      auth->mutable_key_delegate()->set_dbus_service_name(
          cl->GetSwitchValueASCII(switches::kKeyDelegateName));
      auth->mutable_key_delegate()->set_dbus_object_path(
          cl->GetSwitchValueASCII(switches::kKeyDelegatePath));
    } else if (cl->HasSwitch(switches::kRestoreKeyInHexSwitch)) {
      // Restore key is provided
      brillo::SecureBlob raw_byte(
          cl->GetSwitchValueASCII(switches::kRestoreKeyInHexSwitch));
      if (raw_byte.to_string().length() == 0) {
        printf("No hex string specified\n");
        return false;
      }
      SecureBlob::HexStringToSecureBlob(raw_byte.to_string(), &raw_byte);
      auth->mutable_key()->set_secret(raw_byte.to_string());
    } else {
      std::string password;
      GetPassword(proxy, cl, switches::kPasswordSwitch, "Enter the password",
                  &password);

      auth->mutable_key()->set_secret(password);
    }
  } else {
    auth->mutable_key()->mutable_data()->set_type(
        cryptohome::KeyData::KEY_TYPE_KIOSK);
  }

  if (cl->HasSwitch(switches::kKeyLabelSwitch)) {
    auth->mutable_key()->mutable_data()->set_label(
        cl->GetSwitchValueASCII(switches::kKeyLabelSwitch));
  }

  return true;
}

std::string GetPCAName(int pca_type) {
  switch (pca_type) {
    case attestation::DEFAULT_ACA:
      return "the default ACA";
    case attestation::TEST_ACA:
      return "the test ACA";
    default: {
      std::ostringstream stream;
      stream << "ACA " << pca_type;
      return stream.str();
    }
  }
}

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->HasSwitch(switches::kSyslogSwitch))
    brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  else
    brillo::InitLog(brillo::kLogToStderr);

  attestation::ACAType pca_type = attestation::DEFAULT_ACA;
  if (cl->HasSwitch(switches::kAttestationServerSwitch)) {
    std::string server =
        cl->GetSwitchValueASCII(switches::kAttestationServerSwitch);
    bool aca_valid = false;
    for (int i = 0; switches::kAttestationServers[i].name; ++i) {
      if (server == switches::kAttestationServers[i].name) {
        pca_type = switches::kAttestationServers[i].aca_type;
        aca_valid = true;
        break;
      }
    }
    if (!aca_valid) {
      printf("Invalid attestation server: %s\n", server.c_str());
      return 1;
    }
  }

  attestation::VAType va_type = attestation::DEFAULT_VA;
  std::string va_server(
      cl->HasSwitch(switches::kVaServerSwitch)
          ? cl->GetSwitchValueASCII(switches::kVaServerSwitch)
          : cl->GetSwitchValueASCII(switches::kAttestationServerSwitch));
  if (va_server.size()) {
    bool va_valid = false;
    for (int i = 0; switches::kVaServers[i].name; ++i) {
      if (va_server == switches::kVaServers[i].name) {
        va_type = switches::kVaServers[i].va_type;
        va_valid = true;
        break;
      }
    }
    if (!va_valid) {
      printf("Invalid Verified Access server: %s\n", va_server.c_str());
      return 1;
    }
  }

  if (IsMixingOldAndNewFileSwitches(cl)) {
    printf("Use either --%s and --%s together, or --%s only.\n",
           switches::kInputFileSwitch, switches::kOutputFileSwitch,
           switches::kFileSwitch);
    return 1;
  }

  std::string action = cl->GetSwitchValueASCII(switches::kActionSwitch);
  const int timeout_ms = kDefaultTimeoutMs;

  // Setup libbrillo dbus.
  brillo::DBusConnection connection;
  scoped_refptr<dbus::Bus> bus = connection.Connect();
  DCHECK(bus) << "Failed to connect to system bus through libbrillo";

  org::chromium::AttestationProxy attestation_proxy(bus);
  org::chromium::TpmManagerProxy tpm_ownership_proxy(bus);
  org::chromium::TpmNvramProxy tpm_nvram_proxy(bus);
  org::chromium::UserDataAuthInterfaceProxy userdataauth_proxy(bus);
  org::chromium::CryptohomePkcs11InterfaceProxy pkcs11_proxy(bus);
  org::chromium::InstallAttributesInterfaceProxy install_attributes_proxy(bus);
  org::chromium::CryptohomeMiscInterfaceProxy misc_proxy(bus);

  cryptohome::Platform platform;

  if (!strcmp(switches::kActions[switches::ACTION_MOUNT_EX], action.c_str())) {
    bool is_public_mount = cl->HasSwitch(switches::kPublicMount);
    user_data_auth::MountRequest req;

    if (cl->HasSwitch(switches::kAuthSessionId)) {
      std::string auth_session_id_hex, auth_session_id;
      if (GetAuthSessionId(cl, &auth_session_id_hex)) {
        base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);
        req.set_auth_session_id(auth_session_id);
      }
    } else {
      if (!BuildAccountId(cl, req.mutable_account()))
        return 1;
      if (!BuildAuthorization(cl, &misc_proxy, !is_public_mount,
                              req.mutable_authorization()))
        return 1;
    }

    req.set_require_ephemeral(cl->HasSwitch(switches::kEnsureEphemeralSwitch));
    req.set_to_migrate_from_ecryptfs(
        cl->HasSwitch(switches::kToMigrateFromEcryptfsSwitch));
    req.set_public_mount(is_public_mount);
    if (cl->HasSwitch(switches::kCreateSwitch)) {
      user_data_auth::CreateRequest* create = req.mutable_create();
      if (cl->HasSwitch(switches::kPublicMount)) {
        cryptohome::Key* key = create->add_keys();
        key->mutable_data()->set_label(
            req.authorization().key().data().label());
      } else {
        create->set_copy_authorization_key(true);
      }
      if (cl->HasSwitch(switches::kEcryptfsSwitch)) {
        create->set_force_ecryptfs(true);
      }
    }

    user_data_auth::MountReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.Mount(req, &reply, &error, timeout_ms) || error) {
      printf("MountEx call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Mount failed.\n");
      return reply.error();
    }
    printf("Mount succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MOUNT_GUEST_EX],
                     action.c_str())) {
    user_data_auth::MountReply reply;
    user_data_auth::MountRequest req;
    brillo::ErrorPtr error;

    // This is for information. Do not fail if mount namespace is not ready.
    if (!cryptohome::UserSessionMountNamespaceExists()) {
      printf("User session mount namespace at %s has not been created yet.\n",
             cryptohome::kUserSessionMountNamespacePath);
    }

    req.set_guest_mount(true);
    if (!userdataauth_proxy.Mount(req, &reply, &error, timeout_ms) || error) {
      printf("Mount call failed: %s", BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Mount failed.\n");
      return reply.error();
    }
    printf("Mount succeeded.\n");
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_START_FINGERPRINT_AUTH_SESSION],
                     action.c_str())) {
    user_data_auth::StartFingerprintAuthSessionRequest req;
    if (!BuildAccountId(cl, req.mutable_account_id()))
      return 1;

    user_data_auth::StartFingerprintAuthSessionReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.StartFingerprintAuthSession(req, &reply, &error,
                                                        timeout_ms) ||
        error) {
      printf("StartFingerprintAuthSession call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Fingerprint auth session failed to start.\n");
      return reply.error();
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_END_FINGERPRINT_AUTH_SESSION],
                     action.c_str())) {
    user_data_auth::EndFingerprintAuthSessionRequest req;
    user_data_auth::EndFingerprintAuthSessionReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.EndFingerprintAuthSession(req, &reply, &error,
                                                      timeout_ms) ||
        error) {
      printf("EndFingerprintAuthSession call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    // EndFingerprintAuthSession always succeeds.
  } else if (!strcmp(switches::kActions[switches::ACTION_REMOVE_KEY_EX],
                     action.c_str())) {
    user_data_auth::RemoveKeyRequest req;
    if (!BuildAccountId(cl, req.mutable_account_id()))
      return 1;
    if (!BuildAuthorization(cl, &misc_proxy, true /* need_credential */,
                            req.mutable_authorization_request()))
      return 1;

    cryptohome::KeyData* data = req.mutable_key()->mutable_data();
    data->set_label(cl->GetSwitchValueASCII(switches::kRemoveKeyLabelSwitch));

    user_data_auth::RemoveKeyReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.RemoveKey(req, &reply, &error, timeout_ms) ||
        error) {
      printf("RemoveKeyEx call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Key removal failed.\n");
      return reply.error();
    }
    printf("Key removed.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_KEY_DATA_EX],
                     action.c_str())) {
    user_data_auth::GetKeyDataRequest req;
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, req.mutable_account_id())) {
      return 1;
    }
    // Make sure has_authorization_request() returns true.
    req.mutable_authorization_request();
    const std::string label =
        cl->GetSwitchValueASCII(switches::kKeyLabelSwitch);
    if (label.empty()) {
      printf("No key_label specified.\n");
      return 1;
    }
    req.mutable_key()->mutable_data()->set_label(label);

    user_data_auth::GetKeyDataReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.GetKeyData(req, &reply, &error, timeout_ms) ||
        error) {
      printf("GetKeyDataEx call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Key retrieval failed.\n");
      return reply.error();
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_LIST_KEYS_EX],
                     action.c_str())) {
    user_data_auth::ListKeysRequest req;
    if (!BuildAccountId(cl, req.mutable_account_id()))
      return 1;

    user_data_auth::ListKeysReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.ListKeys(req, &reply, &error, timeout_ms) ||
        error) {
      printf("ListKeysEx call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Failed to list keys.\n");
      return reply.error();
    }
    for (int i = 0; i < reply.labels_size(); ++i) {
      printf("Label: %s\n", reply.labels(i).c_str());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_CHECK_KEY_EX],
                     action.c_str())) {
    user_data_auth::CheckKeyRequest req;
    if (!BuildAccountId(cl, req.mutable_account_id()))
      return 1;
    if (cl->HasSwitch(switches::kFingerprintSwitch)) {
      req.mutable_authorization_request()
          ->mutable_key()
          ->mutable_data()
          ->set_type(cryptohome::KeyData::KEY_TYPE_FINGERPRINT);
    } else if (!BuildAuthorization(cl, &misc_proxy, true /* need_credential */,
                                   req.mutable_authorization_request())) {
      return 1;
    }

    // TODO(wad) Add a privileges cl interface

    user_data_auth::CheckKeyReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.CheckKey(req, &reply, &error, timeout_ms) ||
        error) {
      printf("CheckKeyEx call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Key authentication failed.\n");
      return reply.error();
    }
    printf("Key authenticated.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MASS_REMOVE_KEYS],
                     action.c_str())) {
    user_data_auth::MassRemoveKeysRequest req;
    if (!BuildAccountId(cl, req.mutable_account_id()))
      return 1;
    if (!BuildAuthorization(cl, &misc_proxy, true /* need_credential */,
                            req.mutable_authorization_request()))
      return 1;

    // Since it's unlikely to have comma in a label string,
    // exempt_key_labels are seperated by comma from command line input
    // ( e.g. --exempt_key_labels=label1,label2,label3 )
    std::vector<std::string> exempt_labels = SplitString(
        cl->GetSwitchValueASCII(switches::kMassRemoveExemptLabelsSwitch), ",",
        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    for (std::string label : exempt_labels) {
      cryptohome::KeyData* data = req.add_exempt_key_data();
      data->set_label(label);
    }

    user_data_auth::MassRemoveKeysReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.MassRemoveKeys(req, &reply, &error, timeout_ms) ||
        error) {
      printf("MassRemoveKeys call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("MassRemoveKeys failed.\n");
      return reply.error();
    }
    printf("MassRemoveKeys succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MIGRATE_KEY_EX],
                     action.c_str())) {
    std::string account_id, password, old_password;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    GetPassword(&misc_proxy, cl, switches::kPasswordSwitch,
                StringPrintf("Enter the password for <%s>", account_id.c_str()),
                &password);
    GetPassword(
        &misc_proxy, cl, switches::kOldPasswordSwitch,
        StringPrintf("Enter the old password for <%s>", account_id.c_str()),
        &old_password);

    user_data_auth::MigrateKeyRequest req;
    req.mutable_account_id()->set_account_id(account_id);
    req.mutable_authorization_request()->mutable_key()->set_secret(
        old_password);
    req.set_secret(password);

    user_data_auth::MigrateKeyReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.MigrateKey(req, &reply, &error, timeout_ms) ||
        error) {
      printf("MigrateKeyEx call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Key migration failed.\n");
      return reply.error();
    }
    printf("Key migration succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_ADD_KEY_EX],
                     action.c_str())) {
    std::string new_password;
    GetPassword(&misc_proxy, cl, switches::kNewPasswordSwitch,
                "Enter the new password", &new_password);

    user_data_auth::AddKeyRequest req;
    if (!BuildAccountId(cl, req.mutable_account_id()))
      return 1;
    if (!BuildAuthorization(cl, &misc_proxy, true /* need_credential */,
                            req.mutable_authorization_request()))
      return 1;

    req.set_clobber_if_exists(cl->HasSwitch(switches::kForceSwitch));

    cryptohome::Key* key = req.mutable_key();
    key->set_secret(new_password);
    cryptohome::KeyData* data = key->mutable_data();
    data->set_label(cl->GetSwitchValueASCII(switches::kNewKeyLabelSwitch));

    if (cl->HasSwitch(switches::kKeyPolicySwitch)) {
      if (cl->GetSwitchValueASCII(switches::kKeyPolicySwitch) ==
          switches::kKeyPolicyLECredential) {
        data->mutable_policy()->set_low_entropy_credential(true);
      } else {
        printf("Unknown key policy.\n");
        return 1;
      }
    }

    // TODO(wad) Add a privileges cl interface

    user_data_auth::AddKeyReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.AddKey(req, &reply, &error, timeout_ms) || error) {
      printf("AddKeyEx call failed: %s",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Key addition failed.\n");
      return reply.error();
    }
    printf("Key added.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_REMOVE],
                     action.c_str())) {
    user_data_auth::RemoveRequest req;
    std::string account_id;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    if (!cl->HasSwitch(switches::kForceSwitch) && !ConfirmRemove(account_id)) {
      return 1;
    }

    req.mutable_identifier()->set_account_id(account_id);

    user_data_auth::RemoveReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.Remove(req, &reply, &error, timeout_ms) || error) {
      printf("Remove call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Remove failed.\n");
      return 1;
    }
    printf("Remove succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_UNMOUNT],
                     action.c_str())) {
    user_data_auth::UnmountRequest req;

    user_data_auth::UnmountReply reply;
    brillo::ErrorPtr error;
    if (!userdataauth_proxy.Unmount(req, &reply, &error, timeout_ms) || error) {
      printf("Unmount call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Unmount failed.\n");
      return 1;
    }
    printf("Unmount succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MOUNTED],
                     action.c_str())) {
    user_data_auth::IsMountedRequest req;
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }

    user_data_auth::IsMountedReply reply;
    brillo::ErrorPtr error;
    bool is_mounted = false;
    if (!userdataauth_proxy.IsMounted(req, &reply, &error, timeout_ms) ||
        error) {
      printf("IsMounted call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    } else {
      is_mounted = reply.is_mounted();
    }
    if (is_mounted) {
      printf("true\n");
    } else {
      printf("false\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_OBFUSCATE_USER],
                     action.c_str())) {
    std::string account_id;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    if (cl->HasSwitch(switches::kUseDBus)) {
      user_data_auth::GetSanitizedUsernameRequest req;
      req.set_username(account_id);

      user_data_auth::GetSanitizedUsernameReply reply;
      brillo::ErrorPtr error;
      if (!misc_proxy.GetSanitizedUsername(req, &reply, &error, timeout_ms) ||
          error) {
        printf("GetSanitizedUserName call failed: %s.\n",
               BrilloErrorToString(error.get()).c_str());
        return 1;
      }
      printf("%s\n", reply.sanitized_username().c_str());
    } else {
      // Use libbrillo directly instead of going through dbus/cryptohome.
      if (!brillo::cryptohome::home::EnsureSystemSaltIsLoaded()) {
        printf("Failed to load system salt\n");
        return 1;
      }

      std::string* salt_ptr = brillo::cryptohome::home::GetSystemSalt();
      brillo::SecureBlob system_salt = SecureBlob(*salt_ptr);
      printf("%s\n", SanitizeUserNameWithSalt(account_id, system_salt).c_str());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_SYSTEM_SALT],
                     action.c_str())) {
    brillo::SecureBlob system_salt;
    if (cl->HasSwitch(switches::kUseDBus)) {
      system_salt = GetSystemSalt(&misc_proxy);
      if (system_salt.empty()) {
        printf("Failed to retrieve system salt\n");
      }
    } else {
      // Use libbrillo directly instead of going through dbus/cryptohome.
      if (!brillo::cryptohome::home::EnsureSystemSaltIsLoaded()) {
        printf("Failed to load system salt\n");
        return 1;
      }

      std::string* salt_ptr = brillo::cryptohome::home::GetSystemSalt();
      system_salt = SecureBlob(*salt_ptr);
    }
    std::string hex_salt =
        base::HexEncode(system_salt.data(), system_salt.size());
    // We want to follow the convention of having low case hex for output as in
    // GetSanitizedUsername().
    std::transform(hex_salt.begin(), hex_salt.end(), hex_salt.begin(),
                   ::tolower);
    printf("%s\n", hex_salt.c_str());
  } else if (!strcmp(switches::kActions[switches::ACTION_DUMP_KEYSET],
                     action.c_str())) {
    std::string account_id;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    FilePath vault_path =
        FilePath("/home/.shadow")
            .Append(SanitizeUserNameWithSalt(account_id,
                                             GetSystemSalt(&misc_proxy)))
            .Append(std::string(cryptohome::kKeyFile).append(".0"));
    brillo::Blob contents;
    if (!platform.ReadFile(vault_path, &contents)) {
      printf("Couldn't load keyset contents: %s.\n",
             vault_path.value().c_str());
      return 1;
    }
    cryptohome::SerializedVaultKeyset serialized;
    if (!serialized.ParseFromArray(contents.data(), contents.size())) {
      printf("Couldn't parse keyset contents: %s.\n",
             vault_path.value().c_str());
      return 1;
    }
    printf("For keyset: %s\n", vault_path.value().c_str());
    printf("  Flags:\n");
    if ((serialized.flags() & cryptohome::SerializedVaultKeyset::TPM_WRAPPED) &&
        serialized.has_tpm_key()) {
      printf("    TPM_WRAPPED\n");
    }
    if ((serialized.flags() & cryptohome::SerializedVaultKeyset::PCR_BOUND) &&
        serialized.has_tpm_key() && serialized.has_extended_tpm_key()) {
      printf("    PCR_BOUND\n");
    }
    if (serialized.flags() &
        cryptohome::SerializedVaultKeyset::SCRYPT_WRAPPED) {
      printf("    SCRYPT_WRAPPED\n");
    }
    SecureBlob blob(serialized.salt().length());
    serialized.salt().copy(blob.char_data(), serialized.salt().length(), 0);
    printf("  Salt:\n");
    printf("    %s\n", cryptohome::SecureBlobToHex(blob).c_str());
    blob.resize(serialized.wrapped_keyset().length());
    serialized.wrapped_keyset().copy(blob.char_data(),
                                     serialized.wrapped_keyset().length(), 0);
    printf("  Wrapped (Encrypted) Keyset:\n");
    printf("    %s\n", cryptohome::SecureBlobToHex(blob).c_str());
    if (serialized.has_tpm_key()) {
      blob.resize(serialized.tpm_key().length());
      serialized.tpm_key().copy(blob.char_data(), serialized.tpm_key().length(),
                                0);
      printf("  TPM-Bound (Encrypted) Vault Encryption Key:\n");
      printf("    %s\n", cryptohome::SecureBlobToHex(blob).c_str());
    }
    if (serialized.has_extended_tpm_key()) {
      blob.resize(serialized.extended_tpm_key().length());
      serialized.extended_tpm_key().copy(
          blob.char_data(), serialized.extended_tpm_key().length(), 0);
      printf("  TPM-Bound (Encrypted) Vault Encryption Key, PCR extended:\n");
      printf("    %s\n", cryptohome::SecureBlobToHex(blob).c_str());
    }
    if (serialized.has_tpm_public_key_hash()) {
      blob.resize(serialized.tpm_public_key_hash().length());
      serialized.tpm_public_key_hash().copy(blob.char_data(),
                                            serialized.tpm_key().length(), 0);
      printf("  TPM Public Key Hash:\n");
      printf("    %s\n", cryptohome::SecureBlobToHex(blob).c_str());
    }
    if (serialized.has_password_rounds()) {
      printf("  Password rounds:\n");
      printf("    %d\n", serialized.password_rounds());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_DUMP_LAST_ACTIVITY],
                     action.c_str())) {
    std::vector<FilePath> user_dirs;
    if (!platform.EnumerateDirectoryEntries(FilePath("/home/.shadow/"), false,
                                            &user_dirs)) {
      LOG(ERROR) << "Can not list shadow root.";
      return 1;
    }
    for (std::vector<FilePath>::iterator it = user_dirs.begin();
         it != user_dirs.end(); ++it) {
      const std::string dir_name = it->BaseName().value();
      if (!brillo::cryptohome::home::IsSanitizedUserName(dir_name))
        continue;
      base::Time last_activity = base::Time::UnixEpoch();

      FilePath timestamp_path = it->Append("timestamp");
      brillo::Blob tcontents;
      if (platform.ReadFile(timestamp_path, &tcontents)) {
        cryptohome::Timestamp timestamp;
        if (!timestamp.ParseFromArray(tcontents.data(), tcontents.size())) {
          printf("Couldn't parse timestamp contents: %s.\n",
                 timestamp_path.value().c_str());
        }
        last_activity = base::Time::FromDeltaSinceWindowsEpoch(
            base::TimeDelta::FromSeconds(timestamp.timestamp()));
      } else {
        printf("Couldn't load timestamp contents: %s.\n",
               timestamp_path.value().c_str());
      }
      if (last_activity > base::Time::UnixEpoch()) {
        printf("%s %3d\n", dir_name.c_str(),
               (base::Time::Now() - last_activity).InDays());
      }
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_STATUS],
                     action.c_str())) {
    tpm_manager::GetTpmStatusRequest req;
    tpm_manager::GetTpmStatusReply reply;
    brillo::ErrorPtr error;
    if (!tpm_ownership_proxy.GetTpmStatus(req, &reply, &error, timeout_ms) ||
        error) {
      printf("GetTpmStatus call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    } else {
      printf("TPM Enabled: %s\n", (reply.enabled() ? "true" : "false"));
      printf("TPM Owned: %s\n", (reply.owned() ? "true" : "false"));
      printf("TPM Ready: %s\n",
             ((reply.enabled() && reply.owned()) ? "true" : "false"));
      printf("TPM Password: %s\n", reply.local_data().owner_password().c_str());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_MORE_STATUS],
                     action.c_str())) {
    printf(
        "WARNING: This method is deprecated and should not be used in new "
        "code.\n");

    // This method requires the output of more than 1 method and thus is
    // divided into various parts:
    // - TpmManager stage: Calls GetTpmStatus() in tpm_manager
    // - DictionaryAttack stage: Calls GetDictionaryAttackInfo() in tpm_manager
    // - InstallAttributes stage: Calls InstallAttributesGetStatus() in
    // UserDataAuth
    // - Attestation stage: Calls GetStatus() in attestation
    // The 4 stages is executed back to back according to the sequence listed
    // above. After all of them are done, we'll take their results and form the
    // response for this method call.

    cryptohome::GetTpmStatusReply result;

    // Stage 1: TpmManager stage
    tpm_manager::GetTpmStatusRequest req_tpm_manager;
    tpm_manager::GetTpmStatusReply reply_tpm_manager;
    brillo::ErrorPtr error;
    if (!tpm_ownership_proxy.GetTpmStatus(req_tpm_manager, &reply_tpm_manager,
                                          &error, timeout_ms) ||
        error) {
      printf("GetTpmStatus call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply_tpm_manager.status() != tpm_manager::STATUS_SUCCESS) {
      printf("GetTpmStatus call failed: status %d.\n",
             static_cast<int>(reply_tpm_manager.status()));
      return 1;
    }

    result.set_enabled(reply_tpm_manager.enabled());
    result.set_owned(reply_tpm_manager.owned());
    if (!reply_tpm_manager.local_data().owner_password().empty()) {
      result.set_initialized(false);
      result.set_owner_password(
          reply_tpm_manager.local_data().owner_password());
    } else {
      // Initialized is true only when the TPM is owned and the owner password
      // has already been destroyed.
      result.set_initialized(result.owned());
    }

    bool has_reset_lock_permissions = true;
    if (reply_tpm_manager.local_data().owner_password().empty()) {
      if (reply_tpm_manager.local_data().lockout_password().empty() &&
          !reply_tpm_manager.local_data().has_owner_delegate()) {
        has_reset_lock_permissions = false;
      } else if (reply_tpm_manager.local_data().has_owner_delegate() &&
                 !reply_tpm_manager.local_data()
                      .owner_delegate()
                      .has_reset_lock_permissions()) {
        has_reset_lock_permissions = false;
      }
    }
    result.set_has_reset_lock_permissions(has_reset_lock_permissions);

    // Stage 2: DictionaryAttack stage
    tpm_manager::GetDictionaryAttackInfoRequest req_da;
    tpm_manager::GetDictionaryAttackInfoReply reply_da;
    error.reset();
    if (!tpm_ownership_proxy.GetDictionaryAttackInfo(req_da, &reply_da, &error,
                                                     timeout_ms) ||
        error) {
      printf("GetDictionaryAttackInfo call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply_da.status() != tpm_manager::STATUS_SUCCESS) {
      printf("GetDictionaryAttackInfo call failed: status %d.\n",
             static_cast<int>(reply_da.status()));
      return 1;
    }
    result.set_dictionary_attack_counter(reply_da.dictionary_attack_counter());
    result.set_dictionary_attack_threshold(
        reply_da.dictionary_attack_threshold());
    result.set_dictionary_attack_lockout_in_effect(
        reply_da.dictionary_attack_lockout_in_effect());
    result.set_dictionary_attack_lockout_seconds_remaining(
        reply_da.dictionary_attack_lockout_seconds_remaining());

    // Stage 3: InstallAttributes stage
    user_data_auth::InstallAttributesGetStatusRequest req_ia;
    user_data_auth::InstallAttributesGetStatusReply reply_ia;
    error.reset();
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            req_ia, &reply_ia, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply_ia.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("InstallAttributesGetStatus call failed: status %d.\n",
             static_cast<int>(reply_ia.error()));
      return 1;
    }

    result.set_install_lockbox_finalized(
        result.owned() &&
        reply_ia.state() == user_data_auth::InstallAttributesState::VALID);

    // Stage 4: Attestation stage
    attestation::GetStatusRequest req_attestation;
    attestation::GetStatusReply reply_attestation;
    req_attestation.set_extended_status(true);
    error.reset();
    if (!attestation_proxy.GetStatus(req_attestation, &reply_attestation,
                                     &error, timeout_ms) ||
        error) {
      printf("AttestationGetStatus call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    result.set_boot_lockbox_finalized(false);
    result.set_is_locked_to_single_user(platform.FileExists(
        base::FilePath(cryptohome::kLockedToSingleUserFile)));
    if (reply_attestation.status() !=
        attestation::AttestationStatus::STATUS_SUCCESS) {
      printf("AttestationGetStatus call failed: status %d.\n",
             static_cast<int>(reply_attestation.status()));
      return 1;
    } else {
      result.set_attestation_prepared(
          reply_attestation.prepared_for_enrollment());
      result.set_attestation_enrolled(reply_attestation.enrolled());
      result.set_verified_boot_measured(reply_attestation.verified_boot());
      for (auto it = reply_attestation.identities().cbegin(),
                end = reply_attestation.identities().cend();
           it != end; ++it) {
        auto* identity = result.mutable_identities()->Add();
        identity->set_features(it->features());
      }
      for (auto it = reply_attestation.identity_certificates().cbegin(),
                end = reply_attestation.identity_certificates().cend();
           it != end; ++it) {
        cryptohome::GetTpmStatusReply::IdentityCertificate identity_certificate;
        identity_certificate.set_identity(it->second.identity());
        identity_certificate.set_aca(it->second.aca());
        result.mutable_identity_certificates()->insert(
            google::protobuf::Map<
                int, cryptohome::GetTpmStatusReply::IdentityCertificate>::
                value_type(it->first, identity_certificate));
      }
    }

    // Print the result.
    result.PrintDebugString();

    printf("GetTpmStatus success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_STATUS],
                     action.c_str())) {
    user_data_auth::GetStatusStringRequest req;
    user_data_auth::GetStatusStringReply reply;
    brillo::ErrorPtr error;
    if (!misc_proxy.GetStatusString(req, &reply, &error, timeout_ms) || error) {
      printf("GetStatusString call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    } else {
      printf("%s\n", reply.status().c_str());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_SET_CURRENT_USER_OLD],
                     action.c_str())) {
    user_data_auth::UpdateCurrentUserActivityTimestampRequest req;
    user_data_auth::UpdateCurrentUserActivityTimestampReply reply;
    req.set_time_shift_sec(
        base::TimeDelta::FromDays(kSetCurrentUserOldOffsetInDays).InSeconds());
    brillo::ErrorPtr error;
    if (!misc_proxy.UpdateCurrentUserActivityTimestamp(req, &reply, &error,
                                                       timeout_ms) ||
        error) {
      printf("UpdateCurrentUserActivityTimestamp call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    } else {
      printf(
          "Timestamp successfully updated. You may verify it with "
          "--action=dump_keyset --user=...\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_TAKE_OWNERSHIP],
                     action.c_str())) {
    tpm_manager::TakeOwnershipRequest req;
    req.set_is_async(true);
    tpm_manager::TakeOwnershipReply reply;
    brillo::ErrorPtr error;
    if (!tpm_ownership_proxy.TakeOwnership(req, &reply, &error, timeout_ms) ||
        error) {
      printf("TpmCanAttemptOwnership call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_CLEAR_STORED_PASSWORD],
                 action.c_str())) {
    tpm_manager::ClearStoredOwnerPasswordRequest req;
    tpm_manager::ClearStoredOwnerPasswordReply reply;

    brillo::ErrorPtr error;
    if (!tpm_ownership_proxy.ClearStoredOwnerPassword(req, &reply, &error,
                                                      timeout_ms) ||
        error) {
      printf("TpmClearStoredPassword call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_INSTALL_ATTRIBUTES_GET],
                 action.c_str())) {
    std::string name;
    if (!GetAttrName(cl, &name)) {
      printf("No attribute name specified.\n");
      return 1;
    }

    // Make sure install attributes are ready.
    user_data_auth::InstallAttributesGetStatusRequest status_req;
    user_data_auth::InstallAttributesGetStatusReply status_reply;
    brillo::ErrorPtr error;
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            status_req, &status_reply, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (status_reply.state() ==
            user_data_auth::InstallAttributesState::UNKNOWN ||
        status_reply.state() ==
            user_data_auth::InstallAttributesState::TPM_NOT_OWNED) {
      printf("InstallAttributes() is not ready.\n");
      return 1;
    }

    user_data_auth::InstallAttributesGetRequest req;
    user_data_auth::InstallAttributesGetReply reply;
    req.set_name(name);
    error.reset();
    if (!install_attributes_proxy.InstallAttributesGet(req, &reply, &error,
                                                       timeout_ms) ||
        error) {
      printf("InstallAttributesGet call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.error() ==
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("%s\n", reply.value().c_str());
    } else {
      return 1;
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_INSTALL_ATTRIBUTES_SET],
                 action.c_str())) {
    std::string name;
    if (!GetAttrName(cl, &name)) {
      printf("No attribute name specified.\n");
      return 1;
    }
    std::string value;
    if (!GetAttrValue(cl, &value)) {
      printf("No attribute value specified.\n");
      return 1;
    }

    // Make sure install attributes are ready.
    user_data_auth::InstallAttributesGetStatusRequest status_req;
    user_data_auth::InstallAttributesGetStatusReply status_reply;
    brillo::ErrorPtr error;
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            status_req, &status_reply, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (status_reply.state() ==
            user_data_auth::InstallAttributesState::UNKNOWN ||
        status_reply.state() ==
            user_data_auth::InstallAttributesState::TPM_NOT_OWNED) {
      printf("InstallAttributes() is not ready.\n");
      return 1;
    }

    user_data_auth::InstallAttributesSetRequest req;
    user_data_auth::InstallAttributesSetReply reply;
    req.set_name(name);
    // It is expected that a null terminator is part of the value.
    value.push_back('\0');
    req.set_value(value);
    error.reset();
    if (!install_attributes_proxy.InstallAttributesSet(req, &reply, &error,
                                                       timeout_ms) ||
        error) {
      printf("InstallAttributesSet call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Call to InstallAttributesSet() failed.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_FINALIZE],
                     action.c_str())) {
    // Make sure install attributes are ready.
    user_data_auth::InstallAttributesGetStatusRequest status_req;
    user_data_auth::InstallAttributesGetStatusReply status_reply;
    brillo::ErrorPtr error;
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            status_req, &status_reply, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (status_reply.state() ==
            user_data_auth::InstallAttributesState::UNKNOWN ||
        status_reply.state() ==
            user_data_auth::InstallAttributesState::TPM_NOT_OWNED) {
      printf("InstallAttributes() is not ready.\n");
      return 1;
    }

    user_data_auth::InstallAttributesFinalizeRequest req;
    user_data_auth::InstallAttributesFinalizeReply reply;
    error.reset();
    if (!install_attributes_proxy.InstallAttributesFinalize(req, &reply, &error,
                                                            timeout_ms) ||
        error) {
      printf("InstallAttributesFinalize() failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    bool result = reply.error() ==
                  user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET;
    printf("InstallAttributesFinalize(): %d\n", static_cast<int>(result));
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_INSTALL_ATTRIBUTES_COUNT],
                 action.c_str())) {
    user_data_auth::InstallAttributesGetStatusRequest req;
    user_data_auth::InstallAttributesGetStatusReply reply;
    brillo::ErrorPtr error;
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            req, &reply, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus() call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Call to InstallAttributesGetStatus() failed.\n");
      return 1;
    }
    printf("InstallAttributesCount(): %d\n", reply.count());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_GET_STATUS],
                     action.c_str())) {
    user_data_auth::InstallAttributesGetStatusRequest req;
    user_data_auth::InstallAttributesGetStatusReply reply;
    brillo::ErrorPtr error;
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            req, &reply, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus() call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Call to InstallAttributesGetStatus() failed.\n");
      return 1;
    }
    printf("%s\n", InstallAttributesState_Name(reply.state()).c_str());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_IS_READY],
                     action.c_str())) {
    user_data_auth::InstallAttributesGetStatusRequest req;
    user_data_auth::InstallAttributesGetStatusReply reply;
    brillo::ErrorPtr error;
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            req, &reply, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus() call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Call to InstallAttributesGetStatus() failed.\n");
      return 1;
    }

    bool result =
        (reply.state() != user_data_auth::InstallAttributesState::UNKNOWN &&
         reply.state() !=
             user_data_auth::InstallAttributesState::TPM_NOT_OWNED);
    printf("InstallAttributesIsReady(): %d\n", static_cast<int>(result));
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_IS_SECURE],
                     action.c_str())) {
    user_data_auth::InstallAttributesGetStatusRequest req;
    user_data_auth::InstallAttributesGetStatusReply reply;
    brillo::ErrorPtr error;
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            req, &reply, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus() call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Call to InstallAttributesGetStatus() failed.\n");
      return 1;
    }

    bool result = reply.is_secure();
    printf("InstallAttributesIsSecure(): %d\n", static_cast<int>(result));
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_IS_INVALID],
                     action.c_str())) {
    user_data_auth::InstallAttributesGetStatusRequest req;
    user_data_auth::InstallAttributesGetStatusReply reply;
    brillo::ErrorPtr error;
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            req, &reply, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus() call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Call to InstallAttributesGetStatus() failed.\n");
      return 1;
    }

    bool result =
        (reply.state() == user_data_auth::InstallAttributesState::INVALID);
    printf("InstallAttributesIsInvalid(): %d\n", static_cast<int>(result));
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_INSTALL_ATTRIBUTES_IS_FIRST_INSTALL],
                     action.c_str())) {
    user_data_auth::InstallAttributesGetStatusRequest req;
    user_data_auth::InstallAttributesGetStatusReply reply;
    brillo::ErrorPtr error;
    if (!install_attributes_proxy.InstallAttributesGetStatus(
            req, &reply, &error, timeout_ms) ||
        error) {
      printf("InstallAttributesGetStatus() call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Call to InstallAttributesGetStatus() failed.\n");
      return 1;
    }
    bool result = (reply.state() ==
                   user_data_auth::InstallAttributesState::FIRST_INSTALL);

    printf("InstallAttributesIsFirstInstall(): %d\n", static_cast<int>(result));
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_WAIT_OWNERSHIP],
                     action.c_str())) {
    // Note that this is a rather hackish implementation that will be replaced
    // once the refactor to distributed mode is over. It'll be replaced with an
    // implementation that does one synchronous call to tpm_manager's
    // TakeOwnership(), then check if it's owned.
    int timeout = kWaitOwnershipTimeoutInSeconds;
    int timeout_in_switch;
    if (cl->HasSwitch(switches::kWaitOwnershipTimeoutSwitch) &&
        base::StringToInt(
            cl->GetSwitchValueASCII(switches::kWaitOwnershipTimeoutSwitch),
            &timeout_in_switch)) {
      timeout = timeout_in_switch;
    }

    auto deadline = base::Time::Now() + base::TimeDelta::FromSeconds(timeout);
    while (base::Time::Now() < deadline) {
      base::PlatformThread::Sleep(
          base::TimeDelta::FromMilliseconds(kWaitOwnershipPollIntervalInMs));
      tpm_manager::GetTpmStatusRequest req;
      tpm_manager::GetTpmStatusReply reply;
      brillo::ErrorPtr error;
      if (!tpm_ownership_proxy.GetTpmStatus(req, &reply, &error, timeout_ms) ||
          error) {
        printf("TpmIsOwned call failed: %s.\n",
               BrilloErrorToString(error.get()).c_str());
      }
      if (reply.owned()) {
        // This is the condition we are waiting for.
        printf("TPM is now owned.\n");
        return 0;
      }
    }
    printf("Fail to own TPM.\n");
    return 1;
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_PKCS11_GET_USER_TOKEN_INFO],
                     action.c_str())) {
    // If no account_id is specified, proceed with the empty string.
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    if (!account_id.empty()) {
      user_data_auth::Pkcs11GetTpmTokenInfoRequest req;
      user_data_auth::Pkcs11GetTpmTokenInfoReply reply;
      req.set_username(account_id);
      brillo::ErrorPtr error;
      if (!pkcs11_proxy.Pkcs11GetTpmTokenInfo(req, &reply, &error,
                                              timeout_ms) ||
          error) {
        printf("PKCS #11 info call failed: %s.\n",
               BrilloErrorToString(error.get()).c_str());
      } else {
        printf("Token properties for %s:\n", account_id.c_str());
        printf("Label = %s\n", reply.token_info().label().c_str());
        printf("Pin = %s\n", reply.token_info().user_pin().c_str());
        printf("Slot = %d\n", reply.token_info().slot());
      }
    } else {
      printf("Account ID/Username not specified.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_PKCS11_GET_SYSTEM_TOKEN_INFO],
                     action.c_str())) {
    user_data_auth::Pkcs11GetTpmTokenInfoRequest req;
    user_data_auth::Pkcs11GetTpmTokenInfoReply reply;
    brillo::ErrorPtr error;
    if (!pkcs11_proxy.Pkcs11GetTpmTokenInfo(req, &reply, &error, timeout_ms) ||
        error) {
      printf("PKCS #11 info call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    } else {
      printf("System token properties:\n");
      printf("Label = %s\n", reply.token_info().label().c_str());
      printf("Pin = %s\n", reply.token_info().user_pin().c_str());
      printf("Slot = %d\n", reply.token_info().slot());
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_PKCS11_IS_USER_TOKEN_OK],
                 action.c_str())) {
    cryptohome::Pkcs11Init init;
    if (!init.IsUserTokenOK()) {
      printf("User token looks broken!\n");
      return 1;
    }
    printf("User token looks OK!\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_PKCS11_TERMINATE],
                     action.c_str())) {
    user_data_auth::Pkcs11TerminateRequest req;
    user_data_auth::Pkcs11TerminateReply reply;

    // If no account_id is specified, proceed with the empty string.
    std::string account_id;
    GetAccountId(cl, &account_id);
    req.set_username(account_id);
    brillo::ErrorPtr error;
    if (!pkcs11_proxy.Pkcs11Terminate(req, &reply, &error, timeout_ms) ||
        error) {
      printf("PKCS #11 terminate call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_PKCS11_RESTORE_TPM_TOKENS],
                 action.c_str())) {
    user_data_auth::Pkcs11RestoreTpmTokensRequest req;
    user_data_auth::Pkcs11RestoreTpmTokensReply reply;
    brillo::ErrorPtr error;
    if (!pkcs11_proxy.Pkcs11RestoreTpmTokens(req, &reply, &error, timeout_ms) ||
        error) {
      printf("PKCS #11 restore TPM tokens call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_VERIFY_ATTESTATION],
                 action.c_str())) {
    attestation::VerifyRequest req;

    bool is_cros_core = cl->HasSwitch(switches::kCrosCoreSwitch);
    req.set_cros_core(is_cros_core);
    req.set_ek_only(false);

    attestation::VerifyReply reply;
    brillo::ErrorPtr error;
    if (!attestation_proxy.Verify(req, &reply, &error, timeout_ms) || error) {
      printf("TpmVerifyAttestationData call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("TpmVerifyAttestationData call failed: status %d.\n",
             static_cast<int>(reply.status()));
      return 1;
    }
    if (reply.verified()) {
      printf("TPM attestation data is not valid or is not available.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_VERIFY_EK],
                     action.c_str())) {
    attestation::VerifyRequest req;

    bool is_cros_core = cl->HasSwitch(switches::kCrosCoreSwitch);
    req.set_cros_core(is_cros_core);
    req.set_ek_only(true);

    attestation::VerifyReply reply;
    brillo::ErrorPtr error;
    if (!attestation_proxy.Verify(req, &reply, &error, timeout_ms) || error) {
      printf("TpmVerifyEK call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("TpmVerifyEK call failed: status %d.\n",
             static_cast<int>(reply.status()));
      return 1;
    }
    if (reply.verified()) {
      printf("TPM endorsement key is not valid or is not available.\n");
      return 1;
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_ATTESTATION_STATUS],
                 action.c_str())) {
    attestation::GetEnrollmentPreparationsRequest prepare_req;
    attestation::GetEnrollmentPreparationsReply prepare_reply;
    brillo::ErrorPtr error;
    if (!attestation_proxy.GetEnrollmentPreparations(
            prepare_req, &prepare_reply, &error, timeout_ms) ||
        error) {
      printf("TpmIsAttestationPrepared call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    } else {
      bool result = false;
      for (const auto& preparation : prepare_reply.enrollment_preparations()) {
        if (preparation.second) {
          result = true;
          break;
        }
      }
      printf("Attestation Prepared: %s\n", (result ? "true" : "false"));
    }

    attestation::GetStatusRequest req;
    attestation::GetStatusReply reply;
    req.set_extended_status(false);
    error.reset();
    if (!attestation_proxy.GetStatus(req, &reply, &error, timeout_ms) ||
        error) {
      printf("TpmIsAttestationEnrolled call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    } else if (reply.status() !=
               attestation::AttestationStatus::STATUS_SUCCESS) {
      printf("TpmIsAttestationEnrolled call failed: status %d.\n",
             static_cast<int>(reply.status()));
    } else {
      printf("Attestation Enrolled: %s\n",
             (reply.enrolled() ? "true" : "false"));
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_MORE_STATUS],
                     action.c_str())) {
    attestation::GetEnrollmentPreparationsRequest prepare_req;
    attestation::GetEnrollmentPreparationsReply prepare_reply;
    brillo::ErrorPtr error;
    if (!attestation_proxy.GetEnrollmentPreparations(
            prepare_req, &prepare_reply, &error, timeout_ms) ||
        error) {
      printf("TpmAttestationGetEnrollmentPreparationsEx call failed: %s\n",
             BrilloErrorToString(error.get()).c_str());
    } else if (prepare_reply.status() != attestation::STATUS_SUCCESS) {
      printf(
          "TpmAttestationGetEnrollmentPreparationsEx call failed: status %d\n",
          static_cast<int>(prepare_reply.status()));
    } else {
      auto map = prepare_reply.enrollment_preparations();
      bool prepared = false;
      for (auto it = map.cbegin(), end = map.cend(); it != end; ++it) {
        prepared |= it->second;
      }
      printf("Attestation Prepared: %s\n", prepared ? "true" : "false");
      for (auto it = map.cbegin(), end = map.cend(); it != end; ++it) {
        printf("    Prepared for %s: %s\n", GetPCAName(it->first).c_str(),
               (it->second ? "true" : "false"));
      }
    }

    // TODO(crbug.com/922062): Replace with a call listing all identity certs.

    attestation::GetStatusRequest req;
    attestation::GetStatusReply reply;
    req.set_extended_status(false);
    error.reset();
    if (!attestation_proxy.GetStatus(req, &reply, &error, timeout_ms) ||
        error) {
      printf("TpmIsAttestationEnrolled call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
    } else if (reply.status() !=
               attestation::AttestationStatus::STATUS_SUCCESS) {
      printf("TpmIsAttestationEnrolled call failed: status %d.\n",
             static_cast<int>(reply.status()));
    } else {
      printf("Attestation Enrolled: %s\n",
             (reply.enrolled() ? "true" : "false"));
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_START_ENROLL],
                     action.c_str())) {
    attestation::CreateEnrollRequestRequest req;
    attestation::CreateEnrollRequestReply reply;
    req.set_aca_type(pca_type);

    brillo::ErrorPtr error;
    if (!attestation_proxy.CreateEnrollRequest(req, &reply, &error,
                                               timeout_ms) ||
        error) {
      printf("TpmAttestationCreateEnrollRequest call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("TpmAttestationCreateEnrollRequest call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    const std::string& response_data = reply.pca_request();
    base::WriteFile(GetOutputFile(cl), response_data.data(),
                    response_data.length());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_FINISH_ENROLL],
                     action.c_str())) {
    std::string contents;
    if (!base::ReadFileToString(GetInputFile(cl), &contents)) {
      printf("Failed to read input file.\n");
      return 1;
    }

    attestation::FinishEnrollRequest req;
    attestation::FinishEnrollReply reply;
    req.set_pca_response(contents);
    req.set_aca_type(pca_type);

    brillo::ErrorPtr error;
    if (!attestation_proxy.FinishEnroll(req, &reply, &error, timeout_ms) ||
        error) {
      printf("TpmAttestationEnroll call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("TpmAttestationEnroll call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_ATTESTATION_ENROLL],
                 action.c_str())) {
    CHECK(false) << "Not implemented.";
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_START_CERTREQ],
                     action.c_str())) {
    attestation::CertificateProfile profile;
    if (!GetProfile(cl, &profile)) {
      return 1;
    }

    attestation::CreateCertificateRequestRequest req;
    attestation::CreateCertificateRequestReply reply;
    req.set_certificate_profile(profile);
    req.set_username("");
    req.set_request_origin("");
    req.set_aca_type(pca_type);

    brillo::ErrorPtr error;
    if (!attestation_proxy.CreateCertificateRequest(req, &reply, &error,
                                                    timeout_ms) ||
        error) {
      printf("TpmAttestationCreateCertRequest call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("TpmAttestationCreateCertRequest call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    const std::string& response_data = reply.pca_request();
    base::WriteFile(GetOutputFile(cl), response_data.data(),
                    response_data.length());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_FINISH_CERTREQ],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    std::string contents;
    if (!base::ReadFileToString(GetInputFile(cl), &contents)) {
      printf("Failed to read input file.\n");
      return 1;
    }

    attestation::FinishCertificateRequestRequest req;
    attestation::FinishCertificateRequestReply reply;
    req.set_pca_response(contents);
    req.set_key_label(key_name);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }

    brillo::ErrorPtr error;
    if (!attestation_proxy.FinishCertificateRequest(req, &reply, &error,
                                                    timeout_ms) ||
        error) {
      printf("TpmAttestationFinishCertRequest call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("TpmAttestationFinishCertRequest call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    const std::string& cert_data = reply.certificate();
    base::WriteFile(GetOutputFile(cl), cert_data.data(), cert_data.length());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_GET_CERTIFICATE],
                     action.c_str())) {
    CHECK(false) << "Not implemented.";
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_KEY_STATUS],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }

    attestation::GetKeyInfoRequest req;
    attestation::GetKeyInfoReply reply;
    req.set_key_label(key_name);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }

    brillo::ErrorPtr error;
    if (!attestation_proxy.GetKeyInfo(req, &reply, &error, timeout_ms) ||
        error) {
      printf("TpmAttestationGetCertificate call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() == attestation::STATUS_INVALID_PARAMETER) {
      printf("Key does not exist.\n");
      return 0;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("TpmAttestationGetCertificate call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    const std::string& cert_pem = reply.certificate();
    const std::string public_key_hex =
        base::HexEncode(reply.public_key().data(), reply.public_key().size());
    printf("Public Key:\n%s\n\nCertificate:\n%s\n", public_key_hex.c_str(),
           cert_pem.c_str());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_REGISTER_KEY],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }

    attestation::RegisterKeyWithChapsTokenRequest req;
    attestation::RegisterKeyWithChapsTokenReply reply;
    req.set_key_label(key_name);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }

    brillo::ErrorPtr error;
    if (!attestation_proxy.RegisterKeyWithChapsToken(req, &reply, &error,
                                                     timeout_ms) ||
        error) {
      printf("TpmAttestationRegisterKey call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("TpmAttestationRegisterKey call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    printf("Result: Success\n");
  } else if (!strcmp(
                 switches::kActions
                     [switches::ACTION_TPM_ATTESTATION_ENTERPRISE_CHALLENGE],
                 action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    std::string contents;
    if (!base::ReadFileToString(GetInputFile(cl), &contents)) {
      printf("Failed to read input file: %s\n",
             GetInputFile(cl).value().c_str());
      return 1;
    }
    const std::string device_id_str = "fake_device_id";

    attestation::SignEnterpriseChallengeRequest req;
    attestation::SignEnterpriseChallengeReply reply;
    req.set_va_type(va_type);
    req.set_key_label(key_name);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }
    req.set_domain(account_id);
    *req.mutable_device_id() = {device_id_str.begin(), device_id_str.end()};
    req.set_include_signed_public_key(true);
    *req.mutable_challenge() = {contents.begin(), contents.end()};

    brillo::ErrorPtr error;
    if (!attestation_proxy.SignEnterpriseChallenge(req, &reply, &error,
                                                   timeout_ms) ||
        error) {
      printf("AsyncTpmAttestationSignEnterpriseVaChallenge call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf(
          "AsyncTpmAttestationSignEnterpriseVaChallenge call failed: status "
          "%d\n",
          static_cast<int>(reply.status()));
      return 1;
    }

    base::WriteFileDescriptor(STDOUT_FILENO, reply.challenge_response());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_SIMPLE_CHALLENGE],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    std::string contents = "challenge";

    attestation::SignSimpleChallengeRequest req;
    attestation::SignSimpleChallengeReply reply;
    req.set_key_label(key_name);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }
    *req.mutable_challenge() = {contents.begin(), contents.end()};

    brillo::ErrorPtr error;
    if (!attestation_proxy.SignSimpleChallenge(req, &reply, &error,
                                               timeout_ms) ||
        error) {
      printf("AsyncTpmAttestationSignSimpleChallenge call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("AsyncTpmAttestationSignSimpleChallenge call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    base::WriteFileDescriptor(STDOUT_FILENO, reply.challenge_response());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_GET_KEY_PAYLOAD],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }

    attestation::GetKeyInfoRequest req;
    attestation::GetKeyInfoReply reply;
    req.set_key_label(key_name);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }

    brillo::ErrorPtr error;
    if (!attestation_proxy.GetKeyInfo(req, &reply, &error, timeout_ms) ||
        error) {
      printf("AsyncTpmAttestationGetKetPayload call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("AsyncTpmAttestationGetKetPayload call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    base::WriteFile(GetOutputFile(cl), reply.payload().data(),
                    reply.payload().size());
    base::WriteFileDescriptor(STDOUT_FILENO, reply.payload());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_SET_KEY_PAYLOAD],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    std::string value = cl->GetSwitchValueASCII(switches::kAttrValueSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    if (value.length() == 0) {
      printf("No payload specified (--%s=<payload>)\n",
             switches::kAttrValueSwitch);
      return 1;
    }

    attestation::SetKeyPayloadRequest req;
    attestation::SetKeyPayloadReply reply;
    req.set_key_label(key_name);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }
    *req.mutable_payload() = {value.begin(), value.end()};

    brillo::ErrorPtr error;
    if (!attestation_proxy.SetKeyPayload(req, &reply, &error, timeout_ms) ||
        error) {
      printf("AsyncTpmAttestationSetKetPayload call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("AsyncTpmAttestationSetKetPayload call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_DELETE_KEYS],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_prefix =
        cl->GetSwitchValueASCII(switches::kAttrPrefixSwitch);
    if (key_prefix.empty()) {
      printf("No key prefix specified (--%s=<prefix>)\n",
             switches::kAttrPrefixSwitch);
      return 1;
    }

    attestation::DeleteKeysRequest req;
    attestation::DeleteKeysReply reply;
    req.set_key_label_match(key_prefix);
    req.set_match_behavior(
        attestation::DeleteKeysRequest::MATCH_BEHAVIOR_PREFIX);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }

    brillo::ErrorPtr error;
    if (!attestation_proxy.DeleteKeys(req, &reply, &error, timeout_ms) ||
        error) {
      printf("AsyncTpmAttestationDeleteKeys call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("AsyncTpmAttestationDeleteKeys call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_DELETE_KEY],
                     action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.empty()) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }

    attestation::DeleteKeysRequest req;
    attestation::DeleteKeysReply reply;
    req.set_key_label_match(key_name);
    req.set_match_behavior(
        attestation::DeleteKeysRequest::MATCH_BEHAVIOR_EXACT);
    if (!account_id.empty()) {
      req.set_username(account_id);
    }

    brillo::ErrorPtr error;
    if (!attestation_proxy.DeleteKeys(req, &reply, &error, timeout_ms) ||
        error) {
      printf("AsyncTpmAttestationDeleteKeys call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("AsyncTpmAttestationDeleteKeys call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_TPM_ATTESTATION_GET_EK],
                 action.c_str())) {
    attestation::GetEndorsementInfoRequest req;
    attestation::GetEndorsementInfoReply reply;

    brillo::ErrorPtr error;
    if (!attestation_proxy.GetEndorsementInfo(req, &reply, &error,
                                              timeout_ms) ||
        error) {
      printf("GetEndorsementInfo call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("GetEndorsementInfo call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    printf("%s\n", reply.ek_info().c_str());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_TPM_ATTESTATION_RESET_IDENTITY],
                     action.c_str())) {
    attestation::ResetIdentityRequest req;
    attestation::ResetIdentityReply reply;

    std::string token = cl->GetSwitchValueASCII(switches::kPasswordSwitch);
    req.set_reset_token(token);

    brillo::ErrorPtr error;
    if (!attestation_proxy.ResetIdentity(req, &reply, &error, timeout_ms) ||
        error) {
      printf("TpmAttestationResetIdentity call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("TpmAttestationResetIdentity call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    base::WriteFile(GetOutputFile(cl), reply.reset_request().data(),
                    reply.reset_request().size());
  } else if (!strcmp(
                 switches::kActions
                     [switches::ACTION_TPM_ATTESTATION_RESET_IDENTITY_RESULT],
                 action.c_str())) {
    std::string contents;
    if (!base::ReadFileToString(GetInputFile(cl), &contents)) {
      printf("Failed to read input file: %s\n",
             GetInputFile(cl).value().c_str());
      return 1;
    }
    cryptohome::AttestationResetResponse response;
    if (!response.ParseFromString(contents)) {
      printf("Failed to parse response.\n");
      return 1;
    }
    switch (response.status()) {
      case cryptohome::OK:
        printf("Identity reset successful.\n");
        break;
      case cryptohome::SERVER_ERROR:
        printf("Identity reset server error: %s\n", response.detail().c_str());
        break;
      case cryptohome::BAD_REQUEST:
        printf("Identity reset data error: %s\n", response.detail().c_str());
        break;
      case cryptohome::REJECT:
        printf("Identity reset request denied: %s\n",
               response.detail().c_str());
        break;
      case cryptohome::QUOTA_LIMIT_EXCEEDED:
        printf("Identity reset quota exceeded: %s\n",
               response.detail().c_str());
        break;
      default:
        printf("Identity reset unknown error: %s\n", response.detail().c_str());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_SIGN_LOCKBOX],
                     action.c_str())) {
    CHECK(false) << "Not implemented.";
  } else if (!strcmp(switches::kActions[switches::ACTION_VERIFY_LOCKBOX],
                     action.c_str())) {
    CHECK(false) << "Not implemented.";
  } else if (!strcmp(switches::kActions[switches::ACTION_FINALIZE_LOCKBOX],
                     action.c_str())) {
    CHECK(false) << "Not implemented.";
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_BOOT_ATTRIBUTE],
                     action.c_str())) {
    CHECK(false) << "Not implemented.";
  } else if (!strcmp(switches::kActions[switches::ACTION_SET_BOOT_ATTRIBUTE],
                     action.c_str())) {
    CHECK(false) << "Not implemented.";
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_FLUSH_AND_SIGN_BOOT_ATTRIBUTES],
                     action.c_str())) {
    CHECK(false) << "Not implemented.";
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_LOGIN_STATUS],
                     action.c_str())) {
    user_data_auth::GetLoginStatusRequest req;
    user_data_auth::GetLoginStatusReply reply;

    brillo::ErrorPtr error;
    if (!misc_proxy.GetLoginStatus(req, &reply, &error, timeout_ms) || error) {
      printf("Failed to call GetLoginStatus: %s\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }

    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Failed to call GetLoginStatus: status %d\n",
             static_cast<int>(reply.error()));
      return 1;
    }
    // TODO(b/189388158): because PrintDebugString won't print a field if it's
    // default value in proto3. We use a workaround to print it manually here.
    if (!reply.owner_user_exists()) {
      printf("owner_user_exists: false\n");
    }
    if (!reply.is_locked_to_single_user()) {
      printf("is_locked_to_single_user: false\n");
    }

    printf("GetLoginStatus success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_INITIALIZE_CAST_KEY],
                     action.c_str())) {
    CHECK(false) << "Not implemented.";
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_GET_FIRMWARE_MANAGEMENT_PARAMETERS],
                     action.c_str())) {
    user_data_auth::GetFirmwareManagementParametersRequest req;
    user_data_auth::GetFirmwareManagementParametersReply reply;

    brillo::ErrorPtr error;
    if (!install_attributes_proxy.GetFirmwareManagementParameters(
            req, &reply, &error, timeout_ms) ||
        error) {
      printf("Failed to call GetFirmwareManagementParameters: %s\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else {
      reply.PrintDebugString();
      if (reply.error() !=
          user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
        printf("Failed to call GetFirmwareManagementParameters: status %d\n",
               static_cast<int>(reply.error()));
        return 1;
      }
    }

    printf("flags=0x%08x\n", reply.fwmp().flags());
    brillo::Blob hash =
        brillo::BlobFromString(reply.fwmp().developer_key_hash());
    printf("hash=%s\n", cryptohome::BlobToHex(hash).c_str());
    printf("GetFirmwareManagementParameters success.\n");
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_SET_FIRMWARE_MANAGEMENT_PARAMETERS],
                     action.c_str())) {
    user_data_auth::SetFirmwareManagementParametersRequest req;
    user_data_auth::SetFirmwareManagementParametersReply reply;

    if (cl->HasSwitch(switches::kFlagsSwitch)) {
      std::string flags_str = cl->GetSwitchValueASCII(switches::kFlagsSwitch);
      char* end = NULL;
      int32_t flags = strtol(flags_str.c_str(), &end, 0);
      if (end && *end != '\0') {
        printf("Bad flags value.\n");
        return 1;
      }
      req.mutable_fwmp()->set_flags(flags);
    } else {
      printf("Use --flags (and optionally --developer_key_hash).\n");
      return 1;
    }

    if (cl->HasSwitch(switches::kDevKeyHashSwitch)) {
      std::string hash_str =
          cl->GetSwitchValueASCII(switches::kDevKeyHashSwitch);
      brillo::Blob hash;
      if (!base::HexStringToBytes(hash_str, &hash)) {
        printf("Bad hash value.\n");
        return 1;
      }
      if (hash.size() != SHA256_DIGEST_LENGTH) {
        printf("Bad hash size.\n");
        return 1;
      }

      req.mutable_fwmp()->set_developer_key_hash(brillo::BlobToString(hash));
    }

    brillo::ErrorPtr error;
    if (!install_attributes_proxy.SetFirmwareManagementParameters(
            req, &reply, &error, timeout_ms) ||
        error) {
      printf("Failed to call SetFirmwareManagementParameters: %s\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else {
      reply.PrintDebugString();
      if (reply.error() !=
          user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
        printf("Failed to call SetFirmwareManagementParameters: status %d\n",
               static_cast<int>(reply.error()));
        return 1;
      }
    }

    printf("SetFirmwareManagementParameters success.\n");
  } else if (!strcmp(
                 switches::kActions
                     [switches::ACTION_REMOVE_FIRMWARE_MANAGEMENT_PARAMETERS],
                 action.c_str())) {
    user_data_auth::RemoveFirmwareManagementParametersRequest req;
    user_data_auth::RemoveFirmwareManagementParametersReply reply;

    brillo::ErrorPtr error;
    if (!install_attributes_proxy.RemoveFirmwareManagementParameters(
            req, &reply, &error, timeout_ms) ||
        error) {
      printf("Failed to call RemoveFirmwareManagementParameters: %s\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else {
      reply.PrintDebugString();
      if (reply.error() !=
          user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
        printf("Failed to call RemoveFirmwareManagementParameters: status %d\n",
               static_cast<int>(reply.error()));
        return 1;
      }
    }

    printf("RemoveFirmwareManagementParameters success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MIGRATE_TO_DIRCRYPTO],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;

    user_data_auth::StartMigrateToDircryptoRequest req;
    user_data_auth::StartMigrateToDircryptoReply reply;
    *req.mutable_account_id() = id;
    req.set_minimal_migration(cl->HasSwitch(switches::kMinimalMigration));

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.StartMigrateToDircrypto(req, &reply, &error,
                                                    timeout_ms) ||
        error) {
      printf("MigrateToDircrypto call failed: %s\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.error() !=
               user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("MigrateToDircrypto call failed: status %d\n",
             static_cast<int>(reply.error()));
      return 1;
    }

    printf("MigrateToDircrypto call succeeded.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_NEEDS_DIRCRYPTO_MIGRATION],
                 action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id)) {
      printf("No account_id specified.\n");
      return 1;
    }

    user_data_auth::NeedsDircryptoMigrationRequest req;
    user_data_auth::NeedsDircryptoMigrationReply reply;
    *req.mutable_account_id() = id;

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.NeedsDircryptoMigration(req, &reply, &error,
                                                    timeout_ms) ||
        error) {
      printf("NeedsDirCryptoMigration call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.error() !=
               user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("NeedsDirCryptoMigration call failed: status %d\n",
             static_cast<int>(reply.error()));
      return 1;
    }

    if (reply.needs_dircrypto_migration())
      printf("Yes\n");
    else
      printf("No\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_ENROLLMENT_ID],
                     action.c_str())) {
    attestation::GetEnrollmentIdRequest req;
    attestation::GetEnrollmentIdReply reply;
    req.set_ignore_cache(cl->HasSwitch(switches::kIgnoreCache));

    brillo::ErrorPtr error;
    if (!attestation_proxy.GetEnrollmentId(req, &reply, &error, timeout_ms) ||
        error) {
      printf("GetEnrollmentId call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    } else if (reply.status() != attestation::STATUS_SUCCESS) {
      printf("GetEnrollmentId call failed: status %d\n",
             static_cast<int>(reply.status()));
      return 1;
    }

    std::string eid_str = base::ToLowerASCII(base::HexEncode(
        reply.enrollment_id().data(), reply.enrollment_id().size()));
    printf("%s\n", eid_str.c_str());
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_GET_SUPPORTED_KEY_POLICIES],
                     action.c_str())) {
    user_data_auth::GetSupportedKeyPoliciesRequest req;
    user_data_auth::GetSupportedKeyPoliciesReply reply;

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.GetSupportedKeyPolicies(req, &reply, &error,
                                                    timeout_ms) ||
        error) {
      printf("GetSupportedKeyPolicies call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();

    printf("GetSupportedKeyPolicies success.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_GET_ACCOUNT_DISK_USAGE],
                 action.c_str())) {
    user_data_auth::GetAccountDiskUsageRequest req;
    user_data_auth::GetAccountDiskUsageReply reply;

    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;

    *req.mutable_identifier() = id;

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.GetAccountDiskUsage(req, &reply, &error,
                                                timeout_ms) ||
        error) {
      printf("GetAccountDiskUsage call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }

    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("GetAccountDiskUsage call failed: status %d\n",
             static_cast<int>(reply.error()));
      return 1;
    }

    printf("Account Disk Usage in bytes: %" PRId64 "\n", reply.size());
    return 0;
  } else if (!strcmp(
                 switches::kActions
                     [switches::ACTION_LOCK_TO_SINGLE_USER_MOUNT_UNTIL_REBOOT],
                 action.c_str())) {
    user_data_auth::LockToSingleUserMountUntilRebootRequest req;
    user_data_auth::LockToSingleUserMountUntilRebootReply reply;

    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;
    *req.mutable_account_id() = id;

    brillo::ErrorPtr error;
    if (!misc_proxy.LockToSingleUserMountUntilReboot(req, &reply, &error,
                                                     timeout_ms) ||
        error) {
      printf("LockToSingleUserMountUntilReboot call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }

    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("LockToSingleUserMountUntilReboot call failed: status %d\n",
             static_cast<int>(reply.error()));
      return 1;
    }

    printf("Login disabled.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_RSU_DEVICE_ID],
                     action.c_str())) {
    user_data_auth::GetRsuDeviceIdRequest req;
    user_data_auth::GetRsuDeviceIdReply reply;

    brillo::ErrorPtr error;
    if (!misc_proxy.GetRsuDeviceId(req, &reply, &error, timeout_ms) || error) {
      printf("GetRsuDeviceId call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }

    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("GetRsuDeviceId call failed: status %d\n",
             static_cast<int>(reply.error()));
      return 1;
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_CHECK_HEALTH],
                     action.c_str())) {
    user_data_auth::CheckHealthRequest req;
    user_data_auth::CheckHealthReply reply;

    brillo::ErrorPtr error;
    if (!misc_proxy.CheckHealth(req, &reply, &error, timeout_ms) || error) {
      printf("CheckHealth call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }

    reply.PrintDebugString();
  } else if (!strcmp(switches::kActions[switches::ACTION_START_AUTH_SESSION],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return 1;

    user_data_auth::StartAuthSessionRequest req;
    user_data_auth::StartAuthSessionReply reply;
    unsigned int flags = 0;
    flags |= cl->HasSwitch(switches::kEnsureEphemeralSwitch)
                 ? user_data_auth::AUTH_SESSION_FLAGS_EPHEMERAL_USER
                 : 0;
    req.set_flags(flags);
    *req.mutable_account_id() = id;

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.StartAuthSession(req, &reply, &error, timeout_ms) ||
        error) {
      printf("StartAuthSession call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Auth session failed to start.\n");
      return static_cast<int>(reply.error());
    }

    printf("auth_session_id:%s\n",
           base::HexEncode(reply.auth_session_id().c_str(),
                           reply.auth_session_id().size())
               .c_str());
    printf("Auth session start succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_ADD_CREDENTIALS],
                     action.c_str())) {
    user_data_auth::AddCredentialsRequest req;
    user_data_auth::AddCredentialsReply reply;

    std::string auth_session_id_hex, auth_session_id;
    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);
    req.set_auth_session_id(auth_session_id);

    if (!BuildAuthorization(
            cl, &misc_proxy,
            !cl->HasSwitch(switches::kPublicMount) /* need_credential */,
            req.mutable_authorization()))
      return 1;

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.AddCredentials(req, &reply, &error, timeout_ms) ||
        error) {
      printf("AddCredentials call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Auth session failed to add credentials.\n");
      return static_cast<int>(reply.error());
    }

    printf("Auth session added credentials successfully.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_AUTHENTICATE_AUTH_SESSION],
                 action.c_str())) {
    user_data_auth::AuthenticateAuthSessionRequest req;
    user_data_auth::AuthenticateAuthSessionReply reply;

    std::string auth_session_id_hex, auth_session_id;
    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);

    req.set_auth_session_id(auth_session_id);

    if (!BuildAuthorization(
            cl, &misc_proxy,
            !cl->HasSwitch(switches::kPublicMount) /* need_credential */,
            req.mutable_authorization()))
      return 1;

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.AuthenticateAuthSession(req, &reply, &error,
                                                    timeout_ms) ||
        error) {
      printf("AuthenticateAuthSession call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Auth session failed to authenticate.\n");
      return static_cast<int>(reply.error());
    }

    printf("Auth session authentication succeeded.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_INVALIDATE_AUTH_SESSION],
                 action.c_str())) {
    user_data_auth::InvalidateAuthSessionRequest req;
    user_data_auth::InvalidateAuthSessionReply reply;

    std::string auth_session_id_hex, auth_session_id;

    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);
    req.set_auth_session_id(auth_session_id);

    brillo::ErrorPtr error;
    VLOG(1) << "Attempting to invalidate auth session";
    if (!userdataauth_proxy.InvalidateAuthSession(req, &reply, &error,
                                                  timeout_ms) ||
        error) {
      printf("InvalidateAuthSession call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Auth session failed to invalidate.\n");
      return static_cast<int>(reply.error());
    }

    printf("Auth session invalidated.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_EXTEND_AUTH_SESSION],
                     action.c_str())) {
    user_data_auth::ExtendAuthSessionRequest req;
    user_data_auth::ExtendAuthSessionReply reply;

    std::string auth_session_id_hex, auth_session_id;

    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);
    req.set_auth_session_id(auth_session_id);

    // Parse extension duration from string to integer.
    std::string extension_duration_str =
        cl->GetSwitchValueASCII(switches::kExtensionDuration);
    // Default value to extend is 60 seconds, if not specified.
    int extension_duration = 60;
    if (extension_duration_str.empty()) {
      printf("Extension duration not specified, using default of 60 seconds\n");
    } else if (!base::StringToInt(extension_duration_str,
                                  &extension_duration)) {
      printf(
          "Extension duration specified is not a valid duration"
          "(--%s=<extension_duration>)\n",
          switches::kExtensionDuration);
      return 1;
    } else if (extension_duration < 0) {
      printf(
          "Extension duration specified is a negative value"
          "(--%s=<extension_duration>)\n",
          switches::kExtensionDuration);
      return 1;
    }
    req.set_extension_duration(extension_duration);

    brillo::ErrorPtr error;
    VLOG(1) << "Attempting to extend auth session";
    if (!userdataauth_proxy.ExtendAuthSession(req, &reply, &error,
                                              timeout_ms) ||
        error) {
      printf("ExtendAuthSession call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Auth session failed to extend.\n");
      return static_cast<int>(reply.error());
    }

    printf("Auth session extended.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_CREATE_PERSISTENT_USER],
                 action.c_str())) {
    user_data_auth::CreatePersistentUserRequest req;
    user_data_auth::CreatePersistentUserReply reply;

    std::string auth_session_id_hex, auth_session_id;
    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);

    req.set_auth_session_id(auth_session_id);

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.CreatePersistentUser(req, &reply, &error,
                                                 timeout_ms) ||
        error) {
      printf("CreatePersistentUser call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Failed to create persistent user.\n");
      return static_cast<int>(reply.error());
    }

    printf("Created persistent user.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_PREPARE_GUEST_VAULT],
                     action.c_str())) {
    user_data_auth::PrepareGuestVaultRequest req;
    user_data_auth::PrepareGuestVaultReply reply;

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.PrepareGuestVault(req, &reply, &error,
                                              timeout_ms) ||
        error) {
      printf("PrepareGuestVault call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Failed to prepare guest vault.\n");
      return static_cast<int>(reply.error());
    }

    printf("Prepared guest vault.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_PREPARE_EPHEMERAL_VAULT],
                 action.c_str())) {
    user_data_auth::PrepareEphemeralVaultRequest req;
    user_data_auth::PrepareEphemeralVaultReply reply;

    std::string auth_session_id_hex, auth_session_id;
    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);

    req.set_auth_session_id(auth_session_id);

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.PrepareEphemeralVault(req, &reply, &error,
                                                  timeout_ms) ||
        error) {
      printf("PrepareEphemeralVault call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Failed to prepare ephemeral vault.\n");
      return static_cast<int>(reply.error());
    }

    printf("Prepared ephemeral vault.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_PREPARE_PERSISTENT_VAULT],
                 action.c_str())) {
    user_data_auth::PreparePersistentVaultRequest req;
    user_data_auth::PreparePersistentVaultReply reply;

    std::string auth_session_id_hex, auth_session_id;
    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);

    req.set_auth_session_id(auth_session_id);
    if (cl->HasSwitch(switches::kEcryptfsSwitch)) {
      req.set_encryption_type(
          user_data_auth::CRYPTOHOME_VAULT_ENCRYPTION_ECRYPTFS);
    }

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.PreparePersistentVault(req, &reply, &error,
                                                   timeout_ms) ||
        error) {
      printf("PreparePersistentVault call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Failed to prepare persistent vault.\n");
      return static_cast<int>(reply.error());
    }

    printf("Prepared persistent vault.\n");
  } else if (!strcmp(switches::kActions
                         [switches::ACTION_PREPARE_VAULT_FOR_MIGRATION],
                     action.c_str())) {
    user_data_auth::PrepareVaultForMigrationRequest req;
    user_data_auth::PrepareVaultForMigrationReply reply;

    std::string auth_session_id_hex, auth_session_id;
    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);

    req.set_auth_session_id(auth_session_id);

    brillo::ErrorPtr error;
    if (!userdataauth_proxy.PrepareVaultForMigration(req, &reply, &error,
                                                     timeout_ms) ||
        error) {
      printf("PrepareVaultForMigration call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Failed to prepare vault for migration.\n");
      return static_cast<int>(reply.error());
    }

    printf("Prepared vault for migration.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_ADD_AUTH_FACTOR],
                     action.c_str())) {
    user_data_auth::AddAuthFactorRequest req;
    user_data_auth::AddAuthFactorReply reply;

    std::string auth_session_id_hex, auth_session_id;

    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);
    req.set_auth_session_id(auth_session_id);
    // TODO(b/3319388): Implement building AuthFactor for request.
    brillo::ErrorPtr error;
    VLOG(1) << "Attempting to add AuthFactor";
    if (!userdataauth_proxy.AddAuthFactor(req, &reply, &error, timeout_ms) ||
        error) {
      printf("AddAuthFactor call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Failed to AddAuthFactor.\n");
      return static_cast<int>(reply.error());
    }

    printf("AuthFactor added.\n");
  } else if (!strcmp(
                 switches::kActions[switches::ACTION_AUTHENTICATE_AUTH_FACTOR],
                 action.c_str())) {
    user_data_auth::AuthenticateAuthFactorRequest req;
    user_data_auth::AuthenticateAuthFactorReply reply;

    std::string auth_session_id_hex, auth_session_id;

    if (!GetAuthSessionId(cl, &auth_session_id_hex))
      return 1;
    base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);
    req.set_auth_session_id(auth_session_id);
    // TODO(b/208358041): Implement building AuthFactor for request.
    brillo::ErrorPtr error;
    VLOG(1) << "Attempting to authenticate AuthFactor";
    if (!userdataauth_proxy.AuthenticateAuthFactor(req, &reply, &error,
                                                   timeout_ms) ||
        error) {
      printf("AuthenticateAuthFactor call failed: %s.\n",
             BrilloErrorToString(error.get()).c_str());
      return 1;
    }
    reply.PrintDebugString();
    if (reply.error() !=
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
      printf("Failed to authenticate AuthFactor.\n");
      return static_cast<int>(reply.error());
    }

    printf("AuthFactor authenticated.\n");
  } else {
    printf("Unknown action or no action given.  Available actions:\n");
    for (int i = 0; switches::kActions[i]; i++)
      printf("  --action=%s\n", switches::kActions[i]);
  }
  return 0;
}  // NOLINT(readability/fn_size)

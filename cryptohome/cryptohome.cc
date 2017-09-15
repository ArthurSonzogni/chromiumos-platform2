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

#include <memory>
#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/glib/dbus.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <chromeos/constants/cryptohome.h>
#include <chromeos/dbus/service_constants.h>
#include <google/protobuf/message_lite.h>

#include "cryptohome/attestation.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/mount.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_live_test.h"
#include "cryptohome/username_passkey.h"

#include "attestation.pb.h"  // NOLINT(build/include)
#include "bindings/cryptohome.dbusclient.h"
#include "key.pb.h"  // NOLINT(build/include)
#include "rpc.pb.h"  // NOLINT(build/include)
#include "signed_secret.pb.h"  // NOLINT(build/include)
#include "vault_keyset.pb.h"  // NOLINT(build/include)

using base::FilePath;
using base::StringPrintf;
using brillo::SecureBlob;

namespace {
// Number of days that the set_current_user_old action uses when updating the
// home directory timestamp.  ~3 months should be old enough for test purposes.
const int kSetCurrentUserOldOffsetInDays = 92;

// Five minutes is enough to wait for any TPM operations, sync() calls, etc.
const int kDefaultTimeoutMs = 300000;
}  // namespace

namespace switches {
  static const char kSyslogSwitch[] = "syslog";
  static const char kAttestationServerSwitch[] = "attestation-server";
  static struct {
    const char *name;
    const cryptohome::Attestation::PCAType pca_type;
  } kAttestationServers[] = {
    { "default",  cryptohome::Attestation::kDefaultPCA },
    { "test",     cryptohome::Attestation::kTestPCA },
    { nullptr,    cryptohome::Attestation::kMaxPCAType }
  };
  static const char kActionSwitch[] = "action";
  static const char* kActions[] = {"mount",
                                   "mount_ex",
                                   "mount_guest",
                                   "mount_public",
                                   "unmount",
                                   "is_mounted",
                                   "test_auth",
                                   "check_key_ex",
                                   "remove_key_ex",
                                   "get_key_data_ex",
                                   "list_keys_ex",
                                   "migrate_key",
                                   "add_key",
                                   "add_key_ex",
                                   "update_key_ex",
                                   "remove",
                                   "obfuscate_user",
                                   "dump_keyset",
                                   "dump_last_activity",
                                   "tpm_status",
                                   "tpm_more_status",
                                   "status",
                                   "set_current_user_old",
                                   "do_free_disk_space_control",
                                   "tpm_take_ownership",
                                   "tpm_clear_stored_password",
                                   "tpm_wait_ownership",
                                   "install_attributes_set",
                                   "install_attributes_get",
                                   "install_attributes_finalize",
                                   "pkcs11_token_status",
                                   "pkcs11_terminate",
                                   "store_enrollment_state",
                                   "load_enrollment_state",
                                   "tpm_live_test",
                                   "tpm_verify_attestation",
                                   "tpm_verify_ek",
                                   "tpm_attestation_status",
                                   "tpm_attestation_start_enroll",
                                   "tpm_attestation_finish_enroll",
                                   "tpm_attestation_start_cert_request",
                                   "tpm_attestation_finish_cert_request",
                                   "tpm_attestation_key_status",
                                   "tpm_attestation_register_key",
                                   "tpm_attestation_enterprise_challenge",
                                   "tpm_attestation_delete",
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
                                   NULL};
  enum ActionEnum {
    ACTION_MOUNT,
    ACTION_MOUNT_EX,
    ACTION_MOUNT_GUEST,
    ACTION_MOUNT_PUBLIC,
    ACTION_UNMOUNT,
    ACTION_MOUNTED,
    ACTION_TEST_AUTH,
    ACTION_CHECK_KEY_EX,
    ACTION_REMOVE_KEY_EX,
    ACTION_GET_KEY_DATA_EX,
    ACTION_LIST_KEYS_EX,
    ACTION_MIGRATE_KEY,
    ACTION_ADD_KEY,
    ACTION_ADD_KEY_EX,
    ACTION_UPDATE_KEY_EX,
    ACTION_REMOVE,
    ACTION_OBFUSCATE_USER,
    ACTION_DUMP_KEYSET,
    ACTION_DUMP_LAST_ACTIVITY,
    ACTION_TPM_STATUS,
    ACTION_TPM_MORE_STATUS,
    ACTION_STATUS,
    ACTION_SET_CURRENT_USER_OLD,
    ACTION_DO_FREE_DISK_SPACE_CONTROL,
    ACTION_TPM_TAKE_OWNERSHIP,
    ACTION_TPM_CLEAR_STORED_PASSWORD,
    ACTION_TPM_WAIT_OWNERSHIP,
    ACTION_INSTALL_ATTRIBUTES_SET,
    ACTION_INSTALL_ATTRIBUTES_GET,
    ACTION_INSTALL_ATTRIBUTES_FINALIZE,
    ACTION_PKCS11_TOKEN_STATUS,
    ACTION_PKCS11_TERMINATE,
    ACTION_STORE_ENROLLMENT,
    ACTION_LOAD_ENROLLMENT,
    ACTION_TPM_LIVE_TEST,
    ACTION_TPM_VERIFY_ATTESTATION,
    ACTION_TPM_VERIFY_EK,
    ACTION_TPM_ATTESTATION_STATUS,
    ACTION_TPM_ATTESTATION_START_ENROLL,
    ACTION_TPM_ATTESTATION_FINISH_ENROLL,
    ACTION_TPM_ATTESTATION_START_CERTREQ,
    ACTION_TPM_ATTESTATION_FINISH_CERTREQ,
    ACTION_TPM_ATTESTATION_KEY_STATUS,
    ACTION_TPM_ATTESTATION_REGISTER_KEY,
    ACTION_TPM_ATTESTATION_ENTERPRISE_CHALLENGE,
    ACTION_TPM_ATTESTATION_DELETE,
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
  };
  static const char kUserSwitch[] = "user";
  static const char kPasswordSwitch[] = "password";
  static const char kTpmOwnerPasswordSwitch[] = "owner_password";
  static const char kKeyLabelSwitch[] = "key_label";
  static const char kKeyRevisionSwitch[] = "key_revision";
  static const char kHmacSigningKeySwitch[] = "hmac_signing_key";
  static const char kNewKeyLabelSwitch[] = "new_key_label";
  static const char kRemoveKeyLabelSwitch[] = "remove_key_label";
  static const char kOldPasswordSwitch[] = "old_password";
  static const char kNewPasswordSwitch[] = "new_password";
  static const char kForceSwitch[] = "force";
  static const char kAsyncSwitch[] = "async";
  static const char kCreateSwitch[] = "create";
  static const char kAttrNameSwitch[] = "name";
  static const char kAttrValueSwitch[] = "value";
  static const char kFileSwitch[] = "file";
  static const char kEnsureEphemeralSwitch[] = "ensure_ephemeral";
  static const char kCrosCoreSwitch[] = "cros_core";
  static const char kProtobufSwitch[] = "protobuf";
  static const char kFlagsSwitch[] = "flags";
  static const char kDevKeyHashSwitch[] = "developer_key_hash";
  static const char kEcryptfsSwitch[] = "ecryptfs";
  static const char kToMigrateFromEcryptfsSwitch[] = "to_migrate_from_ecryptfs";
  static const char kHiddenMount[] = "hidden_mount";
  static const char kMinimalMigration[] = "minimal_migration";
}  // namespace switches

#define DBUS_METHOD(method_name) \
    org_chromium_CryptohomeInterface_ ## method_name

typedef void (*ProtoDBusReplyMethod)(DBusGProxy*, GArray*, GError*, gpointer);
typedef gboolean (*ProtoDBusMethod)(
    DBusGProxy*, const GArray*, GArray**, GError**);
typedef DBusGProxyCall* (*ProtoDBusAsyncMethod)(
    DBusGProxy*, const GArray*, ProtoDBusReplyMethod, gpointer);

brillo::Blob GetSystemSalt(const brillo::dbus::Proxy& proxy) {
  brillo::glib::ScopedError error;
  brillo::glib::ScopedArray salt;
  if (!org_chromium_CryptohomeInterface_get_system_salt(proxy.gproxy(),
      &brillo::Resetter(&salt).lvalue(),
      &brillo::Resetter(&error).lvalue())) {
    LOG(ERROR) << "GetSystemSalt failed: " << error->message;
    return brillo::Blob();
  }

  brillo::Blob system_salt;
  system_salt.resize(salt->len);
  if (system_salt.size() == salt->len) {
    memcpy(system_salt.data(), static_cast<const void*>(salt->data), salt->len);
  } else {
    system_salt.clear();
  }
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

bool GetPassword(const brillo::dbus::Proxy& proxy,
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
    if (fgets(buffer, arraysize(buffer), stdin))
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

FilePath GetFile(const base::CommandLine* cl) {
  const char kDefaultFilePath[] = "/tmp/__cryptohome";
  FilePath file_path(cl->GetSwitchValueASCII(switches::kFileSwitch));
  if (file_path.empty()) {
    return FilePath(kDefaultFilePath);
  }
  return file_path;
}

bool ConfirmRemove(const std::string& user) {
  printf("!!! Are you sure you want to remove the user's cryptohome?\n");
  printf("!!!\n");
  printf("!!! Re-enter the username at the prompt to remove the\n");
  printf("!!! cryptohome for the user.\n");
  printf("Enter the username <%s>: ", user.c_str());
  fflush(stdout);

  char buffer[256];
  if (!fgets(buffer, arraysize(buffer), stdin)) {
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

GArray* GArrayFromProtoBuf(const google::protobuf::MessageLite& pb) {
  guint len = pb.ByteSize();
  GArray* ary = g_array_sized_new(FALSE, FALSE, 1, len);
  g_array_set_size(ary, len);
  if (!pb.SerializeToArray(ary->data, len)) {
    printf("Failed to serialize protocol buffer.\n");
    return NULL;
  }
  return ary;
}

bool BuildAccountId(base::CommandLine* cl, cryptohome::AccountIdentifier *id) {
  std::string account_id;
  if (!GetAccountId(cl, &account_id)) {
    printf("No account_id specified.\n");
    return false;
  }
  id->set_account_id(account_id);
  return true;
}

bool BuildAuthorization(base::CommandLine* cl,
                        const brillo::dbus::Proxy& proxy,
                        cryptohome::AuthorizationRequest* auth) {
  std::string password;
  GetPassword(proxy, cl, switches::kPasswordSwitch,
              "Enter the password",
              &password);

  auth->mutable_key()->set_secret(password);
  if (cl->HasSwitch(switches::kKeyLabelSwitch)) {
    auth->mutable_key()->mutable_data()->set_label(
        cl->GetSwitchValueASCII(switches::kKeyLabelSwitch));
  }

  return true;
}

void ParseBaseReply(GArray* reply_ary, cryptohome::BaseReply* reply) {
  if (!reply)
    return;
  if (!reply->ParseFromArray(reply_ary->data, reply_ary->len)) {
    printf("Failed to parse reply.\n");
    exit(-1);
  }
  reply->PrintDebugString();
}

class ClientLoop {
 public:
  ClientLoop()
      : loop_(NULL),
        async_call_id_(0),
        return_status_(false),
        return_code_(0) { }

  virtual ~ClientLoop() {
    if (loop_) {
      g_main_loop_unref(loop_);
    }
  }

  void Initialize(brillo::dbus::Proxy* proxy) {
    dbus_g_object_register_marshaller(g_cclosure_marshal_generic,
                                      G_TYPE_NONE,
                                      G_TYPE_INT,
                                      G_TYPE_BOOLEAN,
                                      G_TYPE_INT,
                                      G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy->gproxy(), "AsyncCallStatus",
                            G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_INT,
                            G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(proxy->gproxy(), "AsyncCallStatus",
                                G_CALLBACK(ClientLoop::CallbackThunk),
                                this, NULL);
    dbus_g_object_register_marshaller(g_cclosure_marshal_generic,
                                      G_TYPE_NONE,
                                      G_TYPE_INT,
                                      G_TYPE_BOOLEAN,
                                      DBUS_TYPE_G_UCHAR_ARRAY,
                                      G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy->gproxy(), "AsyncCallStatusWithData",
                            G_TYPE_INT, G_TYPE_BOOLEAN, DBUS_TYPE_G_UCHAR_ARRAY,
                            G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(proxy->gproxy(), "AsyncCallStatusWithData",
                                G_CALLBACK(ClientLoop::CallbackDataThunk),
                                this, NULL);
    loop_ = g_main_loop_new(NULL, TRUE);
  }

  void Run(int async_call_id) {
    async_call_id_ = async_call_id;
    g_main_loop_run(loop_);
  }

  void Run() {
    Run(0);
  }

  // This callback can be used with a ClientLoop instance as the |userdata| to
  // handle an asynchronous reply which emits a serialized BaseReply.
  static void ParseReplyThunk(DBusGProxy* proxy,
                              GArray* data,
                              GError* error,
                              gpointer userdata) {
    reinterpret_cast<ClientLoop*>(userdata)->ParseReply(data, error);
  }

  bool get_return_status() {
    return return_status_;
  }

  int get_return_code() {
    return return_code_;
  }

  std::string get_return_data() {
    return return_data_;
  }

  cryptohome::BaseReply reply() {
    return reply_;
  }

 private:
  void Callback(int async_call_id, bool return_status, int return_code) {
    if (async_call_id == async_call_id_) {
      return_status_ = return_status;
      return_code_ = return_code;
      g_main_loop_quit(loop_);
    }
  }

  void CallbackWithData(int async_call_id, bool return_status, GArray* data) {
    if (async_call_id == async_call_id_) {
      return_status_ = return_status;
      return_data_ = std::string(static_cast<char*>(data->data), data->len);
      g_main_loop_quit(loop_);
    }
  }

  void ParseReply(GArray* reply_ary,
                  GError* error) {
    if (error && error->message) {
      printf("Call error: %s\n", error->message);
      exit(-1);
    }
    ParseBaseReply(reply_ary, &reply_);
    g_main_loop_quit(loop_);
  }

  static void CallbackThunk(DBusGProxy* proxy,
                            int async_call_id, bool return_status,
                            int return_code, gpointer userdata) {
    reinterpret_cast<ClientLoop*>(userdata)->Callback(async_call_id,
                                                      return_status,
                                                      return_code);
  }

  static void CallbackDataThunk(DBusGProxy* proxy,
                                int async_call_id,
                                bool return_status,
                                GArray* data,
                                gpointer userdata) {
    reinterpret_cast<ClientLoop*>(userdata)->CallbackWithData(async_call_id,
                                                              return_status,
                                                              data);
  }

  GMainLoop *loop_;
  int async_call_id_;
  bool return_status_;
  int return_code_;
  std::string return_data_;
  cryptohome::BaseReply reply_;
};

class TpmWaitLoop {
 public:
  TpmWaitLoop()
      : loop_(NULL) { }

  virtual ~TpmWaitLoop() {
    if (loop_) {
      g_main_loop_unref(loop_);
    }
  }

  void Initialize(brillo::dbus::Proxy* proxy) {
    dbus_g_object_register_marshaller(g_cclosure_marshal_generic,
                                      G_TYPE_NONE,
                                      G_TYPE_BOOLEAN,
                                      G_TYPE_BOOLEAN,
                                      G_TYPE_BOOLEAN,
                                      G_TYPE_INVALID);
    dbus_g_proxy_add_signal(proxy->gproxy(), "TpmInitStatus",
                            G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
                            G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(proxy->gproxy(), "TpmInitStatus",
                                G_CALLBACK(TpmWaitLoop::CallbackThunk),
                                this, NULL);
    loop_ = g_main_loop_new(NULL, TRUE);
  }

  void Run() {
    g_main_loop_run(loop_);
  }

 private:
  static void CallbackThunk(DBusGProxy* proxy, bool ready, bool is_owned,
                            bool took_ownership, gpointer userdata) {
    printf("TPM ready: %s\n", (ready ? "true" : "false"));
    printf("TPM owned: %s\n", (is_owned ? "true" : "false"));
    printf("TPM took_ownership: %s\n", (took_ownership ? "true" : "false"));
    g_main_loop_quit(reinterpret_cast<TpmWaitLoop*>(userdata)->loop_);
  }

  GMainLoop *loop_;
};

bool WaitForTPMOwnership(brillo::dbus::Proxy* proxy) {
  TpmWaitLoop client_loop;
  client_loop.Initialize(proxy);
  gboolean result;
  brillo::glib::ScopedError error;
  if (!org_chromium_CryptohomeInterface_tpm_is_being_owned(
          proxy->gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
    printf("TpmIsBeingOwned call failed: %s.\n", error->message);
  }
  if (result) {
    printf("Waiting for TPM to be owned...\n");
    client_loop.Run();
  } else {
    printf("TPM is not currently being owned.\n");
  }
  return result;
}

bool MakeProtoDBusCall(const std::string& name,
                       ProtoDBusMethod method,
                       ProtoDBusAsyncMethod async_method,
                       base::CommandLine* cl,
                       brillo::dbus::Proxy* proxy,
                       const google::protobuf::MessageLite& request,
                       cryptohome::BaseReply* reply) {
  brillo::glib::ScopedArray request_ary(GArrayFromProtoBuf(request));
  if (cl->HasSwitch(switches::kAsyncSwitch)) {
    ClientLoop loop;
    loop.Initialize(proxy);
    DBusGProxyCall* call = (*async_method)(proxy->gproxy(),
                                           request_ary.get(),
                                           &ClientLoop::ParseReplyThunk,
                                           static_cast<gpointer>(&loop));
    if (!call) {
      printf("Failed to call %s!\n", name.c_str());
      return false;
    }
    loop.Run();
    *reply = loop.reply();
  } else {
    brillo::glib::ScopedError error;
    brillo::glib::ScopedArray reply_ary;
    if (!(*method)(proxy->gproxy(),
                   request_ary.get(),
                   &brillo::Resetter(&reply_ary).lvalue(),
                   &brillo::Resetter(&error).lvalue())) {
      printf("Failed to call %s: %s\n", name.c_str(), error->message);
      return false;
    }
    ParseBaseReply(reply_ary.get(), reply);
  }
  if (reply->has_error()) {
    printf("%s error: %d\n", name.c_str(), reply->error());
    return false;
  }

  return true;
}

int main(int argc, char **argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine *cl = base::CommandLine::ForCurrentProcess();
  if (cl->HasSwitch(switches::kSyslogSwitch))
    brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  else
    brillo::InitLog(brillo::kLogToStderr);

  cryptohome::Attestation::PCAType pca_type =
      cryptohome::Attestation::kDefaultPCA;
  if (cl->HasSwitch(switches::kAttestationServerSwitch)) {
    std::string server =
        cl->GetSwitchValueASCII(switches::kAttestationServerSwitch);
    for (int i = 0; switches::kAttestationServers[i].name; ++i) {
      if (server == switches::kAttestationServers[i].name) {
        pca_type = switches::kAttestationServers[i].pca_type;
        break;
      }
    }
  }

  std::string action = cl->GetSwitchValueASCII(switches::kActionSwitch);
  brillo::dbus::BusConnection bus = brillo::dbus::GetSystemBusConnection();
  brillo::dbus::Proxy proxy(bus,
                              cryptohome::kCryptohomeServiceName,
                              cryptohome::kCryptohomeServicePath,
                              cryptohome::kCryptohomeInterface);
  DCHECK(proxy.gproxy()) << "Failed to acquire proxy";
  dbus_g_proxy_set_default_timeout(proxy.gproxy(), kDefaultTimeoutMs);

  cryptohome::Platform platform;

  if (!strcmp(switches::kActions[switches::ACTION_MOUNT], action.c_str())) {
    std::string account_id, password;

    if (!GetAccountId(cl, &account_id)) {
      printf("No account_id specified.\n");
      return 1;
    }

    GetPassword(proxy, cl, switches::kPasswordSwitch,
                StringPrintf("Enter the password for <%s>", account_id.c_str()),
                &password);

    gboolean done = false;
    gint mount_error = 0;
    brillo::glib::ScopedError error;

    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      if (!org_chromium_CryptohomeInterface_mount(proxy.gproxy(),
               account_id.c_str(),
               password.c_str(),
               cl->HasSwitch(switches::kCreateSwitch),
               cl->HasSwitch(switches::kEnsureEphemeralSwitch),
               NULL,
               &mount_error,
               &done,
               &brillo::Resetter(&error).lvalue())) {
        printf("Mount call failed: %s, with reason code: %d.\n", error->message,
               mount_error);
      }
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_mount(proxy.gproxy(),
               account_id.c_str(),
               password.c_str(),
               cl->HasSwitch(switches::kCreateSwitch),
               cl->HasSwitch(switches::kEnsureEphemeralSwitch),
               NULL,
               &async_id,
               &brillo::Resetter(&error).lvalue())) {
        printf("Mount call failed: %s.\n", error->message);
      } else {
        client_loop.Run(async_id);
        done = client_loop.get_return_status();
      }
    }
    if (!done) {
      printf("Mount failed.\n");
    } else {
      printf("Mount succeeded.\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_MOUNT_EX],
                action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return -1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, &auth))
      return -1;

    cryptohome::MountRequest mount_req;
    mount_req.set_require_ephemeral(
        cl->HasSwitch(switches::kEnsureEphemeralSwitch));
    mount_req.set_to_migrate_from_ecryptfs(
        cl->HasSwitch(switches::kToMigrateFromEcryptfsSwitch));
    mount_req.set_hidden_mount(cl->HasSwitch(switches::kHiddenMount));
    if (cl->HasSwitch(switches::kCreateSwitch)) {
      cryptohome::CreateRequest* create = mount_req.mutable_create();
      create->set_copy_authorization_key(true);
      if (cl->HasSwitch(switches::kEcryptfsSwitch)) {
        create->set_force_ecryptfs(true);
      }
    }

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(mount_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return -1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call = org_chromium_CryptohomeInterface_mount_ex_async(
                                 proxy.gproxy(),
                                 account_ary.get(),
                                 auth_ary.get(),
                                 req_ary.get(),
                                 &ClientLoop::ParseReplyThunk,
                                 static_cast<gpointer>(&loop));
      if (!call)
        return -1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_mount_ex(proxy.gproxy(),
            account_ary.get(),
            auth_ary.get(),
            req_ary.get(),
            &out_reply,
            &brillo::Resetter(&error).lvalue())) {
        printf("MountEx call failed: %s", error->message);
        return -1;
      }
      ParseBaseReply(out_reply, &reply);
    }
    if (reply.has_error()) {
      printf("Mount failed.\n");
      return reply.error();
    }
    printf("Mount succeeded.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MOUNT_GUEST],
                action.c_str())) {
    gboolean done = false;
    gint mount_error = 0;
    brillo::glib::ScopedError error;

    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      if (!org_chromium_CryptohomeInterface_mount_guest(proxy.gproxy(),
               &mount_error,
               &done,
               &brillo::Resetter(&error).lvalue())) {
        printf("MountGuest call failed: %s, with reason code: %d.\n",
               error->message, mount_error);
      }
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_mount_guest(proxy.gproxy(),
               &async_id,
               &brillo::Resetter(&error).lvalue())) {
        printf("Mount call failed: %s.\n", error->message);
      } else {
        client_loop.Run(async_id);
        done = client_loop.get_return_status();
      }
    }
    if (!done) {
      printf("Mount failed.\n");
    } else {
      printf("Mount succeeded.\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_MOUNT_PUBLIC],
                     action.c_str())) {
    std::string account_id;

    if (!GetAccountId(cl, &account_id)) {
      printf("No account_id specified.\n");
      return 1;
    }

    gboolean done = false;
    gint mount_error = 0;
    brillo::glib::ScopedError error;

    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      if (!org_chromium_CryptohomeInterface_mount_public(proxy.gproxy(),
               account_id.c_str(),
               cl->HasSwitch(switches::kCreateSwitch),
               cl->HasSwitch(switches::kEnsureEphemeralSwitch),
               &mount_error,
               &done,
               &brillo::Resetter(&error).lvalue())) {
        printf("Mount call failed: %s, with reason code: %d.\n", error->message,
               mount_error);
      }
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_mount_public(proxy.gproxy(),
               account_id.c_str(),
               cl->HasSwitch(switches::kCreateSwitch),
               cl->HasSwitch(switches::kEnsureEphemeralSwitch),
               &async_id,
               &brillo::Resetter(&error).lvalue())) {
        printf("Mount call failed: %s.\n", error->message);
      } else {
        client_loop.Run(async_id);
        done = client_loop.get_return_status();
      }
    }
    if (!done) {
      printf("Mount failed.\n");
    } else {
      printf("Mount succeeded.\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TEST_AUTH],
                     action.c_str())) {
    std::string account_id, password;

    if (!GetAccountId(cl, &account_id)) {
      printf("No account_id specified.\n");
      return 1;
    }

    GetPassword(proxy, cl, switches::kPasswordSwitch,
                StringPrintf("Enter the password for <%s>", account_id.c_str()),
                &password);

    gboolean done = false;
    brillo::glib::ScopedError error;

    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      if (!org_chromium_CryptohomeInterface_check_key(proxy.gproxy(),
               account_id.c_str(),
               password.c_str(),
               &done,
               &brillo::Resetter(&error).lvalue())) {
        printf("CheckKey call failed: %s.\n", error->message);
      }
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_check_key(proxy.gproxy(),
               account_id.c_str(),
               password.c_str(),
               &async_id,
               &brillo::Resetter(&error).lvalue())) {
        printf("CheckKey call failed: %s.\n", error->message);
      } else {
        client_loop.Run(async_id);
        done = client_loop.get_return_status();
      }
    }
    if (!done) {
      printf("Authentication failed.\n");
    } else {
      printf("Authentication succeeded.\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_REMOVE_KEY_EX],
                action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return -1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, &auth))
      return -1;

    cryptohome::RemoveKeyRequest remove_req;
    cryptohome::KeyData* data = remove_req.mutable_key()->mutable_data();
    data->set_label(cl->GetSwitchValueASCII(switches::kRemoveKeyLabelSwitch));

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(remove_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return -1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
           org_chromium_CryptohomeInterface_remove_key_ex_async(
               proxy.gproxy(),
               account_ary.get(),
               auth_ary.get(),
               req_ary.get(),
               &ClientLoop::ParseReplyThunk,
               static_cast<gpointer>(&loop));
      if (!call)
        return -1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_remove_key_ex(proxy.gproxy(),
            account_ary.get(),
            auth_ary.get(),
            req_ary.get(),
            &out_reply,
            &brillo::Resetter(&error).lvalue())) {
        printf("RemoveKeyEx call failed: %s", error->message);
        return -1;
      }
      ParseBaseReply(out_reply, &reply);
    }
    if (reply.has_error()) {
      printf("Key removal failed.\n");
      return reply.error();
    }
    printf("Key removed.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_KEY_DATA_EX],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id)) {
      return -1;
    }
    cryptohome::AuthorizationRequest auth;
    cryptohome::GetKeyDataRequest key_data_req;
    const std::string label =
      cl->GetSwitchValueASCII(switches::kKeyLabelSwitch);
    if (label.empty()) {
      printf("No key_label specified.\n");
      return -1;
    }
    key_data_req.mutable_key()->mutable_data()->set_label(label);

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(key_data_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get()) {
      return -1;
    }

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
           org_chromium_CryptohomeInterface_get_key_data_ex_async(
               proxy.gproxy(),
               account_ary.get(),
               auth_ary.get(),
               req_ary.get(),
               &ClientLoop::ParseReplyThunk,
               static_cast<gpointer>(&loop));
      if (!call) {
        return -1;
      }
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_get_key_data_ex(proxy.gproxy(),
            account_ary.get(),
            auth_ary.get(),
            req_ary.get(),
            &out_reply,
            &brillo::Resetter(&error).lvalue())) {
        printf("GetKeyDataEx call failed: %s", error->message);
        return -1;
      }
      ParseBaseReply(out_reply, &reply);
    }
    if (reply.has_error()) {
      printf("Key retrieval failed.\n");
      return reply.error();
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_LIST_KEYS_EX],
                action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return -1;
    cryptohome::AuthorizationRequest auth;

    cryptohome::ListKeysRequest list_keys_req;

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(list_keys_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return -1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
           org_chromium_CryptohomeInterface_list_keys_ex_async(
               proxy.gproxy(),
               account_ary.get(),
               auth_ary.get(),
               req_ary.get(),
               &ClientLoop::ParseReplyThunk,
               static_cast<gpointer>(&loop));
      if (!call) {
        return -1;
      }
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_list_keys_ex(proxy.gproxy(),
            account_ary.get(),
            auth_ary.get(),
            req_ary.get(),
            &out_reply,
            &brillo::Resetter(&error).lvalue())) {
        printf("ListKeysEx call failed: %s", error->message);
        return -1;
      }
      ParseBaseReply(out_reply, &reply);
    }
    if (reply.has_error()) {
      printf("Failed to list keys.\n");
      return reply.error();
    }
    if (!reply.HasExtension(cryptohome::ListKeysReply::reply)) {
      printf("ListKeysReply missing.\n");
      return -1;
    }
    cryptohome::ListKeysReply list_keys_reply =
        reply.GetExtension(cryptohome::ListKeysReply::reply);
    for (int i = 0; i < list_keys_reply.labels_size(); ++i) {
      printf("Label: %s\n", list_keys_reply.labels(i).c_str());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_CHECK_KEY_EX],
                action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return -1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, &auth))
      return -1;

    cryptohome::CheckKeyRequest check_req;
    // TODO(wad) Add a privileges cl interface

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(check_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return -1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
           org_chromium_CryptohomeInterface_check_key_ex_async(
               proxy.gproxy(),
               account_ary.get(),
               auth_ary.get(),
               req_ary.get(),
               &ClientLoop::ParseReplyThunk,
               static_cast<gpointer>(&loop));
      if (!call)
        return -1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_check_key_ex(proxy.gproxy(),
            account_ary.get(),
            auth_ary.get(),
            req_ary.get(),
            &out_reply,
            &brillo::Resetter(&error).lvalue())) {
        printf("CheckKeyEx call failed: %s", error->message);
        return -1;
      }
      ParseBaseReply(out_reply, &reply);
    }
    if (reply.has_error()) {
      printf("Key authentication failed.\n");
      return reply.error();
    }
    printf("Key authenticated.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MIGRATE_KEY],
                     action.c_str())) {
    std::string account_id, password, old_password;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    GetPassword(proxy, cl, switches::kPasswordSwitch,
                StringPrintf("Enter the password for <%s>", account_id.c_str()),
                &password);
    GetPassword(
        proxy, cl, switches::kOldPasswordSwitch,
        StringPrintf("Enter the old password for <%s>", account_id.c_str()),
        &old_password);

    gboolean done = false;
    brillo::glib::ScopedError error;

    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      if (!org_chromium_CryptohomeInterface_migrate_key(proxy.gproxy(),
               account_id.c_str(),
               old_password.c_str(),
               password.c_str(),
               &done,
               &brillo::Resetter(&error).lvalue())) {
        printf("MigrateKey call failed: %s.\n", error->message);
      }
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_migrate_key(proxy.gproxy(),
               account_id.c_str(),
               old_password.c_str(),
               password.c_str(),
               &async_id,
               &brillo::Resetter(&error).lvalue())) {
        printf("MigrateKey call failed: %s.\n", error->message);
      } else {
        client_loop.Run(async_id);
        done = client_loop.get_return_status();
      }
    }
    if (!done) {
      printf("Key migration failed.\n");
    } else {
      printf("Key migration succeeded.\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_ADD_KEY],
                     action.c_str())) {
    std::string account_id, password, new_password;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    GetPassword(
        proxy, cl, switches::kPasswordSwitch,
        StringPrintf("Enter a current password for <%s>", account_id.c_str()),
        &password);
    GetPassword(
        proxy, cl, switches::kNewPasswordSwitch,
        StringPrintf("Enter the new password for <%s>", account_id.c_str()),
        &new_password);

    gboolean done = false;
    gint key_index = -1;
    brillo::glib::ScopedError error;

    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      if (!org_chromium_CryptohomeInterface_add_key(proxy.gproxy(),
               account_id.c_str(),
               password.c_str(),
               new_password.c_str(),
               &key_index,
               &done,
               &brillo::Resetter(&error).lvalue())) {
        printf("AddKey call failed: %s.\n", error->message);
      }
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_add_key(proxy.gproxy(),
               account_id.c_str(),
               password.c_str(),
               new_password.c_str(),
               &async_id,
               &brillo::Resetter(&error).lvalue())) {
        printf("AddKey call failed: %s.\n", error->message);
      } else {
        client_loop.Run(async_id);
        done = client_loop.get_return_status();
        key_index = client_loop.get_return_code();
      }
    }
    if (!done) {
      printf("Key addition failed.\n");
    } else {
      printf("Key %d was added.\n", key_index);
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_ADD_KEY_EX],
                action.c_str())) {
    std::string new_password;
    GetPassword(proxy, cl, switches::kNewPasswordSwitch,
                "Enter the new password",
                &new_password);
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return -1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, &auth))
      return -1;

    cryptohome::AddKeyRequest key_req;
    key_req.set_clobber_if_exists(cl->HasSwitch(switches::kForceSwitch));

    cryptohome::Key* key = key_req.mutable_key();
    key->set_secret(new_password);
    cryptohome::KeyData* data = key->mutable_data();
    data->set_label(cl->GetSwitchValueASCII(switches::kNewKeyLabelSwitch));

    if (cl->HasSwitch(switches::kHmacSigningKeySwitch)) {
      cryptohome::KeyAuthorizationData* auth_data =
          data->add_authorization_data();
      auth_data->set_type(
          cryptohome::KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
      cryptohome::KeyAuthorizationSecret* auth_secret =
          auth_data->add_secrets();
      auth_secret->mutable_usage()->set_sign(true);
      auth_secret->set_symmetric_key(cl->GetSwitchValueASCII(
          switches::kHmacSigningKeySwitch));

      LOG(INFO) << "Adding restricted key";
      cryptohome::KeyPrivileges* privs = data->mutable_privileges();
      privs->set_mount(true);
      privs->set_authorized_update(true);
      privs->set_update(false);
      privs->set_add(false);
      privs->set_remove(false);
    }

    // TODO(wad) Add a privileges cl interface

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(key_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return -1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call = org_chromium_CryptohomeInterface_add_key_ex_async(
                                 proxy.gproxy(),
                                 account_ary.get(),
                                 auth_ary.get(),
                                 req_ary.get(),
                                 &ClientLoop::ParseReplyThunk,
                                 static_cast<gpointer>(&loop));
      if (!call)
        return -1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_add_key_ex(proxy.gproxy(),
            account_ary.get(),
            auth_ary.get(),
            req_ary.get(),
            &out_reply,
            &brillo::Resetter(&error).lvalue())) {
        printf("AddKeyEx call failed: %s", error->message);
        return -1;
      }
      ParseBaseReply(out_reply, &reply);
    }
    if (reply.has_error()) {
      printf("Key addition failed.\n");
      return reply.error();
    }
    printf("Key added.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_UPDATE_KEY_EX],
                action.c_str())) {
    std::string new_password;
    GetPassword(proxy, cl, switches::kNewPasswordSwitch,
                "Enter the new password",
                &new_password);
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return -1;
    cryptohome::AuthorizationRequest auth;
    if (!BuildAuthorization(cl, proxy, &auth))
      return -1;

    cryptohome::UpdateKeyRequest key_req;
    cryptohome::Key* key = key_req.mutable_changes();
    key->set_secret(new_password);
    cryptohome::KeyData* data = key->mutable_data();
    if (cl->HasSwitch(switches::kNewKeyLabelSwitch))
      data->set_label(cl->GetSwitchValueASCII(switches::kNewKeyLabelSwitch));

    if (cl->HasSwitch(switches::kKeyRevisionSwitch)) {
      int int_value = 0;
      if (!base::StringToInt(
            cl->GetSwitchValueASCII(switches::kKeyRevisionSwitch), &int_value))
        LOG(FATAL) << "Cannot parse --key_revision";
      data->set_revision(int_value);
    }

    if (cl->HasSwitch(switches::kHmacSigningKeySwitch)) {
      ac::chrome::managedaccounts::account::Secret new_secret;
      new_secret.set_revision(data->revision());
      new_secret.set_secret(key->secret());
      std::string changes_str;
      if (!new_secret.SerializeToString(&changes_str)) {
        LOG(FATAL) << "Failed to serialize Secret";
      }
      brillo::SecureBlob hmac_key(
          cl->GetSwitchValueASCII(switches::kHmacSigningKeySwitch));
      brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
      SecureBlob hmac = cryptohome::CryptoLib::HmacSha256(hmac_key, hmac_data);
      key_req.set_authorization_signature(hmac.to_string());
    }

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    brillo::glib::ScopedArray auth_ary(GArrayFromProtoBuf(auth));
    brillo::glib::ScopedArray req_ary(GArrayFromProtoBuf(key_req));
    if (!account_ary.get() || !auth_ary.get() || !req_ary.get())
      return -1;

    cryptohome::BaseReply reply;
    brillo::glib::ScopedError error;
    if (cl->HasSwitch(switches::kAsyncSwitch)) {
      ClientLoop loop;
      loop.Initialize(&proxy);
      DBusGProxyCall* call =
          org_chromium_CryptohomeInterface_update_key_ex_async(
              proxy.gproxy(),
              account_ary.get(),
              auth_ary.get(),
              req_ary.get(),
              &ClientLoop::ParseReplyThunk,
              static_cast<gpointer>(&loop));
      if (!call)
        return -1;
      loop.Run();
      reply = loop.reply();
    } else {
      GArray* out_reply = NULL;
      if (!org_chromium_CryptohomeInterface_update_key_ex(proxy.gproxy(),
            account_ary.get(),
            auth_ary.get(),
            req_ary.get(),
            &out_reply,
            &brillo::Resetter(&error).lvalue())) {
        printf("Failed to call UpdateKeyEx!\n");
      }
      ParseBaseReply(out_reply, &reply);
    }
    if (reply.has_error()) {
      printf("Key update failed.\n");
      return reply.error();
    }
    printf("Key updated.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_REMOVE],
                     action.c_str())) {
    std::string account_id;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    if (!cl->HasSwitch(switches::kForceSwitch) && !ConfirmRemove(account_id)) {
      return 1;
    }

    gboolean done = false;
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_remove(proxy.gproxy(),
        account_id.c_str(),
        &done,
        &brillo::Resetter(&error).lvalue())) {
      printf("Remove call failed: %s.\n", error->message);
    }
    if (!done) {
      printf("Remove failed.\n");
    } else {
      printf("Remove succeeded.\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_UNMOUNT],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean done = false;
    if (!org_chromium_CryptohomeInterface_unmount(proxy.gproxy(),
        &done,
        &brillo::Resetter(&error).lvalue())) {
      printf("Unmount call failed: %s.\n", error->message);
    }
    if (!done) {
      printf("Unmount failed.\n");
    } else {
      printf("Unmount succeeded.\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_MOUNTED],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean done = false;
    if (!org_chromium_CryptohomeInterface_is_mounted(proxy.gproxy(),
        &done,
        &brillo::Resetter(&error).lvalue())) {
      printf("IsMounted call failed: %s.\n", error->message);
    }
    if (done) {
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

    cryptohome::UsernamePasskey up(account_id.c_str(), SecureBlob());
    printf("%s\n", up.GetObfuscatedUsername(GetSystemSalt(proxy)).c_str());
  } else if (!strcmp(switches::kActions[switches::ACTION_DUMP_KEYSET],
                     action.c_str())) {
    std::string account_id;

    if (!GetAccountId(cl, &account_id)) {
      return 1;
    }

    cryptohome::UsernamePasskey up(account_id.c_str(), SecureBlob());

    FilePath vault_path = FilePath("/home/.shadow")
        .Append(up.GetObfuscatedUsername(GetSystemSalt(proxy)))
        .Append("master.0");
    SecureBlob contents;
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
    if ((serialized.flags() & cryptohome::SerializedVaultKeyset::TPM_WRAPPED)
        && serialized.has_tpm_key()) {
      printf("    TPM_WRAPPED\n");
    }
    if (serialized.flags()
        & cryptohome::SerializedVaultKeyset::SCRYPT_WRAPPED) {
      printf("    SCRYPT_WRAPPED\n");
    }
    SecureBlob blob(serialized.salt().length());
    serialized.salt().copy(blob.char_data(), serialized.salt().length(), 0);
    printf("  Salt:\n");
    printf("    %s\n", cryptohome::CryptoLib::BlobToHex(blob).c_str());
    blob.resize(serialized.wrapped_keyset().length());
    serialized.wrapped_keyset().copy(blob.char_data(),
                                     serialized.wrapped_keyset().length(), 0);
    printf("  Wrapped (Encrypted) Keyset:\n");
    printf("    %s\n", cryptohome::CryptoLib::BlobToHex(blob).c_str());
    if (serialized.has_tpm_key()) {
      blob.resize(serialized.tpm_key().length());
      serialized.tpm_key().copy(blob.char_data(),
                                serialized.tpm_key().length(), 0);
      printf("  TPM-Bound (Encrypted) Vault Encryption Key:\n");
      printf("    %s\n", cryptohome::CryptoLib::BlobToHex(blob).c_str());
    }
    if (serialized.has_tpm_public_key_hash()) {
      blob.resize(serialized.tpm_public_key_hash().length());
      serialized.tpm_public_key_hash().copy(blob.char_data(),
                                            serialized.tpm_key().length(), 0);
      printf("  TPM Public Key Hash:\n");
      printf("    %s\n", cryptohome::CryptoLib::BlobToHex(blob).c_str());
    }
    if (serialized.has_password_rounds()) {
      printf("  Password rounds:\n");
      printf("    %d\n", serialized.password_rounds());
    }
    if (serialized.has_last_activity_timestamp()) {
      const base::Time last_activity =
          base::Time::FromInternalValue(serialized.last_activity_timestamp());
      printf("  Last activity (days ago):\n");
      printf("    %d\n", (base::Time::Now() - last_activity).InDays());
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_DUMP_LAST_ACTIVITY],
                     action.c_str())) {
    std::vector<FilePath> user_dirs;
    if (!platform.EnumerateDirectoryEntries(
          FilePath("/home/.shadow/"), false, &user_dirs)) {
      LOG(ERROR) << "Can not list shadow root.";
      return 1;
    }
    for (std::vector<FilePath>::iterator it = user_dirs.begin();
         it != user_dirs.end(); ++it) {
      const std::string dir_name = it->BaseName().value();
      if (!brillo::cryptohome::home::IsSanitizedUserName(dir_name))
        continue;
      // TODO(wad): change it so that it uses GetVaultKeysets().
      std::unique_ptr<cryptohome::FileEnumerator> file_enumerator(
          platform.GetFileEnumerator(*it, false, base::FileEnumerator::FILES));
      base::Time max_activity = base::Time::UnixEpoch();
      FilePath next_path;
      while (!(next_path = file_enumerator->Next()).empty()) {
        FilePath file_name = next_path.BaseName().RemoveFinalExtension();
        // Scan for "master." files.
        if (file_name.value() != cryptohome::kKeyFile)
          continue;
        SecureBlob contents;
        if (!platform.ReadFile(next_path, &contents)) {
          LOG(ERROR) << "Couldn't load keyset: " << next_path.value();
          continue;
        }
        cryptohome::SerializedVaultKeyset keyset;
        if (!keyset.ParseFromArray(contents.data(), contents.size())) {
          LOG(ERROR) << "Couldn't parse keyset: " << next_path.value();
          continue;
        }
        if (keyset.has_last_activity_timestamp()) {
          const base::Time last_activity =
              base::Time::FromInternalValue(keyset.last_activity_timestamp());
          if (last_activity > max_activity)
            max_activity = last_activity;
        }
      }
      if (max_activity > base::Time::UnixEpoch()) {
        printf("%s %3d\n", dir_name.c_str(),
                           (base::Time::Now() - max_activity).InDays());
      }
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_STATUS],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result = false;
    if (!org_chromium_CryptohomeInterface_tpm_is_enabled(proxy.gproxy(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsEnabled call failed: %s.\n", error->message);
    } else {
      printf("TPM Enabled: %s\n", (result ? "true" : "false"));
    }
    result = false;
    if (!org_chromium_CryptohomeInterface_tpm_is_owned(proxy.gproxy(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsOwned call failed: %s.\n", error->message);
    } else {
      printf("TPM Owned: %s\n", (result ? "true" : "false"));
    }
    if (!org_chromium_CryptohomeInterface_tpm_is_being_owned(proxy.gproxy(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsBeingOwned call failed: %s.\n", error->message);
    } else {
      printf("TPM Being Owned: %s\n", (result ? "true" : "false"));
    }
    if (!org_chromium_CryptohomeInterface_tpm_is_ready(proxy.gproxy(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsReady call failed: %s.\n", error->message);
    } else {
      printf("TPM Ready: %s\n", (result ? "true" : "false"));
    }
    gchar* password;
    if (!org_chromium_CryptohomeInterface_tpm_get_password(proxy.gproxy(),
        &password,
        &brillo::Resetter(&error).lvalue())) {
      printf("TpmGetPassword call failed: %s.\n", error->message);
    } else {
      printf("TPM Password: %s\n", password);
      g_free(password);
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_MORE_STATUS],
                     action.c_str())) {
    cryptohome::GetTpmStatusRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall(cryptohome::kCryptohomeGetTpmStatus,
                           DBUS_METHOD(get_tpm_status),
                           DBUS_METHOD(get_tpm_status_async),
                           cl, &proxy, request, &reply)) {
      return -1;
    }
    if (!reply.HasExtension(cryptohome::GetTpmStatusReply::reply)) {
      printf("GetTpmStatusReply missing.\n");
      return -1;
    }
    printf("GetTpmStatus success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_STATUS],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    gchar* status;
    if (!org_chromium_CryptohomeInterface_get_status_string(proxy.gproxy(),
        &status,
        &brillo::Resetter(&error).lvalue())) {
      printf("GetStatusString call failed: %s.\n", error->message);
    } else {
      printf("%s\n", status);
      g_free(status);
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_SET_CURRENT_USER_OLD],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    ClientLoop client_loop;
    client_loop.Initialize(&proxy);
    if (!org_chromium_CryptohomeInterface_update_current_user_activity_timestamp(  // NOLINT
            proxy.gproxy(),
            base::TimeDelta::FromDays(
                kSetCurrentUserOldOffsetInDays).InSeconds(),
            &brillo::Resetter(&error).lvalue())) {
      printf("UpdateCurrentUserActivityTimestamp call failed: %s.\n",
             error->message);
    } else {
        printf("Timestamp successfully updated. You may verify it with "
               "--action=dump_keyset --user=...\n");
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_DO_FREE_DISK_SPACE_CONTROL],
      action.c_str())) {
    brillo::glib::ScopedError error;
    ClientLoop client_loop;
    client_loop.Initialize(&proxy);
    gint async_id = -1;
    if (!org_chromium_CryptohomeInterface_async_do_automatic_free_disk_space_control(  // NOLINT
            proxy.gproxy(),
            &async_id,
            &brillo::Resetter(&error).lvalue())) {
      printf("AsyncDoAutomaticFreeDiskSpaceControl call failed: %s.\n",
             error->message);
    } else {
      client_loop.Run(async_id);
      if (client_loop.get_return_status()) {
        printf("Free disk space control completed successfully "
               "and maybe cut away something. Use `df` to check.\n");
      } else {
        printf("Cleanup reported that there was enough free space "
               "(more than %" PRIu64" Kbytes, check with `df`) "
               "so it didn't try to cut anything.\n",
               cryptohome::kMinFreeSpaceInBytes >> 10);
      }
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_TAKE_OWNERSHIP],
                     action.c_str())) {
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_tpm_can_attempt_ownership(
        proxy.gproxy(),
        &brillo::Resetter(&error).lvalue())) {
      printf("TpmCanAttemptOwnership call failed: %s.\n", error->message);
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_CLEAR_STORED_PASSWORD],
      action.c_str())) {
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_tpm_clear_stored_password(
        proxy.gproxy(),
        &brillo::Resetter(&error).lvalue())) {
      printf("TpmClearStoredPassword call failed: %s.\n", error->message);
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_INSTALL_ATTRIBUTES_GET],
      action.c_str())) {
    std::string name;
    if (!GetAttrName(cl, &name)) {
      printf("No attribute name specified.\n");
      return 1;
    }

    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_ready(
        proxy.gproxy(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("IsReady call failed: %s.\n", error->message);
    }
    if (result == FALSE) {
      printf("InstallAttributes() is not ready.\n");
      return 1;
    }

    brillo::glib::ScopedArray value;
    if (!org_chromium_CryptohomeInterface_install_attributes_get(
        proxy.gproxy(),
        name.c_str(),
        &brillo::Resetter(&value).lvalue(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
       printf("Get() failed: %s.\n", error->message);
    }
    std::string value_str(value->data, value->len);
    if (result == TRUE) {
      printf("%s\n", value_str.c_str());
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

    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_ready(
        proxy.gproxy(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("IsReady call failed: %s.\n", error->message);
    }

    if (result == FALSE) {
      printf("InstallAttributes() is not ready.\n");
      return 1;
    }

    brillo::glib::ScopedArray value_ary(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(value_ary.get(), value.c_str(), value.size() + 1);
    if (!org_chromium_CryptohomeInterface_install_attributes_set(
        proxy.gproxy(),
        name.c_str(),
        value_ary.get(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
       printf("Set() failed: %s.\n", error->message);
    }
    if (result == FALSE)
      return 1;
  } else if (!strcmp(
      switches::kActions[switches::ACTION_INSTALL_ATTRIBUTES_FINALIZE],
      action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result;
    if (!org_chromium_CryptohomeInterface_install_attributes_is_ready(
        proxy.gproxy(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("IsReady call failed: %s.\n", error->message);
    }
    if (result == FALSE) {
      printf("InstallAttributes is not ready.\n");
      return 1;
    }
    if (!org_chromium_CryptohomeInterface_install_attributes_finalize(
        proxy.gproxy(),
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("Finalize() failed: %s.\n", error->message);
    }
    printf("InstallAttributesFinalize(): %d\n", result);
  } else if (!strcmp(
      switches::kActions[switches::ACTION_STORE_ENROLLMENT],
      action.c_str())) {
    gboolean success;
    std::string random_data = "TEST DATA TO STORE";
    brillo::glib::ScopedArray data(g_array_new(FALSE, FALSE, 1));
    brillo::glib::ScopedError error;
    g_array_append_vals(data.get(),
                        random_data.data(),
                        random_data.length());

    if (!org_chromium_CryptohomeInterface_store_enrollment_state(
        proxy.gproxy(),
        data.get(),
        &success,
        &brillo::Resetter(&error).lvalue())) {
      printf("Store enrollment failed: %s.\n", error->message);
      return 1;
    }
    if (!success) {
      printf("Store enrollment failed but dbus send succeeded.\n");
      return 1;
    } else {
      printf("Stored %s.\n", random_data.c_str());
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_LOAD_ENROLLMENT],
      action.c_str())) {
    gboolean success;
    brillo::glib::ScopedArray data(g_array_new(FALSE, FALSE, 1));
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_load_enrollment_state(
        proxy.gproxy(),
        &brillo::Resetter(&data).lvalue(),
        &success,
        &brillo::Resetter(&error).lvalue())) {
      printf("Load enrollment failed: %s.\n", error->message);
      return 1;
    }
    if (!success) {
      printf("Load enrollment failed but dbus send succeeded.\n");
      return 1;
    } else {
      std::string loaded_data(static_cast<char*>(data->data), data->len);
      printf("Decrypted from disk: %s.\n", loaded_data.c_str());
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_WAIT_OWNERSHIP],
      action.c_str())) {
    return !WaitForTPMOwnership(&proxy);
  } else if (!strcmp(
      switches::kActions[switches::ACTION_PKCS11_TOKEN_STATUS],
      action.c_str())) {
    // If no account_id is specified, proceed with the empty string.
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    if (!account_id.empty()) {
      brillo::glib::ScopedError error;
      gchar* label = NULL;
      gchar* pin = NULL;
      int slot = 0;
      if (!org_chromium_CryptohomeInterface_pkcs11_get_tpm_token_info_for_user(
              proxy.gproxy(),
              account_id.c_str(),
              &label,
              &pin,
              &slot,
              &brillo::Resetter(&error).lvalue())) {
        printf("PKCS #11 info call failed: %s.\n", error->message);
      } else {
        printf("Token properties for %s:\n", account_id.c_str());
        printf("Label = %s\n", label);
        printf("Pin = %s\n", pin);
        printf("Slot = %d\n", slot);
        g_free(label);
        g_free(pin);
      }
    } else {
      cryptohome::Pkcs11Init init;
      if (!init.IsUserTokenOK()) {
        printf("User token looks broken!\n");
        return 1;
      }
      printf("User token looks OK!\n");
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_PKCS11_TERMINATE],
                     action.c_str())) {
    // If no account_id is specified, proceed with the empty string.
    std::string account_id;
    GetAccountId(cl, &account_id);
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_pkcs11_terminate(
            proxy.gproxy(),
            account_id.c_str(),
            &brillo::Resetter(&error).lvalue())) {
      printf("PKCS #11 terminate call failed: %s.\n", error->message);
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_LIVE_TEST],
                     action.c_str())) {
    cryptohome::TpmLiveTest tpm_test;
    SecureBlob owner_password(cl->GetSwitchValueASCII(
        switches::kTpmOwnerPasswordSwitch));
    return tpm_test.RunLiveTests(owner_password);
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_VERIFY_ATTESTATION],
      action.c_str())) {
    bool is_cros_core = cl->HasSwitch(switches::kCrosCoreSwitch);
    brillo::glib::ScopedError error;
    gboolean result = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_verify_attestation_data(
        proxy.gproxy(),
        is_cros_core,
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("TpmVerifyAttestationData call failed: %s.\n", error->message);
    }
    if (result == FALSE) {
      printf("TPM attestation data is not valid or is not available.\n");
      return 1;
    }
  } else if (!strcmp(switches::kActions[switches::ACTION_TPM_VERIFY_EK],
                     action.c_str())) {
    bool is_cros_core = cl->HasSwitch(switches::kCrosCoreSwitch);
    brillo::glib::ScopedError error;
    gboolean result = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_verify_ek(
        proxy.gproxy(),
        is_cros_core,
        &result,
        &brillo::Resetter(&error).lvalue())) {
      printf("TpmVerifyEK call failed: %s.\n", error->message);
    }
    if (result == FALSE) {
      printf("TPM endorsement key is not valid or is not available.\n");
      return 1;
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_STATUS],
      action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean result = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_is_attestation_prepared(
        proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsAttestationPrepared call failed: %s.\n", error->message);
    } else {
      printf("Attestation Prepared: %s\n", (result ? "true" : "false"));
    }
    if (!org_chromium_CryptohomeInterface_tpm_is_attestation_enrolled(
        proxy.gproxy(), &result, &brillo::Resetter(&error).lvalue())) {
      printf("TpmIsAttestationEnrolled call failed: %s.\n", error->message);
    } else {
      printf("Attestation Enrolled: %s\n", (result ? "true" : "false"));
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_START_ENROLL],
      action.c_str())) {
    brillo::glib::ScopedError error;
    std::string response_data;
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      brillo::glib::ScopedArray data;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_create_enroll_request(  // NOLINT
          proxy.gproxy(),
          pca_type,
          &brillo::Resetter(&data).lvalue(),
          &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationCreateEnrollRequest call failed: %s.\n",
               error->message);
        return 1;
      }
      response_data = std::string(static_cast<char*>(data->data), data->len);
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_create_enroll_request(  // NOLINT
              proxy.gproxy(),
              pca_type,
              &async_id,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationCreateEnrollRequest call failed: %s.\n",
               error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        if (!client_loop.get_return_status()) {
          printf("Attestation enrollment request failed.\n");
          return 1;
        }
        response_data = client_loop.get_return_data();
      }
    }
    base::WriteFile(GetFile(cl), response_data.data(), response_data.length());
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_FINISH_ENROLL],
      action.c_str())) {
    std::string contents;
    if (!base::ReadFileToString(GetFile(cl), &contents)) {
      printf("Failed to read input file.\n");
      return 1;
    }
    brillo::glib::ScopedArray data(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(data.get(), contents.data(), contents.length());
    gboolean success = FALSE;
    brillo::glib::ScopedError error;
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      if (!org_chromium_CryptohomeInterface_tpm_attestation_enroll(
              proxy.gproxy(), pca_type, data.get(),
              &success, &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationEnroll call failed: %s.\n", error->message);
        return 1;
      }
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_enroll(
              proxy.gproxy(), pca_type, data.get(),
              &async_id, &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationEnroll call failed: %s.\n", error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        success = client_loop.get_return_status();
      }
    }
    if (!success) {
      printf("Attestation enrollment failed.\n");
      return 1;
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_START_CERTREQ],
      action.c_str())) {
    brillo::glib::ScopedError error;
    std::string response_data;
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      brillo::glib::ScopedArray data;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_create_cert_request(
          proxy.gproxy(),
          pca_type,
          cryptohome::ENTERPRISE_USER_CERTIFICATE,
          "", "",
          &brillo::Resetter(&data).lvalue(),
          &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationCreateCertRequest call failed: %s.\n",
               error->message);
        return 1;
      }
      response_data = std::string(static_cast<char*>(data->data), data->len);
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_create_cert_request(  // NOLINT
              proxy.gproxy(),
              pca_type,
              cryptohome::ENTERPRISE_USER_CERTIFICATE,
              "", "",
              &async_id,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationCreateCertRequest call failed: %s.\n",
               error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        if (!client_loop.get_return_status()) {
          printf("Attestation certificate request failed.\n");
          return 1;
        }
        response_data = client_loop.get_return_data();
      }
    }
    base::WriteFile(GetFile(cl), response_data.data(), response_data.length());
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_FINISH_CERTREQ],
      action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    std::string contents;
    if (!base::ReadFileToString(GetFile(cl), &contents)) {
      printf("Failed to read input file.\n");
      return 1;
    }
    gboolean is_user_specific = (key_name != "attest-ent-machine");
    brillo::glib::ScopedArray data(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(data.get(), contents.data(), contents.length());
    gboolean success = FALSE;
    brillo::glib::ScopedError error;
    std::string cert_data;
    if (!cl->HasSwitch(switches::kAsyncSwitch)) {
      brillo::glib::ScopedArray cert;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_finish_cert_request(
          proxy.gproxy(),
          data.get(),
          is_user_specific,
          account_id.c_str(),
          key_name.c_str(),
          &brillo::Resetter(&cert).lvalue(),
          &success,
          &brillo::Resetter(&error).lvalue())) {
        printf("TpmAttestationFinishCertRequest call failed: %s.\n",
               error->message);
        return 1;
      }
      cert_data = std::string(static_cast<char*>(cert->data), cert->len);
    } else {
      ClientLoop client_loop;
      client_loop.Initialize(&proxy);
      gint async_id = -1;
      if (!org_chromium_CryptohomeInterface_async_tpm_attestation_finish_cert_request(  // NOLINT
              proxy.gproxy(),
              data.get(),
              is_user_specific,
              account_id.c_str(),
              key_name.c_str(),
              &async_id,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationFinishCertRequest call failed: %s.\n",
               error->message);
        return 1;
      } else {
        client_loop.Run(async_id);
        success = client_loop.get_return_status();
        cert_data = client_loop.get_return_data();
      }
    }
    if (!success) {
      printf("Attestation certificate request failed.\n");
      return 1;
    }
    base::WriteFile(GetFile(cl), cert_data.data(), cert_data.length());
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_KEY_STATUS],
      action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    gboolean is_user_specific = (key_name != "attest-ent-machine");
    brillo::glib::ScopedError error;
    gboolean exists = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_does_key_exist(
          proxy.gproxy(),
          is_user_specific,
          account_id.c_str(),
          key_name.c_str(),
          &exists,
          &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationDoesKeyExist call failed: %s.\n", error->message);
      return 1;
    }
    if (!exists) {
      printf("Key does not exist.\n");
      return 0;
    }
    gboolean success = FALSE;
    brillo::glib::ScopedArray cert;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_get_certificate(
          proxy.gproxy(),
          is_user_specific,
          account_id.c_str(),
          key_name.c_str(),
          &brillo::Resetter(&cert).lvalue(),
          &success,
          &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationGetCertificate call failed: %s.\n", error->message);
      return 1;
    }
    brillo::glib::ScopedArray public_key;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_get_public_key(
          proxy.gproxy(),
          is_user_specific,
          account_id.c_str(),
          key_name.c_str(),
          &brillo::Resetter(&public_key).lvalue(),
          &success,
          &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationGetPublicKey call failed: %s.\n", error->message);
      return 1;
    }
    std::string cert_pem =
      std::string(static_cast<char*>(cert->data), cert->len);
    std::string public_key_hex =
      base::HexEncode(public_key->data, public_key->len);
    printf("Public Key:\n%s\n\nCertificate:\n%s\n",
           public_key_hex.c_str(),
           cert_pem.c_str());
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_REGISTER_KEY],
      action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    ClientLoop client_loop;
    client_loop.Initialize(&proxy);
    gint async_id = -1;
    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_register_key(
          proxy.gproxy(),
          true,
          account_id.c_str(),
          key_name.c_str(),
          &async_id,
          &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationRegisterKey call failed: %s.\n", error->message);
      return 1;
    } else {
      client_loop.Run(async_id);
      gboolean result = client_loop.get_return_status();
      printf("Result: %s\n", result ? "Success" : "Failure");
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_ENTERPRISE_CHALLENGE],
      action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    gboolean is_user_specific = (key_name != "attest-ent-machine");
    std::string contents;
    if (!base::ReadFileToString(GetFile(cl), &contents)) {
      printf("Failed to read input file: %s\n", GetFile(cl).value().c_str());
      return 1;
    }
    brillo::glib::ScopedArray challenge(g_array_new(FALSE, FALSE, 1));
    g_array_append_vals(challenge.get(), contents.data(), contents.length());
    brillo::glib::ScopedArray device_id(g_array_new(FALSE, FALSE, 1));
    std::string device_id_str = "fake_device_id";
    g_array_append_vals(device_id.get(),
                        device_id_str.data(),
                        device_id_str.length());
    brillo::glib::ScopedError error;
    ClientLoop client_loop;
    client_loop.Initialize(&proxy);
    gint async_id = -1;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_sign_enterprise_challenge(  // NOLINT
            proxy.gproxy(),
            is_user_specific,
            account_id.c_str(),
            key_name.c_str(),
            "cros@crosdmsregtest.com",
            device_id.get(),
            TRUE,
            challenge.get(),
            &async_id,
            &brillo::Resetter(&error).lvalue())) {
      printf("AsyncTpmAttestationSignEnterpriseChallenge call failed: %s.\n",
             error->message);
      return 1;
    }
    client_loop.Run(async_id);
    if (!client_loop.get_return_status()) {
      printf("Attestation challenge response failed.\n");
      return 1;
    }
    std::string response_data = client_loop.get_return_data();
    base::WriteFileDescriptor(STDOUT_FILENO,
                              response_data.data(),
                              response_data.length());
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_DELETE],
      action.c_str())) {
    std::string account_id = cl->GetSwitchValueASCII(switches::kUserSwitch);
    std::string key_name = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);
    if (key_name.length() == 0) {
      printf("No key name specified (--%s=<name>)\n",
             switches::kAttrNameSwitch);
      return 1;
    }
    gboolean is_user_specific = (key_name != "attest-ent-machine");
    brillo::glib::ScopedError error;
    gboolean success = FALSE;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_delete_keys(
            proxy.gproxy(),
            is_user_specific,
            account_id.c_str(),
            key_name.c_str(),
            &success,
            &brillo::Resetter(&error).lvalue())) {
      printf("AsyncTpmAttestationDeleteKeys call failed: %s.\n",
             error->message);
      return 1;
    }
    if (!success) {
      printf("Delete operation failed.\n");
      return 1;
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_GET_EK],
      action.c_str())) {
    if (cl->HasSwitch(switches::kProtobufSwitch)) {
      // Get the EK info as a protobuf.
      cryptohome::GetEndorsementInfoRequest request;
      cryptohome::BaseReply reply;
      if (!MakeProtoDBusCall("GetEndorsementInfo",
                             DBUS_METHOD(get_endorsement_info),
                             DBUS_METHOD(get_endorsement_info_async),
                             cl, &proxy, request, &reply)) {
        return -1;
      }
      if (!reply.HasExtension(cryptohome::GetEndorsementInfoReply::reply)) {
        printf("GetEndorsementInfoReply missing.\n");
        return -1;
      }
      printf("GetEndorsmentInfo (protobuf) success.\n");
    } else {
      brillo::glib::ScopedError error;
      gboolean success = FALSE;
      gchar* ek_info = NULL;
      if (!org_chromium_CryptohomeInterface_tpm_attestation_get_ek(
              proxy.gproxy(),
              &ek_info,
              &success,
              &brillo::Resetter(&error).lvalue())) {
        printf("AsyncTpmAttestationGetEK call failed: %s.\n", error->message);
        return 1;
      }
      if (!success) {
        printf("Failed to get EK.\n");
        g_free(ek_info);
        return 1;
      }
      printf("%s\n", ek_info);
      g_free(ek_info);
    }
  } else if (!strcmp(
      switches::kActions[switches::ACTION_TPM_ATTESTATION_RESET_IDENTITY],
      action.c_str())) {
    brillo::glib::ScopedError error;
    gboolean success = FALSE;
    std::string token = cl->GetSwitchValueASCII(switches::kPasswordSwitch);
    brillo::glib::ScopedArray reset_request;
    if (!org_chromium_CryptohomeInterface_tpm_attestation_reset_identity(
            proxy.gproxy(),
            token.c_str(),
            &brillo::Resetter(&reset_request).lvalue(),
            &success,
            &brillo::Resetter(&error).lvalue())) {
      printf("TpmAttestationResetIdentity call failed: %s.\n", error->message);
      return 1;
    }
    if (!success) {
      printf("Failed to get identity reset request.\n");
      return 1;
    }
    base::WriteFile(GetFile(cl), reset_request->data, reset_request->len);
  } else if (!strcmp(
      switches::kActions[
          switches::ACTION_TPM_ATTESTATION_RESET_IDENTITY_RESULT],
      action.c_str())) {
    std::string contents;
    if (!base::ReadFileToString(GetFile(cl), &contents)) {
      printf("Failed to read input file: %s\n", GetFile(cl).value().c_str());
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
    std::string data;
    if (!base::ReadFileToString(GetFile(cl), &data)) {
      printf("Failed to read input file: %s\n", GetFile(cl).value().c_str());
      return 1;
    }

    cryptohome::SignBootLockboxRequest request;
    request.set_data(data);
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("SignBootLockbox",
                           DBUS_METHOD(sign_boot_lockbox),
                           DBUS_METHOD(sign_boot_lockbox_async),
                           cl, &proxy, request, &reply)) {
      return -1;
    }

    if (!reply.HasExtension(cryptohome::SignBootLockboxReply::reply)) {
      printf("SignBootLockboxReply missing.\n");
      return -1;
    }
    std::string signature =
        reply.GetExtension(cryptohome::SignBootLockboxReply::reply).signature();
    base::WriteFile(GetFile(cl).AddExtension("signature"),
                    signature.data(),
                    signature.size());
    printf("SignBootLockbox success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_VERIFY_LOCKBOX],
                     action.c_str())) {
    std::string data;
    if (!base::ReadFileToString(GetFile(cl), &data)) {
      printf("Failed to read input file: %s\n", GetFile(cl).value().c_str());
      return 1;
    }
    std::string signature;
    FilePath signature_file = GetFile(cl).AddExtension("signature");
    if (!base::ReadFileToString(signature_file, &signature)) {
      printf("Failed to read input file: %s\n", signature_file.value().c_str());
      return 1;
    }

    cryptohome::VerifyBootLockboxRequest request;
    request.set_data(data);
    request.set_signature(signature);
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("VerifyBootLockbox",
                           DBUS_METHOD(verify_boot_lockbox),
                           DBUS_METHOD(verify_boot_lockbox_async),
                           cl, &proxy, request, &reply)) {
      return -1;
    }
    printf("VerifyBootLockbox success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_FINALIZE_LOCKBOX],
                     action.c_str())) {
    cryptohome::FinalizeBootLockboxRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("FinalizeBootLockbox",
                           DBUS_METHOD(finalize_boot_lockbox),
                           DBUS_METHOD(finalize_boot_lockbox_async),
                           cl, &proxy, request, &reply)) {
      return -1;
    }
    printf("FinalizeBootLockbox success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_GET_BOOT_ATTRIBUTE],
                     action.c_str())) {
    std::string name;
    if (!GetAttrName(cl, &name)) {
      printf("No attribute name specified.\n");
      return 1;
    }

    cryptohome::GetBootAttributeRequest request;
    request.set_name(name);
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("GetBootAttribute",
                           DBUS_METHOD(get_boot_attribute),
                           DBUS_METHOD(get_boot_attribute_async),
                           cl, &proxy, request, &reply)) {
      return -1;
    }
    if (!reply.HasExtension(cryptohome::GetBootAttributeReply::reply)) {
      printf("GetBootAttributeReply missing.\n");
      return -1;
    }
    std::string value =
        reply.GetExtension(cryptohome::GetBootAttributeReply::reply).value();
    printf("%s\n", value.c_str());
  } else if (!strcmp(switches::kActions[switches::ACTION_SET_BOOT_ATTRIBUTE],
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

    cryptohome::SetBootAttributeRequest request;
    request.set_name(name);
    request.set_value(value);
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("SetBootAttribute",
                           DBUS_METHOD(set_boot_attribute),
                           DBUS_METHOD(set_boot_attribute_async),
                           cl, &proxy, request, &reply)) {
      return -1;
    }
    printf("SetBootAttribute success.\n");
  } else if (!strcmp(switches::kActions[
      switches::ACTION_FLUSH_AND_SIGN_BOOT_ATTRIBUTES], action.c_str())) {
    cryptohome::FlushAndSignBootAttributesRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("FlushAndSignBootAttributes",
                           DBUS_METHOD(flush_and_sign_boot_attributes),
                           DBUS_METHOD(flush_and_sign_boot_attributes_async),
                           cl, &proxy, request, &reply)) {
      return -1;
    }
    printf("FlushAndSignBootAttributes success.\n");
  } else if (!strcmp(
      switches::kActions[switches::ACTION_GET_LOGIN_STATUS],
      action.c_str())) {
    cryptohome::GetLoginStatusRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("GetLoginStatus",
                           DBUS_METHOD(get_login_status),
                           DBUS_METHOD(get_login_status_async),
                           cl, &proxy, request, &reply)) {
      return -1;
    }
    if (!reply.HasExtension(cryptohome::GetLoginStatusReply::reply)) {
      printf("GetLoginStatusReply missing.\n");
      return -1;
    }
    printf("GetLoginStatus success.\n");
  } else if (!strcmp(
      switches::kActions[switches::ACTION_INITIALIZE_CAST_KEY],
      action.c_str())) {
    cryptohome::InitializeCastKeyRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall("InitializeCastKey",
                           DBUS_METHOD(initialize_cast_key),
                           DBUS_METHOD(initialize_cast_key_async),
                           cl, &proxy, request, &reply)) {
      return -1;
    }
    printf("InitializeCastKey success.\n");
  } else if (!strcmp(
      switches::kActions[switches::ACTION_GET_FIRMWARE_MANAGEMENT_PARAMETERS],
      action.c_str())) {
    cryptohome::GetFirmwareManagementParametersRequest request;
    cryptohome::BaseReply reply;
    if (!MakeProtoDBusCall(
        cryptohome::kCryptohomeGetFirmwareManagementParameters,
        DBUS_METHOD(get_firmware_management_parameters),
        DBUS_METHOD(get_firmware_management_parameters_async),
        cl, &proxy, request, &reply)) {
      return -1;
    }
    if (!reply.HasExtension(
        cryptohome::GetFirmwareManagementParametersReply::reply)) {
      printf("GetFirmwareManagementParametersReply missing.\n");
      return -1;
    }
    cryptohome::GetFirmwareManagementParametersReply get_fwmp_reply =
        reply.GetExtension(
            cryptohome::GetFirmwareManagementParametersReply::reply);
    printf("flags=0x%08x\n", get_fwmp_reply.flags());
    brillo::SecureBlob hash(get_fwmp_reply.developer_key_hash());
    printf("hash=%s\n", cryptohome::CryptoLib::BlobToHex(hash).c_str());
    printf("GetFirmwareManagementParameters success.\n");
  } else if (!strcmp(
      switches::kActions[switches::ACTION_SET_FIRMWARE_MANAGEMENT_PARAMETERS],
      action.c_str())) {
    cryptohome::SetFirmwareManagementParametersRequest request;
    cryptohome::BaseReply reply;

    if (cl->HasSwitch(switches::kFlagsSwitch)) {
      std::string flags_str = cl->GetSwitchValueASCII(switches::kFlagsSwitch);
      char *end = NULL;
      int32_t flags = strtol(flags_str.c_str(), &end, 0);
      if (end && *end != '\0') {
        printf("Bad flags value.\n");
        return 1;
      }
      request.set_flags(flags);
    } else {
      printf("Use --flags (and optionally --developer_key_hash).\n");
      return 1;
    }

    if (cl->HasSwitch(switches::kDevKeyHashSwitch)) {
      std::string hash_str =
        cl->GetSwitchValueASCII(switches::kDevKeyHashSwitch);
      brillo::SecureBlob hash;
      if (!base::HexStringToBytes(hash_str, &hash)) {
        printf("Bad hash value.\n");
        return 1;
      }
      if (hash.size() != SHA256_DIGEST_LENGTH) {
        printf("Bad hash size.\n");
        return 1;
      }

      request.set_developer_key_hash(hash.to_string());
    }

    if (!MakeProtoDBusCall(
        cryptohome::kCryptohomeSetFirmwareManagementParameters,
        DBUS_METHOD(set_firmware_management_parameters),
        DBUS_METHOD(set_firmware_management_parameters_async),
        cl, &proxy, request, &reply)) {
      return -1;
    }
    printf("SetFirmwareManagementParameters success.\n");
  } else if (!strcmp(
      switches::kActions[
          switches::ACTION_REMOVE_FIRMWARE_MANAGEMENT_PARAMETERS],
      action.c_str())) {
    cryptohome::RemoveFirmwareManagementParametersRequest request;
    cryptohome::BaseReply reply;

    if (!MakeProtoDBusCall(
        cryptohome::kCryptohomeRemoveFirmwareManagementParameters,
        DBUS_METHOD(remove_firmware_management_parameters),
        DBUS_METHOD(remove_firmware_management_parameters_async),
        cl, &proxy, request, &reply)) {
      return -1;
    }
    printf("RemoveFirmwareManagementParameters success.\n");
  } else if (!strcmp(switches::kActions[switches::ACTION_MIGRATE_TO_DIRCRYPTO],
                     action.c_str())) {
    cryptohome::AccountIdentifier id;
    if (!BuildAccountId(cl, &id))
      return -1;

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    if (!account_ary.get())
      return -1;

    cryptohome::MigrateToDircryptoRequest request;
    request.set_minimal_migration(cl->HasSwitch(switches::kMinimalMigration));

    brillo::glib::ScopedArray request_ary(GArrayFromProtoBuf(request));
    if (!request_ary.get())
      return -1;

    brillo::glib::ScopedError error;
    if (!org_chromium_CryptohomeInterface_migrate_to_dircrypto_ex(
            proxy.gproxy(),
            account_ary.get(),
            request_ary.get(),
            &brillo::Resetter(&error).lvalue())) {
      printf("MigrateToDircrypto call failed: %s\n", error->message);
      return -1;
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

    brillo::glib::ScopedArray account_ary(GArrayFromProtoBuf(id));
    if (!account_ary.get())
      return -1;

    brillo::glib::ScopedError error;
    gboolean needs_migration = false;
    if (!org_chromium_CryptohomeInterface_needs_dircrypto_migration(
            proxy.gproxy(),
            account_ary.get(),
            &needs_migration,
            &brillo::Resetter(&error).lvalue())) {
      printf("NeedsDirCryptoMigration call failed: %s.\n", error->message);
      return 1;
    }
    if (needs_migration)
      printf("Yes\n");
    else
      printf("No\n");
  } else {
    printf("Unknown action or no action given.  Available actions:\n");
    for (int i = 0; switches::kActions[i]; i++)
      printf("  --action=%s\n", switches::kActions[i]);
  }
  return 0;
}  // NOLINT(readability/fn_size)

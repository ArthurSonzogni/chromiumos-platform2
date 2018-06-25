//
// Copyright (C) 2014 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// trunks_client is a command line tool that supports various TPM operations. It
// does not provide direct access to the trunksd D-Bus interface.

#include <inttypes.h>
#include <stdio.h>

#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/file_utils.h>
#include <brillo/syslog_logging.h>

#include "trunks/error_codes.h"
#include "trunks/hmac_session.h"
#include "trunks/password_authorization_delegate.h"
#include "trunks/policy_session.h"
#include "trunks/scoped_key_handle.h"
#include "trunks/session_manager.h"
#include "trunks/tpm_state.h"
#include "trunks/tpm_utility.h"
#include "trunks/trunks_client_test.h"
#include "trunks/trunks_factory_impl.h"

namespace {

using trunks::CommandTransceiver;
using trunks::TrunksFactory;
using trunks::TrunksFactoryImpl;

void PrintUsage() {
  puts("Options:");
  puts("  --allocate_pcr - Configures PCR 0-15 under the SHA256 bank.");
  puts("  --clear - Clears the TPM. Use before initializing the TPM.");
  puts("  --help - Prints this message.");
  puts("  --init_tpm - Initializes a TPM as CrOS firmware does.");
  puts("  --own - Takes ownership of the TPM with the provided password.");
  puts("  --owner_password - used to provide an owner password");
  puts("  --endorsement_password - used to provide an endorsement password");
  puts("  --regression_test - Runs some basic regression tests. If");
  puts("                      *_password is supplied, it runs tests that");
  puts("                      require the permissions.");
  puts("  --startup - Performs startup and self-tests.");
  puts("  --status - Prints TPM status information.");
  puts("  --stress_test - Runs some basic stress tests.");
  puts("  --read_pcr --index=<N> - Reads a PCR and prints the value.");
  puts("  --extend_pcr --index=<N> --value=<value> - Extends a PCR.");
  puts("  --tpm_version - Prints TPM versions and IDs similar to tpm_version.");
  puts("  --endorsement_public_key - Prints the public endorsement key.");
  puts("  --key_create --rsa=<bits> --usage=sign|decrypt|all");
  puts("               --key_blob=<file> [--print_time] [--sess_*]");
  puts("                    - Creates a key and saves the blob to file.");
  puts("  --key_load --key_blob=<file> [--print_time] [--sess_*]");
  puts("                    - Loads key from blob, returns handle.");
  puts("  --key_sign --handle=<H> --data=<in_file> --signature=<out_file>");
  puts("             [--print_time] [--sess_*]");
  puts("                    - Signs the hash of data using the loaded key.");
  puts("  --key_info --handle=<H> - Prints information about the loaded key.");
  puts("  --sess_* - group of options providing parameters for auth session:");
  puts("      --sess_salted");
  puts("      --sess_encrypted");
}

std::string HexEncode(const std::string& bytes) {
  return base::HexEncode(bytes.data(), bytes.size());
}

std::string HexEncode(const trunks::TPM2B_DIGEST* tpm2b) {
  return base::HexEncode(tpm2b->buffer, tpm2b->size);
}

int OutputToFile(const std::string& file_name, const std::string& data) {
  if (!brillo::WriteStringToFile(base::FilePath(file_name), data)) {
    LOG(ERROR) << "Failed to write to " << file_name;
    return -1;
  }
  return 0;
}

int InputFromFile(const std::string& file_name, std::string* data) {
  if (!base::ReadFileToString(base::FilePath(file_name), data)) {
    LOG(ERROR) << "Failed to write to " << file_name;
    return -1;
  }
  return 0;
}

template <typename OP, typename... ARGS>
trunks::TPM_RC CallTimed(bool print_time,
                         const char* op_name,
                         OP op_func,
                         ARGS&&... args) {
  base::ElapsedTimer timer;
  trunks::TPM_RC rc = op_func(args...);
  if (print_time) {
    printf("%s took %" PRId64 " ms\n", op_name,
           timer.Elapsed().InMilliseconds());
  }
  return rc;
}

template <typename OP, typename... ARGS>
trunks::TPM_RC CallTpmUtility(bool print_time,
                              const TrunksFactory& factory,
                              const char* op_name,
                              OP op_func,
                              ARGS&&... args) {
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();
  trunks::TPM_RC rc = CallTimed(print_time, op_name, std::mem_fn(op_func),
                                tpm_utility.get(), args...);
  LOG_IF(ERROR, rc) << "Error during " << op_name << ": "
                    << trunks::GetErrorString(rc);
  return rc;
}

int Startup(const TrunksFactory& factory) {
  factory.GetTpmUtility()->Shutdown();
  return factory.GetTpmUtility()->Startup();
}

int Clear(const TrunksFactory& factory) {
  return factory.GetTpmUtility()->Clear();
}

int InitializeTpm(const TrunksFactory& factory) {
  return factory.GetTpmUtility()->InitializeTpm();
}

int AllocatePCR(const TrunksFactory& factory) {
  trunks::TPM_RC result;
  result = factory.GetTpmUtility()->AllocatePCR("");
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error allocating PCR:" << trunks::GetErrorString(result);
    return result;
  }
  factory.GetTpmUtility()->Shutdown();
  return factory.GetTpmUtility()->Startup();
}

int TakeOwnership(const std::string& owner_password,
                  const TrunksFactory& factory) {
  trunks::TPM_RC rc;
  rc = factory.GetTpmUtility()->TakeOwnership(owner_password, owner_password,
                                              owner_password);
  if (rc) {
    LOG(ERROR) << "Error taking ownership: " << trunks::GetErrorString(rc);
    return rc;
  }
  return 0;
}

int DumpStatus(const TrunksFactory& factory) {
  std::unique_ptr<trunks::TpmState> state = factory.GetTpmState();
  trunks::TPM_RC result = state->Initialize();
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to read TPM state: "
               << trunks::GetErrorString(result);
    return result;
  }
  printf("Owner password set: %s\n",
         state->IsOwnerPasswordSet() ? "true" : "false");
  printf("Endorsement password set: %s\n",
         state->IsEndorsementPasswordSet() ? "true" : "false");
  printf("Lockout password set: %s\n",
         state->IsLockoutPasswordSet() ? "true" : "false");
  printf("Ownership status: %s\n", state->IsOwned() ? "true" : "false");
  printf("In lockout: %s\n", state->IsInLockout() ? "true" : "false");
  printf("Platform hierarchy enabled: %s\n",
         state->IsPlatformHierarchyEnabled() ? "true" : "false");
  printf("Storage hierarchy enabled: %s\n",
         state->IsStorageHierarchyEnabled() ? "true" : "false");
  printf("Endorsement hierarchy enabled: %s\n",
         state->IsEndorsementHierarchyEnabled() ? "true" : "false");
  printf("Is Tpm enabled: %s\n", state->IsEnabled() ? "true" : "false");
  printf("Was shutdown orderly: %s\n",
         state->WasShutdownOrderly() ? "true" : "false");
  printf("Is RSA supported: %s\n", state->IsRSASupported() ? "true" : "false");
  printf("Is ECC supported: %s\n", state->IsECCSupported() ? "true" : "false");
  printf("Lockout Counter: %u\n", state->GetLockoutCounter());
  printf("Lockout Threshold: %u\n", state->GetLockoutThreshold());
  printf("Lockout Interval: %u\n", state->GetLockoutInterval());
  printf("Lockout Recovery: %u\n", state->GetLockoutRecovery());
  return 0;
}

int ReadPCR(const TrunksFactory& factory, int index) {
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();
  std::string value;
  trunks::TPM_RC result = tpm_utility->ReadPCR(index, &value);
  if (result) {
    LOG(ERROR) << "ReadPCR: " << trunks::GetErrorString(result);
    return result;
  }
  printf("PCR Value: %s\n", HexEncode(value).c_str());
  return 0;
}

int ExtendPCR(const TrunksFactory& factory,
              int index,
              const std::string& value) {
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();
  trunks::TPM_RC result = tpm_utility->ExtendPCR(index, value, nullptr);
  if (result) {
    LOG(ERROR) << "ExtendPCR: " << trunks::GetErrorString(result);
    return result;
  }
  return 0;
}

char* TpmPropertyToStr(uint32_t value) {
  static char str[5];
  char c;
  int i = 0;
  int shift = 24;
  for (; i < 4; i++, shift -= 8) {
    c = static_cast<char>((value >> shift) & 0xFF);
    if (c == 0)
      break;
    str[i] = (c >= 32 && c < 127) ? c : ' ';
  }
  str[i] = 0;
  return str;
}

int TpmVersion(const TrunksFactory& factory) {
  std::unique_ptr<trunks::TpmState> state = factory.GetTpmState();
  trunks::TPM_RC result = state->Initialize();
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to read TPM state: "
               << trunks::GetErrorString(result);
    return result;
  }
  printf("  TPM 2.0 Version Info:\n");
  // Print Chip Version for compatibility with tpm_version, hardcoded as
  // there's no 2.0 equivalent (TPM_PT_FAMILY_INDICATOR is const).
  printf("  Chip Version:        2.0.0.0\n");
  uint32_t family = state->GetTpmFamily();
  printf("  Spec Family:         %08" PRIx32 "\n", family);
  printf("  Spec Family String:  %s\n", TpmPropertyToStr(family));
  printf("  Spec Level:          %" PRIu32 "\n",
         state->GetSpecificationLevel());
  printf("  Spec Revision:       %" PRIu32 "\n",
         state->GetSpecificationRevision());
  uint32_t manufacturer = state->GetManufacturer();
  printf("  Manufacturer Info:   %08" PRIx32 "\n", manufacturer);
  printf("  Manufacturer String: %s\n", TpmPropertyToStr(manufacturer));
  printf("  Vendor ID:           %s\n", state->GetVendorIDString().c_str());
  printf("  TPM Model:           %08" PRIx32 "\n", state->GetTpmModel());
  printf("  Firmware Version:    %016" PRIx64 "\n",
         state->GetFirmwareVersion());

  return 0;
}

int EndorsementPublicKey(const TrunksFactory& factory) {
  std::string ekm;
  factory.GetTpmUtility()->GetPublicRSAEndorsementKeyModulus(&ekm);
  std::string ekm_hex = HexEncode(ekm);
  printf("  Public Endorsement Key Modulus: %s\n", ekm_hex.c_str());
  return 0;
}

int KeyStartSession(trunks::SessionManager* session_manager,
                    base::CommandLine* cl,
                    trunks::HmacAuthorizationDelegate* delegate) {
  bool salted = cl->HasSwitch("sess_salted");
  bool encrypted = cl->HasSwitch("sess_encrypted");
  bool print_time = cl->HasSwitch("print_time");

  trunks::TPM_RC rc =
      CallTimed(print_time, "StartSession",
                std::mem_fn(&trunks::SessionManager::StartSession),
                session_manager, trunks::TPM_SE_HMAC, trunks::TPM_RH_NULL, "",
                salted, encrypted, delegate);
  LOG_IF(ERROR, rc) << "Failed to start session: "
                    << trunks::GetErrorString(rc);
  return rc;
}

int GetKeyUsage(const std::string& option_value,
                trunks::TpmUtility::AsymmetricKeyUsage* key_usage) {
  const std::map<std::string,
                 trunks::TpmUtility::AsymmetricKeyUsage> mapping = {
    {"decrypt", trunks::TpmUtility::kDecryptKey},
    {"sign", trunks::TpmUtility::kSignKey},
    {"all", trunks::TpmUtility::kDecryptAndSignKey},
  };
  auto entry = mapping.find(option_value);
  if (entry == mapping.end()) {
    LOG(ERROR) << "Unrecognized key usage: " << option_value;
    return -1;
  }
  *key_usage = entry->second;
  return 0;
}

int KeyInfo(bool print_time, const TrunksFactory& factory, uint32_t handle) {
  trunks::TPMT_PUBLIC public_area;
  if (CallTpmUtility(print_time, factory, "GetKeyPublicArea",
                     &trunks::TpmUtility::GetKeyPublicArea, handle,
                     &public_area)) {
    return -1;
  }
  puts("Key public area:");
  printf("  type: %#x\n", public_area.type);
  printf("  name_alg: %#x\n", public_area.name_alg);
  printf("  attributes: %#x\n", public_area.object_attributes);
  printf("  auth_policy: %s\n", HexEncode(&public_area.auth_policy).c_str());

  std::string key_name;
  if (CallTpmUtility(print_time, factory, "GetKeyName",
                     &trunks::TpmUtility::GetKeyName, handle, &key_name)) {
    return -1;
  }
  printf("Key name: %s\n", HexEncode(key_name).c_str());

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->HasSwitch("help")) {
    puts("Trunks Client: A command line tool to access the TPM.");
    PrintUsage();
    return 0;
  }

  TrunksFactoryImpl factory;
  CHECK(factory.Initialize()) << "Failed to initialize trunks factory.";

  bool print_time = cl->HasSwitch("print_time");
  if (cl->HasSwitch("status")) {
    return DumpStatus(factory);
  }
  if (cl->HasSwitch("startup")) {
    return Startup(factory);
  }
  if (cl->HasSwitch("clear")) {
    return Clear(factory);
  }
  if (cl->HasSwitch("init_tpm")) {
    return InitializeTpm(factory);
  }
  if (cl->HasSwitch("allocate_pcr")) {
    return AllocatePCR(factory);
  }

  if (cl->HasSwitch("own")) {
    return TakeOwnership(cl->GetSwitchValueASCII("owner_password"), factory);
  }
  if (cl->HasSwitch("regression_test")) {
    trunks::TrunksClientTest test(factory);
    LOG(INFO) << "Running RNG test.";
    if (!test.RNGTest()) {
      LOG(ERROR) << "Error running RNGtest.";
      return -1;
    }
    LOG(INFO) << "Running RSA key tests.";
    if (!test.SignTest()) {
      LOG(ERROR) << "Error running SignTest.";
      return -1;
    }
    if (!test.DecryptTest()) {
      LOG(ERROR) << "Error running DecryptTest.";
      return -1;
    }
    if (!test.ImportTest()) {
      LOG(ERROR) << "Error running ImportTest.";
      return -1;
    }
    if (!test.AuthChangeTest()) {
      LOG(ERROR) << "Error running AuthChangeTest.";
      return -1;
    }
    if (!test.VerifyKeyCreationTest()) {
      LOG(ERROR) << "Error running VerifyKeyCreationTest.";
      return -1;
    }
    LOG(INFO) << "Running Sealed Data test.";
    if (!test.SealedDataTest()) {
      LOG(ERROR) << "Error running SealedDataTest.";
      return -1;
    }
    LOG(INFO) << "Running Sealed to Multiple PCR Data test.";
    if (!test.SealedToMultiplePCRDataTest()) {
      LOG(ERROR) << "Error running SealedToMultiplePCRDataTest.";
      return -1;
    }
    LOG(INFO) << "Running PCR test.";
    if (!test.PCRTest()) {
      LOG(ERROR) << "Error running PCRTest.";
      return -1;
    }
    LOG(INFO) << "Running policy tests.";
    if (!test.PolicyAuthValueTest()) {
      LOG(ERROR) << "Error running PolicyAuthValueTest.";
      return -1;
    }
    if (!test.PolicyAndTest()) {
      LOG(ERROR) << "Error running PolicyAndTest.";
      return -1;
    }
    if (!test.PolicyOrTest()) {
      LOG(ERROR) << "Error running PolicyOrTest.";
      return -1;
    }
    LOG(INFO) << "Running identity key test.";
    if (!test.IdentityKeyTest()) {
      LOG(ERROR) << "Error running IdentityKeyTest.";
      return -1;
    }
    if (cl->HasSwitch("owner_password")) {
      std::string owner_password = cl->GetSwitchValueASCII("owner_password");
      LOG(INFO) << "Running NVRAM test.";
      if (!test.NvramTest(owner_password)) {
        LOG(ERROR) << "Error running NvramTest.";
        return -1;
      }
      if (cl->HasSwitch("endorsement_password")) {
        std::string endorsement_password =
            cl->GetSwitchValueASCII("endorsement_password");
        LOG(INFO) << "Running endorsement test.";
        if (!test.EndorsementTest(endorsement_password, owner_password)) {
          LOG(ERROR) << "Error running EndorsementTest.";
          return -1;
        }
      }
    }
    LOG(INFO) << "All tests were run successfully.";
    return 0;
  }
  if (cl->HasSwitch("stress_test")) {
    LOG(INFO) << "Running stress tests.";
    trunks::TrunksClientTest test(factory);
    if (!test.ManyKeysTest()) {
      LOG(ERROR) << "Error running ManyKeysTest.";
      return -1;
    }
    if (!test.ManySessionsTest()) {
      LOG(ERROR) << "Error running ManySessionsTest.";
      return -1;
    }
    return 0;
  }
  if (cl->HasSwitch("read_pcr") && cl->HasSwitch("index")) {
    return ReadPCR(factory, atoi(cl->GetSwitchValueASCII("index").c_str()));
  }
  if (cl->HasSwitch("extend_pcr") && cl->HasSwitch("index") &&
      cl->HasSwitch("value")) {
    return ExtendPCR(factory, atoi(cl->GetSwitchValueASCII("index").c_str()),
                     cl->GetSwitchValueASCII("value"));
  }
  if (cl->HasSwitch("tpm_version")) {
    return TpmVersion(factory);
  }
  if (cl->HasSwitch("endorsement_public_key")) {
    return EndorsementPublicKey(factory);
  }

  if (cl->HasSwitch("key_create") && cl->HasSwitch("rsa") &&
      cl->HasSwitch("usage") && cl->HasSwitch("key_blob")) {
    int modulus_bits = std::stoi(cl->GetSwitchValueASCII("rsa"), nullptr, 0);
    trunks::TpmUtility::AsymmetricKeyUsage key_usage;
    if (GetKeyUsage(cl->GetSwitchValueASCII("usage"), &key_usage)) {
      return -1;
    }
    trunks::HmacAuthorizationDelegate delegate;
    std::unique_ptr<trunks::SessionManager> session_manager =
        factory.GetSessionManager();
    if (KeyStartSession(session_manager.get(), cl, &delegate)) {
      return -1;
    }
    std::string key_blob;
    if (CallTpmUtility(print_time, factory, "CreateRSAKeyPair",
                       &trunks::TpmUtility::CreateRSAKeyPair, key_usage,
                       modulus_bits, 0x10001 /* exponent */, "" /* password */,
                       "" /* policy_digest */,
                       false /* use_only_policy_digest */,
                       std::vector<uint32_t>() /* pcrs */, &delegate, &key_blob,
                       nullptr /* creation_blob */)) {
      return -1;
    }
    return OutputToFile(cl->GetSwitchValueASCII("key_blob"), key_blob);
  }
  if (cl->HasSwitch("key_load") && cl->HasSwitch("key_blob")) {
    std::string key_blob;
    if (InputFromFile(cl->GetSwitchValueASCII("key_blob"), &key_blob)) {
      return -1;
    }
    trunks::HmacAuthorizationDelegate delegate;
    std::unique_ptr<trunks::SessionManager> session_manager =
        factory.GetSessionManager();
    if (KeyStartSession(session_manager.get(), cl, &delegate)) {
      return -1;
    }
    trunks::TPM_HANDLE handle;
    if (CallTpmUtility(print_time, factory, "Load",
                       &trunks::TpmUtility::LoadKey, key_blob, &delegate,
                       &handle)) {
      return -1;
    }
    printf("Loaded key handle: %#x\n", handle);
    return 0;
  }
  if (cl->HasSwitch("key_sign") && cl->HasSwitch("handle") &&
      cl->HasSwitch("data") && cl->HasSwitch("signature")) {
    uint32_t handle = std::stoul(cl->GetSwitchValueASCII("handle"), nullptr, 0);
    std::string data;
    if (InputFromFile(cl->GetSwitchValueASCII("data"), &data)) {
      return -1;
    }
    trunks::HmacAuthorizationDelegate delegate;
    std::unique_ptr<trunks::SessionManager> session_manager =
        factory.GetSessionManager();
    if (KeyStartSession(session_manager.get(), cl, &delegate)) {
      return -1;
    }
    std::string signature;
    if (CallTpmUtility(print_time, factory, "Sign", &trunks::TpmUtility::Sign,
                       handle, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA256,
                       data, true /* generate_hash */, &delegate, &signature)) {
      return -1;
    }
    return OutputToFile(cl->GetSwitchValueASCII("signature"), signature);
  }
  if (cl->HasSwitch("key_info") && cl->HasSwitch("handle")) {
    uint32_t handle = std::stoul(cl->GetSwitchValueASCII("handle"), nullptr, 0);
    return KeyInfo(print_time, factory, handle);
  }

  puts("Invalid options!");
  PrintUsage();
  return -1;
}

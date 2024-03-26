// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/pinweaver_manager/pinweaver_manager.h"
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <brillo/secure_blob.h>
#include <sysexits.h>

#include <base/check.h>
#include <base/command_line.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/syslog_logging.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include "base/no_destructor.h"
#include "libhwsec/client/command_helpers.h"
#include "libhwsec/factory/factory_impl.h"
#include "libhwsec/frontend/pinweaver_manager/frontend.h"
#include "libhwsec-foundation/status/status_chain_macros.h"

using hwsec::ClientArgs;

namespace {

using DelaySchedule = hwsec::PinWeaverManagerFrontend::DelaySchedule;

constexpr int kLEMaxIncorrectAttempt = 5;
const DelaySchedule kDefaultDelaySchedule = {
    {kLEMaxIncorrectAttempt, UINT32_MAX},
};

constexpr char kUsage[] =
    "Usage: pinweaver_manager_client <command> [<args>]\nCommands:\n";

template <typename... Args>
struct Help {
  static constexpr char kName[] = "help";
  static constexpr char kArgs[] = "";
  static constexpr char kDesc[] = R"(
      Print this help message.
)";

  static int Run(const ClientArgs& args) {
    PrintUsage();
    return EX_USAGE;
  }

  static void PrintUsage() {
    printf("%s", kUsage);
    (hwsec::PrintCommandUsage<Args>(), ...);
  }
};

struct Initialize {
  static constexpr char kName[] = "init";
  static constexpr char kArgs[] = "";
  static constexpr char kDesc[] = R"(
      Initialize PinWeaverManager (specifically the memory-mapped pinweaver leaf
      cache file). Returning success indicates that PinWeaverManager is
      initialized, not locked out, and hash tree is valid.
)";

  static int Run(const ClientArgs& args) {
    if (args.size() != 0) {
      hwsec::PrintCommandUsage<Initialize>();
      return EX_USAGE;
    }

    RETURN_IF_ERROR(
        hwsec::FactoryImpl().GetPinWeaverManagerFrontend()->Initialize())
        .LogError()
        .As(EXIT_FAILURE);
    printf("PinWeaver Manager is in good state.\n");
    return EXIT_SUCCESS;
  }
};

struct SyncHashTree {
  static constexpr char kName[] = "sync";
  static constexpr char kArgs[] = "";
  static constexpr char kDesc[] = R"(
      Sync the PinWeaver hash tree between OS and GSC.
      Returning success indicates that the hash tree is synced.
)";

  static int Run(const ClientArgs& args) {
    if (args.size() != 0) {
      hwsec::PrintCommandUsage<Initialize>();
      return EX_USAGE;
    }

    RETURN_IF_ERROR(
        hwsec::FactoryImpl().GetPinWeaverManagerFrontend()->SyncHashTree())
        .LogError()
        .As(EXIT_FAILURE);
    printf("PinWeaver Manager is synced.\n");
    return EXIT_SUCCESS;
  }
};

struct InsertCredential {
  static constexpr char kName[] = "insert";
  static constexpr char kArgs[] = "<le_secret> <he_secret> <reset_secret>";
  static constexpr char kDesc[] = R"(
      Inserts an credential with given LE/HE/ResetSecret into the system.
      The argument strings are transformed into SecureBlob of size 32.
      Prints the label of inserted credential on success.
)";

  static int Run(const ClientArgs& args) {
    if (args.size() != 3) {
      hwsec::PrintCommandUsage<InsertCredential>();
      return EX_USAGE;
    }

    brillo::SecureBlob le_secret(args[0]);
    le_secret.resize(32);
    brillo::SecureBlob he_secret(args[1]);
    he_secret.resize(32);
    brillo::SecureBlob reset_secret(args[2]);
    reset_secret.resize(32);

    ASSIGN_OR_RETURN(
        uint64_t label,
        hwsec::FactoryImpl().GetPinWeaverManagerFrontend()->InsertCredential(
            std::vector<hwsec::OperationPolicySetting>(), le_secret, he_secret,
            reset_secret, kDefaultDelaySchedule,
            /*expiration_delay=*/std::nullopt),
        _.LogError().As(EXIT_FAILURE));
    printf(
        "Succeed to insert credential,\n"
        "label=%" PRIu64 "\n",
        label);
    return EXIT_SUCCESS;
  }
};
struct CheckCredential {
  static constexpr char kName[] = "auth";
  static constexpr char kArgs[] = "<label> <le_secret>";
  static constexpr char kDesc[] = R"(
      Checks whether the LE credential <le_secret> for a <label> is correct.
      Prints corresponding <he_secret> and <reset_secret> on success.
)";

  using CheckCredentialReply =
      hwsec::PinWeaverManagerFrontend::CheckCredentialReply;

  static int Run(const ClientArgs& args) {
    if (args.size() != 2) {
      hwsec::PrintCommandUsage<CheckCredential>();
      return EX_USAGE;
    }

    uint64_t label;
    if (!base::StringToUint64(args[0], &label)) {
      LOG(ERROR) << "Failed to convert label.";
      return EX_USAGE;
    }
    brillo::SecureBlob le_secret(args[1]);
    le_secret.resize(32);

    ASSIGN_OR_RETURN(
        CheckCredentialReply reply,
        hwsec::FactoryImpl().GetPinWeaverManagerFrontend()->CheckCredential(
            label, le_secret),
        _.LogError().As(EXIT_FAILURE));
    printf(
        "Auth succeed,\n"
        "he_secret=%s\n"
        "reset_secret=%s\n",
        reply.he_secret.char_data(), reply.reset_secret.char_data());
    return EXIT_SUCCESS;
  }
};

struct ResetCredential {
  static constexpr char kName[] = "reset";
  static constexpr char kArgs[] = "<label> <reset_secret>";
  static constexpr char kDesc[] = R"(
      Attempts to reset tme wrong attempt and the expiration time of a LE
      Credential.
)";

  using ResetType = hwsec::PinWeaverManagerFrontend::ResetType;

  static int Run(const ClientArgs& args) {
    if (args.size() != 2) {
      hwsec::PrintCommandUsage<ResetCredential>();
      return EX_USAGE;
    }

    uint64_t label;
    if (!base::StringToUint64(args[0], &label)) {
      LOG(ERROR) << "Failed to convert label.";
      return EX_USAGE;
    }
    brillo::SecureBlob reset_secret(args[1]);
    reset_secret.resize(32);

    RETURN_IF_ERROR(
        hwsec::FactoryImpl().GetPinWeaverManagerFrontend()->ResetCredential(
            label, reset_secret, ResetType::kWrongAttempts))
        .LogError()
        .As(EXIT_FAILURE);
    printf("Reset succeed.\n");
    return EXIT_SUCCESS;
  }
};

struct RemoveCredential {
  static constexpr char kName[] = "remove";
  static constexpr char kArgs[] = "<label>";
  static constexpr char kDesc[] = R"(
      Remove the credential with label=<label>.
)";

  static int Run(const ClientArgs& args) {
    if (args.size() != 1) {
      hwsec::PrintCommandUsage<RemoveCredential>();
      return EX_USAGE;
    }

    uint64_t label;
    if (!base::StringToUint64(args[0], &label)) {
      LOG(ERROR) << "Failed to convert label.";
      return EX_USAGE;
    }

    RETURN_IF_ERROR(
        hwsec::FactoryImpl().GetPinWeaverManagerFrontend()->RemoveCredential(
            label))
        .LogError()
        .As(EXIT_FAILURE);
    printf("Remove label %" PRIu64 " succeed.\n", label);
    return EXIT_SUCCESS;
  }
};

#define COMMAND_LIST                                           \
  Initialize, SyncHashTree, InsertCredential, CheckCredential, \
      ResetCredential, RemoveCredential

using Usage = Help<Help<>, COMMAND_LIST>;

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);

  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  std::vector<std::string> cmd_args = cl->GetArgs();

  hwsec::ClientArgs args(cmd_args.data(), cmd_args.size());

  if (args.empty()) {
    return Usage::Run(args);
  }

  return hwsec::MatchCommands<Usage, COMMAND_LIST>::Run(args);
}

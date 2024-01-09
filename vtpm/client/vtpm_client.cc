// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// vtpm_client is a command line tool that supports various vTPM operations.
#include <memory>
#include <stdio.h>
#include <string>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/syslog_logging.h>

#include "trunks/error_codes.h"
#include "trunks/trunks_factory_impl.h"
#include "vtpm/client/vtpm_dbus_proxy.h"

namespace {

using trunks::AuthorizationDelegate;
using trunks::TrunksFactory;
using trunks::TrunksFactoryImpl;
using vtpm::VtpmDBusProxy;

void PrintUsage() {
  puts("vTPM command options:");
  puts("  --index_data --index=<N> - print the data of NV index N in hex");
  puts("                             format.");
}

std::string HexEncode(const std::string& bytes) {
  return base::HexEncode(bytes.data(), bytes.size());
}

int PrintIndexDataInHex(const TrunksFactory& factory, int index) {
  // Mask out the nv index handle so the user can either add or not add it
  // themselves.
  index &= trunks::HR_HANDLE_MASK;
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();
  trunks::TPMS_NV_PUBLIC nvram_public;
  trunks::TPM_RC rc = tpm_utility->GetNVSpacePublicArea(index, &nvram_public);
  if (rc != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading NV space public area: "
               << trunks::GetErrorString(rc);
    return -1;
  }
  std::unique_ptr<AuthorizationDelegate> empty_password_authorization =
      factory.GetPasswordAuthorization("");
  std::string nvram_data;
  rc =
      tpm_utility->ReadNVSpace(index, /*offset=*/0, nvram_public.data_size,
                               /*using_owner_authorization=*/false, &nvram_data,
                               empty_password_authorization.get());
  if (rc != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading NV space: " << trunks::GetErrorString(rc);
    return -1;
  }
  printf("NV Index data: %s\n", HexEncode(nvram_data).c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->HasSwitch("help")) {
    puts("vTPM Client: A command line tool to access the vTPM.");
    PrintUsage();
    return 0;
  }

  std::unique_ptr<VtpmDBusProxy> dbus_proxy = std::make_unique<VtpmDBusProxy>();

  CHECK(dbus_proxy->Init()) << "Failed to initialize D-Bus proxy.";

  TrunksFactoryImpl factory(dbus_proxy.get());
  CHECK(factory.Initialize()) << "Failed to initialize trunks factory.";

  if (cl->HasSwitch("index_data") && cl->HasSwitch("index")) {
    uint32_t nv_index =
        std::stoul(cl->GetSwitchValueASCII("index"), nullptr, 16);
    return PrintIndexDataInHex(factory, nv_index);
  }

  puts("Invalid options!");
  PrintUsage();
  return -1;
}

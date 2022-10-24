// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <absl/strings/str_format.h>
#include <base/command_line.h>
#include <base/strings/string_number_conversions.h>
#include <trunks/command_codes.h>
#include <trunks/error_codes.h>

namespace {

constexpr char kXml[] = "xml";
constexpr char kHelp[] = "help";

void PrintUsage() {
  printf("Usage: tpm_commmand_response_decoder [OPTION]... -- [VALUE]...\n");
  printf("Translate TPM command and response into human-readable string.\n");
  printf("VALUE could be an decimal or hexadecimal integer.\n");
  printf("\n");
  printf("TPM command and reponse encoding:\n");
  printf("- The upper 16 bits: command code\n");
  printf("- The lower 16 bits: response code\n");
  printf("\n");
  printf("Option:\n");
  printf("  --help  Show the help message.\n");
  printf("  --xml   Format the string as the xml entry.\n");
  printf("          e.g. <int value=\"23658635\"\n");
  printf("                label=\"TPM_CC_NV_ReadPublic: TPM_RC_HANDLE\"/>\n");
}

std::string ToXml(int value, const std::string& label) {
  return absl::StrFormat("<int value=\"%d\" label=\"%s\"/>", value, label);
}

std::string DecodeCommandResponse(uint32_t data) {
  trunks::TPM_CC cc = data >> 16;
  trunks::TPM_RC rc = data & 0xFFFF;

  std::string command = trunks::GetCommandString(cc);
  std::string response = trunks::GetErrorString(rc);
  return command + ": " + response;
}

bool ParseValue(const std::string& input, uint32_t& output) {
  if (uint32_t data; base::StringToUint(input, &data)) {
    output = data;
    return true;
  }
  if (int32_t data; base::StringToInt(input, &data)) {
    output = static_cast<uint32_t>(data);
    return true;
  }
  if (uint32_t data; base::HexStringToUInt(input, &data)) {
    output = data;
    return true;
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  if (cl->HasSwitch(kHelp)) {
    PrintUsage();
    return 0;
  }

  bool to_xml = false;
  if (cl->HasSwitch(kXml)) {
    to_xml = true;
  }

  const auto& args = cl->GetArgs();
  if (args.empty()) {
    PrintUsage();
    return -1;
  }

  for (const auto& arg : args) {
    uint32_t data = 0;
    if (!ParseValue(arg, data)) {
      printf("Failed to parse command and response: arg: %s", arg.c_str());
      return -1;
    }

    std::string result = DecodeCommandResponse(data);
    if (to_xml) {
      result = ToXml(data, result);
    }
    printf("%s\n", result.c_str());
  }
}

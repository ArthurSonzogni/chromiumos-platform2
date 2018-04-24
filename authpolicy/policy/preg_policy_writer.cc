// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/policy/preg_policy_writer.h"

#include <limits>

#include <base/files/file_util.h>
#include <base/strings/string16.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "authpolicy/policy/policy_encoder_helper.h"
#include "authpolicy/policy/preg_parser.h"

namespace {

const char kActionTriggerDelVals[] = "**delvals";

// Constants for PReg file delimiters.
const base::char16 kDelimBracketOpen = L'[';
const base::char16 kDelimBracketClose = L']';
const base::char16 kDelimSemicolon = L';';
const base::char16 kDelimNull = L'\0';

// Registry data type constants. Values have to match REG_* constants on
// Windows. Does not check data types or whether a policy can be recommended or
// not. This is checked when GPO is converted to a policy proto.
const uint32_t kRegSz = 1;
const uint32_t kRegDwordLittleEndian = 4;

}  // namespace

namespace policy {

PRegPolicyWriter::PRegPolicyWriter(const std::string& mandatory_key,
                                   const std::string& recommended_key)
    : PRegPolicyWriter() {
  SetMandatoryKey(mandatory_key);
  SetRecommendedKey(recommended_key);
}

PRegPolicyWriter::PRegPolicyWriter() {
  buffer_.append(
      preg_parser::kPRegFileHeader,
      preg_parser::kPRegFileHeader + arraysize(preg_parser::kPRegFileHeader));
}

PRegPolicyWriter::~PRegPolicyWriter() {
  DCHECK(!entry_started_);
}

void PRegPolicyWriter::SetMandatoryKey(const std::string& mandatory_key) {
  mandatory_key_ = mandatory_key;
}

void PRegPolicyWriter::SetRecommendedKey(const std::string& recommended_key) {
  recommended_key_ = recommended_key;
}

void PRegPolicyWriter::SetKeysForUserDevicePolicy() {
  SetMandatoryKey(policy::kKeyUserDevice);
  SetRecommendedKey(
      base::StringPrintf("%s\\%s", policy::kKeyUserDevice, kKeyRecommended));
}

void PRegPolicyWriter::SetKeysForExtensionPolicy(
    const std::string& extension_id) {
  std::string base_key = base::StringPrintf("%s\\%s\\", policy::kKeyExtensions,
                                            extension_id.c_str());
  SetMandatoryKey(base_key + kKeyMandatoryExtension);
  SetRecommendedKey(base_key + kKeyRecommended);
}

void PRegPolicyWriter::AppendBoolean(const char* policy_name,
                                     bool value,
                                     PolicyLevel level) {
  DCHECK(!recommended_key_.empty() && !mandatory_key_.empty());

  StartEntry(GetKey(level), policy_name, kRegDwordLittleEndian,
             sizeof(uint32_t));
  AppendUnsignedInt(value ? 1 : 0);
  EndEntry();
}

void PRegPolicyWriter::AppendInteger(const char* policy_name,
                                     uint32_t value,
                                     PolicyLevel level) {
  DCHECK(!recommended_key_.empty() && !mandatory_key_.empty());

  StartEntry(GetKey(level), policy_name, kRegDwordLittleEndian,
             sizeof(uint32_t));
  AppendUnsignedInt(value);
  EndEntry();
}

void PRegPolicyWriter::AppendString(const char* policy_name,
                                    const std::string& value,
                                    PolicyLevel level) {
  DCHECK(!recommended_key_.empty() && !mandatory_key_.empty());

  // size * 2 + 2 because of char16 and the 0-terminator.
  CHECK(value.size() <= (std::numeric_limits<uint32_t>::max() - 2) / 2);
  StartEntry(GetKey(level), policy_name, kRegSz,
             static_cast<uint32_t>(value.size()) * 2 + 2);
  AppendNullTerminatedString(value);
  EndEntry();
}

void PRegPolicyWriter::AppendStringList(const char* policy_name,
                                        const std::vector<std::string>& values,
                                        PolicyLevel level) {
  DCHECK(!recommended_key_.empty() && !mandatory_key_.empty());

  // Add entry to wipe previous values.
  std::string key = GetKey(level) + "\\" + policy_name;
  StartEntry(key, kActionTriggerDelVals, kRegSz, 2);
  AppendNullTerminatedString("");
  EndEntry();

  // Add an entry for each value.
  CHECK(values.size() <= std::numeric_limits<int>::max());
  for (int n = 0; n < static_cast<int>(values.size()); ++n) {
    CHECK(values[n].size() <= (std::numeric_limits<uint32_t>::max() - 2) / 2);
    StartEntry(key, base::IntToString(n + 1), kRegSz,
               static_cast<uint32_t>(values[n].size()) * 2 + 2);
    AppendNullTerminatedString(values[n]);
    EndEntry();
  }
}

bool PRegPolicyWriter::WriteToFile(const base::FilePath& path) {
  int size = static_cast<int>(buffer_.size());
  return base::WriteFile(path, buffer_.data(), size) == size;
}

void PRegPolicyWriter::StartEntry(const std::string& key_name,
                                  const std::string& value_name,
                                  uint32_t data_type,
                                  uint32_t data_size) {
  DCHECK(!entry_started_);
  entry_started_ = true;

  AppendChar16(kDelimBracketOpen);

  AppendNullTerminatedString(key_name);
  AppendChar16(kDelimSemicolon);

  AppendNullTerminatedString(value_name);
  AppendChar16(kDelimSemicolon);

  AppendUnsignedInt(data_type);
  AppendChar16(kDelimSemicolon);

  AppendUnsignedInt(data_size);
  AppendChar16(kDelimSemicolon);
}

void PRegPolicyWriter::EndEntry() {
  AppendChar16(kDelimBracketClose);

  DCHECK(entry_started_);
  entry_started_ = false;
}

void PRegPolicyWriter::AppendNullTerminatedString(const std::string& str) {
  for (char ch : str)
    AppendChar16(static_cast<base::char16>(ch));
  AppendChar16(kDelimNull);
}

void PRegPolicyWriter::AppendUnsignedInt(uint32_t value) {
  AppendChar16(value & 0xffff);
  AppendChar16(value >> 16);
}

void PRegPolicyWriter::AppendChar16(base::char16 ch) {
  buffer_.append(1, ch & 0xff);
  buffer_.append(1, ch >> 8);
}

const std::string& PRegPolicyWriter::GetKey(PolicyLevel level) {
  return level == POLICY_LEVEL_RECOMMENDED ? recommended_key_ : mandatory_key_;
}

PRegUserDevicePolicyWriter::PRegUserDevicePolicyWriter() {
  SetKeysForUserDevicePolicy();
}

PRegExtensionPolicyWriter::PRegExtensionPolicyWriter(
    const std::string& extension_id) {
  SetKeysForExtensionPolicy(extension_id);
}

}  // namespace policy

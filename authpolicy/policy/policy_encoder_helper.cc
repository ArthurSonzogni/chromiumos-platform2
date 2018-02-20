// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/policy/policy_encoder_helper.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string16.h>
#include <base/strings/utf_string_conversions.h>
#include <base/sys_info.h>
#include <components/policy/core/common/policy_load_status.h>
#include <components/policy/core/common/preg_parser.h>
#include <components/policy/core/common/registry_dict.h>

namespace policy {
namespace {

// TODO(ljusten): Copied from latest Chromium base::Value::GetTypeName, remove
// once the latest code is merged.
const char* const kTypeNames[] = {"null",   "boolean", "integer",    "double",
                                  "string", "binary",  "dictionary", "list"};
static_assert(arraysize(kTypeNames) ==
                  static_cast<size_t>(base::Value::Type::LIST) + 1,
              "kTypeNames Has Wrong Size");

const char* GetValueTypeName(const base::Value* value) {
  DCHECK_GE(static_cast<int>(value->GetType()), 0);
  DCHECK_LT(static_cast<size_t>(value->GetType()), arraysize(kTypeNames));
  return kTypeNames[static_cast<size_t>(value->GetType())];
}

}  // namespace

constexpr char kKeyUserDevice[] = "Software\\Policies\\Google\\ChromeOS";
constexpr char kKeyExtensions[] =
    "Software\\Policies\\Google\\Chrome\\3rdparty\\Extensions";
constexpr char kKeyRecommended[] = "Recommended";
constexpr char kKeyMandatoryExtension[] = "Policy";

bool LoadPRegFile(const base::FilePath& preg_file,
                  const char* registry_key,
                  RegistryDict* dict) {
  if (!base::PathExists(preg_file)) {
    LOG(ERROR) << "PReg file '" << preg_file.value() << "' does not exist";
    return false;
  }

  // Note: Don't use PolicyLoadStatusUmaReporter here, it leaks, see
  // crbug.com/717888. Simply eat the status and report a less fine-grained
  // ERROR_PARSE_PREG_FAILED error in authpolicy. It would be possible to get
  // the load status into authpolicy, but that would require a lot of plumbing
  // since this code usually runs in a sandboxed process.
  PolicyLoadStatusSampler status;
  const base::string16 registry_key_utf16 = base::ASCIIToUTF16(registry_key);
  if (!preg_parser::ReadFile(preg_file, registry_key_utf16, dict, &status)) {
    LOG(ERROR) << "Failed to parse preg file '" << preg_file.value() << "'";
    return false;
  }

  return true;
}

bool GetAsBoolean(const base::Value* value, bool* bool_value) {
  if (value->GetAsBoolean(bool_value))
    return true;

  // Boolean policies are represented as integer 0/1 in the registry.
  int int_value = 0;
  if (value->GetAsInteger(&int_value) && (int_value == 0 || int_value == 1)) {
    *bool_value = int_value != 0;
    return true;
  }

  return false;
}

bool GetAsInteger(const base::Value* value, int* int_value) {
  return value->GetAsInteger(int_value);
}

bool GetAsString(const base::Value* value, std::string* string_value) {
  return value->GetAsString(string_value);
}

void PrintConversionError(const base::Value* value,
                          const char* target_type,
                          const char* policy_name,
                          const std::string* index_str) {
  LOG(ERROR) << "Failed to convert value '" << *value << " of type '"
             << GetValueTypeName(value) << "'"
             << " to " << target_type << " for policy '" << policy_name << "'"
             << (index_str ? " at index " + *index_str : "");
}

bool GetAsIntegerInRangeAndPrintError(const base::Value* value,
                                      int range_min,
                                      int range_max,
                                      const char* policy_name,
                                      int* int_value) {
  *int_value = 0;
  if (!GetAsInteger(value, int_value)) {
    PrintConversionError(value, "integer", policy_name);
    return false;
  }

  if (*int_value < range_min || *int_value > range_max) {
    *int_value = 0;
    LOG(ERROR) << "Value of policy '" << policy_name << "' is " << value
               << ", outside of expected range [" << range_min << ","
               << range_max << "]";
    return false;
  }

  return true;
}

}  // namespace policy

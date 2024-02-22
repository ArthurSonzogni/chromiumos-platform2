// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/cryptohome.h"

#include <openssl/sha.h>
#include <stdint.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

namespace brillo::cryptohome::home {
namespace {

using base::FilePath;

// Daemon store main directory.
constexpr char kDaemonStorePath[] = "/run/daemon-store";

constexpr char kRootHomePrefix[] = "/home/root/";
constexpr char kDefaultLegacySystemSaltPath[] = "/home/.shadow/salt";
constexpr char kDefaultSystemSaltPath[] = "/var/lib/system_salt";

char g_user_home_prefix[PATH_MAX] = "/home/user/";

SystemSaltLoader* g_system_salt_loader = nullptr;

}  // namespace

bool IsLegacySystemSalt(base::FilePath root) {
  // Append wants a relative path, so "+1" to skip leading "/".
  return base::PathExists(root.Append(kDefaultLegacySystemSaltPath + 1));
}

const Username& GetGuestUsername() {
  static const base::NoDestructor<Username> kGuest("$guest");
  return *kGuest;
}

bool EnsureSystemSaltIsLoaded() {
  return SystemSaltLoader::GetInstance()->EnsureLoaded();
}

ObfuscatedUsername SanitizeUserName(const Username& username) {
  if (!EnsureSystemSaltIsLoaded())
    return ObfuscatedUsername();

  return SanitizeUserNameWithSalt(
      username,
      SecureBlob(*SystemSaltLoader::GetInstance()->value_or_override()));
}

ObfuscatedUsername SanitizeUserNameWithSalt(const Username& username,
                                            const SecureBlob& salt) {
  unsigned char binmd[SHA_DIGEST_LENGTH];
  std::string lowercase(*username);
  std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(),
                 ::tolower);
  SHA_CTX ctx;
  SHA1_Init(&ctx);
  SHA1_Update(&ctx, salt.data(), salt.size());
  SHA1_Update(&ctx, lowercase.data(), lowercase.size());
  SHA1_Final(binmd, &ctx);
  std::string final = base::HexEncode(binmd, sizeof(binmd));
  // Stay compatible with CryptoLib::HexEncodeToBuffer()
  std::transform(final.begin(), final.end(), final.begin(), ::tolower);
  return ObfuscatedUsername(std::move(final));
}

FilePath GetUserPathPrefix() {
  return FilePath(g_user_home_prefix);
}

FilePath GetRootPathPrefix() {
  return FilePath(kRootHomePrefix);
}

FilePath GetUserPath(const Username& username) {
  if (!SystemSaltLoader::GetInstance()->EnsureLoaded())
    return FilePath();
  return GetUserPath(SanitizeUserName(username));
}

FilePath GetUserPath(const ObfuscatedUsername& username) {
  return FilePath(
      base::StringPrintf("%s%s", g_user_home_prefix, username->c_str()));
}

FilePath GetRootPath(const Username& username) {
  if (!SystemSaltLoader::GetInstance()->EnsureLoaded())
    return FilePath();
  return GetRootPath(SanitizeUserName(username));
}

FilePath GetRootPath(const ObfuscatedUsername& username) {
  return FilePath(
      base::StringPrintf("%s%s", kRootHomePrefix, username->c_str()));
}

FilePath GetDaemonStorePath(const Username& username,
                            const std::string& daemon) {
  if (!SystemSaltLoader::GetInstance()->EnsureLoaded())
    return FilePath();
  return GetDaemonStorePath(SanitizeUserName(username), daemon);
}

FilePath GetDaemonStorePath(const ObfuscatedUsername& username,
                            const std::string& daemon) {
  return FilePath(kDaemonStorePath).Append(daemon).Append(*username);
}

bool IsSanitizedUserName(const std::string& sanitized) {
  std::vector<uint8_t> bytes;
  return (sanitized.length() == 2 * SHA_DIGEST_LENGTH) &&
         base::HexStringToBytes(sanitized, &bytes);
}

void SetUserHomePrefix(const std::string& prefix) {
  if (prefix.length() < sizeof(g_user_home_prefix)) {
    snprintf(g_user_home_prefix, sizeof(g_user_home_prefix), "%s",
             prefix.c_str());
  }
}

std::string* GetSystemSalt() {
  return SystemSaltLoader::GetInstance()->value_or_override();
}

void SetSystemSalt(std::string* value) {
  SystemSaltLoader::GetInstance()->override_value_for_testing(value);
}

SystemSaltLoader* SystemSaltLoader::GetInstance() {
  if (!g_system_salt_loader) {
    static base::NoDestructor<SystemSaltLoader> default_instance;
    return default_instance.get();
  }
  return g_system_salt_loader;
}

SystemSaltLoader::SystemSaltLoader()
    : SystemSaltLoader({base::FilePath(kDefaultLegacySystemSaltPath),
                        base::FilePath(kDefaultSystemSaltPath)}) {}

SystemSaltLoader::~SystemSaltLoader() {
  DCHECK_EQ(g_system_salt_loader, this);
  g_system_salt_loader = nullptr;
}

bool SystemSaltLoader::EnsureLoaded() {
  if (!value_.empty() || value_override_for_testing_) {
    return true;
  }
  for (const auto& f : file_paths_) {
    if (!base::PathExists(f)) {
      continue;
    }
    if (base::ReadFileToString(f, &value_)) {
      return true;
    }
    PLOG(ERROR) << "Error reading system salt from " << f;
  }
  LOG(WARNING) << "No valid system salt file found";
  value_.clear();
  return false;
}

const std::string& SystemSaltLoader::value() const {
  return value_;
}

std::string* SystemSaltLoader::value_or_override() {
  if (value_override_for_testing_) {
    return value_override_for_testing_;
  }
  if (!value_.empty()) {
    return &value_;
  }
  return nullptr;
}

void SystemSaltLoader::override_value_for_testing(std::string* new_value) {
  value_override_for_testing_ = new_value;
}

SystemSaltLoader::SystemSaltLoader(base::FilePath file_path)
    : SystemSaltLoader(std::vector<base::FilePath>{std::move(file_path)}) {}

SystemSaltLoader::SystemSaltLoader(std::vector<base::FilePath> file_paths)
    : file_paths_(std::move(file_paths)) {
  for (const auto& f : file_paths_) {
    DCHECK(!f.empty());
  }
  DCHECK_EQ(g_system_salt_loader, nullptr);
  g_system_salt_loader = this;
}

}  // namespace brillo::cryptohome::home

// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUTHPOLICY_PATH_SERVICE_H_
#define AUTHPOLICY_PATH_SERVICE_H_

#include <map>
#include <string>

#include <base/macros.h>

namespace authpolicy {

enum class Path {
  // Invalid path, not set, triggers a DCHECK in PathService::Get().
  INVALID,

  // Base directories.
  TEMP_DIR,
  STATE_DIR,

  // Samba directories.
  SAMBA_DIR,
  SAMBA_LOCK_DIR,
  SAMBA_CACHE_DIR,
  SAMBA_STATE_DIR,
  SAMBA_PRIVATE_DIR,
  GPO_LOCAL_DIR,  // Location of downloaded GPOs.

  // Configuration files.
  CONFIG_DAT,  // Authpolicy configuration.
  SMB_CONF,    // Samba configuration.

  // Kerberos configuration.
  USER_KRB5_CONF,
  DEVICE_KRB5_CONF,

  // Credential cache paths.
  USER_CREDENTIAL_CACHE,
  DEVICE_CREDENTIAL_CACHE,

  // Keytab files.
  MACHINE_KT_STATE,  // Persistent machine keytab.
  MACHINE_KT_TEMP,   // Temp machine keytab.

  // Samba/Kerberos/parser executables.
  KINIT,
  NET,
  SMBCLIENT,
  PARSER,

  // Seccomp filter policies.
  KINIT_SECCOMP,
  NET_ADS_SECCOMP,
  PARSER_SECCOMP,
  SMBCLIENT_SECCOMP,

  // Misc.
  DEBUG_FLAGS,  // File with debug flags.
  KRB5_TRACE,   // kinit trace log.
};

// Simple path service.
class PathService {
 public:
  // Calls Initialize().
  PathService();
  virtual ~PathService();

  // Retrieves the file or directory path for the given |path_key|.
  const std::string& Get(Path path_key) const;

 protected:
  // Calls Initialize() if |initialize| is true.
  explicit PathService(bool initialize);

  // Should be called at some point during construction to initialize all paths.
  // Derived classes can override paths by specifying a constuctor that calls
  // PathService(false), inserts paths and then calls Initialize() to initialize
  // paths not set yet.
  void Initialize();

  // Inserts |path| at key |path_key| into |path_map_| if the key is not
  // already set.
  void Insert(Path path_key, const std::string& path);

 private:
  std::map<Path, std::string> paths_;

  DISALLOW_COPY_AND_ASSIGN(PathService);
};

}  // namespace authpolicy

#endif  // AUTHPOLICY_PATH_SERVICE_H_

// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/shill_config.h"

namespace shill {

// static
const char Config::kShillDefaultPrefsDir[] = "/var/lib/shill";

// static
const char Config::kDefaultRunDirectory[] = RUNDIR;
// static
const char Config::kDefaultStorageDirectory[] = "/var/cache/shill";
// static
const char Config::kDefaultUserStorageFormat[] = RUNDIR "/user_profiles/%s";
// static
const char Config::kFlimflamRunDirectory[] = "/var/run/flimflam";
// static
const char Config::kFlimflamStorageDirectory[] = "/var/cache/flimflam";
// static
const char Config::kFlimflamUserStorageFormat[] = "/home/%s/user/flimflam";

Config::Config() : use_flimflam_(false) {
}

Config::~Config() {}

std::string Config::GetRunDirectory() {
  return (use_flimflam_ ? kFlimflamRunDirectory : kDefaultRunDirectory);
}

std::string Config::GetStorageDirectory() {
  return (use_flimflam_ ? kFlimflamStorageDirectory : kDefaultStorageDirectory);
}

std::string Config::GetUserStorageDirectoryFormat() {
  return (use_flimflam_ ?
          kFlimflamUserStorageFormat :
          kDefaultUserStorageFormat);
}

}  // namespace shill

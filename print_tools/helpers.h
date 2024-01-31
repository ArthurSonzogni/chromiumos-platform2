// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINT_TOOLS_HELPERS_H_
#define PRINT_TOOLS_HELPERS_H_

#include <string>

#include <netdb.h>

// Validates the protocol of `url` and modifies it if necessary. The protocols
// ipp and ipps are converted to http and https, respectively. If the
// conversion occurs, adds a port number if one is not specified.
// Prints an error message to stderr and returns false in the following cases:
// * `url` does not contain "://" substring
// * the protocol is not one of http, https, ipp or ipps.
// Does not verify the correctness of the given URL.
bool ConvertIppToHttp(std::string& url);

// Function signature matching `getaddrinfo`.  Used for unit test dependency
// injection in `ResolveZeroconfHostname`.
typedef int (*ResolveFunc)(const char*,
                           const char*,
                           const struct addrinfo*,
                           struct addrinfo**);

// If `url` contains a hostname ending with .local, use `resolver` to look it up
// and replace the hostname with the first IP address returned.  Leave `url`
// unchanged for any other hostname.  This function assumes that `url` has
// already been normalized via `ConvertIppToHttp`.
bool ResolveZeroconfHostname(std::string& url, ResolveFunc resolver = nullptr);

#endif  //  PRINT_TOOLS_HELPERS_H_

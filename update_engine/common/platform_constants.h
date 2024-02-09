// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_PLATFORM_CONSTANTS_H_
#define UPDATE_ENGINE_COMMON_PLATFORM_CONSTANTS_H_

namespace chromeos_update_engine {
namespace constants {

// The default URL used by all products when running in normal mode. The AUTest
// URL is used when testing normal images against the alternative AUTest server.
// Note that the URL can be override in run-time in certain cases.
extern const char kOmahaDefaultProductionURL[];
extern const char kOmahaDefaultAUTestURL[];

// Our product name used in Omaha. This value must match the one configured in
// the server side and is sent on every request.
extern const char kOmahaUpdaterID[];

// The name of the platform as sent to Omaha.
extern const char kOmahaPlatformName[];

// Path to the location of the public half of the payload key. The payload key
// is used to sign the contents of the payload binary file: the manifest and the
// whole payload.
extern const char kUpdatePayloadPublicKeyPath[];

// Path to the location of the zip archive file that contains PEM encoded X509
// certificates. e.g. 'system/etc/security/otacerts.zip'.
extern const char kUpdateCertificatesPath[];

// Path to the directory containing all the SSL certificates accepted by
// update_engine when sending requests to Omaha and the download server (if
// HTTPS is used for that as well).
extern const char kCACertificatesPath[];

// The stateful directory used by update_engine.
extern const char kNonVolatileDirectory[];

// Recovery key version file that exists under the non-volatile directory.
extern const char kRecoveryKeyVersionFileName[];

// Options passed to the filesystem when mounting the new partition during
// postinstall.
extern const char kPostinstallMountOptions[];

}  // namespace constants
}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_PLATFORM_CONSTANTS_H_

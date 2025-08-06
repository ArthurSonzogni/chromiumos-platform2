// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2025-08-06-000105_4eed1ee9ccfb918be25f1f402e1a4c3fd91fbc4a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6b2bab0e7333831375c1fd0ab553f1ddfd121f9e11fffb1d4c7a9a839d1f442b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5ed53576abd3eb59fd51f4c16744bf8492197419e9de5eb83c18239bcf2c010f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

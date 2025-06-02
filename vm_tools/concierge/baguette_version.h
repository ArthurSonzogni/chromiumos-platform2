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
constexpr char kBaguetteVersion[] = "2025-06-02-000145_9661060b3521aa258bc7e6fa50fe4230c98aec18";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "05f541463760dca3941677ac9a1cae0ea91685f359a80d370aead20222382b6a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a4be085d1d138820e5508d3c3d0909043a15210fc37d66447ec2ab05f9278cf5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

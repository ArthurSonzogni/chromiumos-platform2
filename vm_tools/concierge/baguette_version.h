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
constexpr char kBaguetteVersion[] = "2025-04-11-000105_8475444ef887e9fc52f4cb8f346103d92caf3484";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e4754544c1dcfe10ae0e81fb049a8dec4b6319e2be7b18de7724d9928e537965";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "af87b50e66c238b5c84233a944a7a57f8380b6ea3bb1456fd9e8849ffaf8ae36";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

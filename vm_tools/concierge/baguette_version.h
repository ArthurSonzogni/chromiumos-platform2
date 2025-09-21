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
constexpr char kBaguetteVersion[] = "2025-09-21-000130_8fc8ca03c761e3abb69d2903f43d6c0132060f91";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "14ec4902cab499bcb7b83ca38f904f35120614364f2e1b9223ca706656b1c989";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ed2161ec866cce8ee27941c9bd735b4ff481044633219fad9b4544c4de746358";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

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
constexpr char kBaguetteVersion[] = "2026-05-04-000153_78323ea4f3462008bcbb384a00c2b3358cb5c82a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a2e580a73172a0aefe6da9fd903b66976021c50aef94853aca42e6806f978d13";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "976c670efd7c7284396ba931a0693bad001aa721260e1275edfa08b644328e52";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

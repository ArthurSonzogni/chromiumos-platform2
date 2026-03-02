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
constexpr char kBaguetteVersion[] = "2026-03-02-000123_e048c70e960f4a04ede8f7d6030e560986d511f6";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "00c398cfd321671c7213b1fafdcf90d525a02b6b2bc757dd966166d979588ec6";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "34ac41208062e8a357d8b558d971ef6a66cfe1a313de5cf378c713bf7e088675";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

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
constexpr char kBaguetteVersion[] = "2026-10-14-000112_3932278a009f748fdc2605a40fe8b295e011b956";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b7dec9c845422cb881066a52389f4fbaed899bbd3f47e720dcc46f52b892efe4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "850baa30a9203849698a8f698aaafb723757ef45e0a899ff745d37cc4998595b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

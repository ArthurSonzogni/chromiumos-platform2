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
constexpr char kBaguetteVersion[] = "2025-12-08-000137_cd9c880901f27cfdbceaa2228743b58565ae8449";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4e3e74902994cc0f637c0829663b4a7f106e3c3ddab997fa5baff86509d43f0b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ba14527b48b05186843e8f962f1f5b2f6324a913b687baf4b5e81ee605948165";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

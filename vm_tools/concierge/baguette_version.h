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
constexpr char kBaguetteVersion[] = "2025-09-05-000110_da4dafdf0295892bf250d11c47128acedb00125d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a42bf2e8bc28c1e55de09043f4e1ec566a38cb29fb33a3e3a398633fa05ba739";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "30bf2bc23ff1dc36a4642624c97cf51dc50ef6369bbdaee9cedf8a5e14f12609";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

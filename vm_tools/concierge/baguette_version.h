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
constexpr char kBaguetteVersion[] = "2025-12-31-000125_71c72e751354cf069846be86dfc3a2dee0488948";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "93421601eb15288fa34d65c75b6036383d0b975e91acf7bbb6ed8621694e6937";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "08be212e8227c44e7f4255d85213c2b80ba6843cd4275bf7d8809ae673c98897";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

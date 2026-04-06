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
constexpr char kBaguetteVersion[] = "2026-04-06-000119_cb1fe7160f2ce0be80b5ff312a62c54577f692d1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e440bc6615696f3a8b78dc549cbcf38e57792b7032852122a56dae3eee9628f7";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0ab34d78673dab4938f996f2e2bf1d1c3b050005605ee88beb91cbf40ad233e3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

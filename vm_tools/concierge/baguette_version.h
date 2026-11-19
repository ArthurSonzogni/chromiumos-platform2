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
constexpr char kBaguetteVersion[] = "2026-11-19-000115_30399c368da430224dfb0ed73296f4d1fae2e1af";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2445fd906295f9cb8aca2e1bbb9c5a4b7048c4ab92735d500512ae30f80cc58e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "03c2a2cbb15d1b6728ee93a863fd847a3f61e37a9e55391e59656c4316fed5df";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

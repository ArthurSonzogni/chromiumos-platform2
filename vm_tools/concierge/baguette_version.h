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
constexpr char kBaguetteVersion[] = "2026-11-03-000122_d1b2e2c2f86f5bbf00cecd4fc1cb1b1edfbb4a70";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "275bc00bebcc794546eb25a5f025ee54eee4169e235bf5944367f25cf6b02d5e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9332f7f5a717368031fe761c5db70023a6e6d69a642a1475da1be3316a28acc5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

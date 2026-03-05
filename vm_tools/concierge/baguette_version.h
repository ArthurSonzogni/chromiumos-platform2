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
constexpr char kBaguetteVersion[] = "2026-03-05-000105_62c026ade18fdba38e10a05c049c939973b9aa4b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "afffe1504232d9ace305b49f56e7d811d1d6a00fb6fca0bb0774d97b55afbfae";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4ca25d3b8fb2e74a1b00948609df22e86590b049a7d121bcf0f54298e7a08623";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

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
constexpr char kBaguetteVersion[] = "2026-11-12-000121_33545b5456e72ef7e65305617740915542393062";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a3691e005d45368cb8c36836d3ad25c101cb8b64f0c55d61206fbeb21c72ea49";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c83a8ed644358bc1107fc2fc0e23267a2d9ec30a86d32c624ed9f02ad56d6a94";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

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
constexpr char kBaguetteVersion[] = "2026-07-15-000201_04a784ffd9250d4752653f4a9c20953cdaf03b3c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b976b5a29d9134e6bfdb496e0057055498a2e1c5bff91b888f76f6d905a00a81";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "677e03eea36236c0008daa6b6179c9649b648e7e9687033d3713202eae83dcd6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

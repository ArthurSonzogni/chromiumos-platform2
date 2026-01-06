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
constexpr char kBaguetteVersion[] = "2026-01-06-000119_5d96ace50c79502e6aaf8af770e867190ebda4c5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d9870f47448213193144c32798c90b2b9b14aa6273ac282503d3e6ab80ca9fee";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4b4a1b2e0ab58aba27da2c53d6931bcff39d0be07fc1b7ae877b7982633257cf";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

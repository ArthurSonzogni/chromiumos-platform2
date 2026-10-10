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
constexpr char kBaguetteVersion[] = "2026-10-10-000233_18012685939df38930c9653992f1829845df15f3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "691ff44cc57bf89742b1b930841b484535a6c1593361948f2d3e1cebe5fa3c31";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "faa481c1b0caa4cc79c6dbef201fe93781bca49601258dd5bdbb62d205dac7da";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

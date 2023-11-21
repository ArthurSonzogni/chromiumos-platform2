// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_BPF_CONSTANTS_H_
#define PATCHPANEL_BPF_CONSTANTS_H_

// This file contains constants which should be shared between patchpanel and
// the bpf_loader.
namespace patchpanel {

// The bpffs path for holding eBPF objects of patchpanel. The parent path is
// mounted by chromeos-init and this path itself will be created by
// patchpanel_bpf_loader only on supported kernels.
constexpr char kBPFPath[] = "/sys/fs/bpf/patchpanel";

// The pinned eBPF program for WebRTC detection which is supposed to be used by
// iptables.
constexpr char kWebRTCMatcherPinPath[] =
    "/sys/fs/bpf/patchpanel/match_dtls_srtp";

}  // namespace patchpanel

#endif  // PATCHPANEL_BPF_CONSTANTS_H_

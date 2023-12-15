// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_RESIZE_PRIORITY_H_
#define VM_TOOLS_CONCIERGE_MM_RESIZE_PRIORITY_H_

#include <vm_memory_management/vm_memory_management.pb.h>

namespace vm_tools::concierge::mm {

// An exhaustive list of resize priorities ordered in decreasing priority order.
enum ResizePriority {
  kInvalid = 0,
  kBalloonStall = 1,
  kNoKillCandidatesHost = 2,
  kNoKillCandidatesGuest = 3,
  kFocusedTab = 4,
  kFocusedApp = 5,
  kPerceptibleTab = 6,
  kPerceptibleApp = 7,
  kCachedTab = 8,
  kCachedApp = 9,
  kMglruReclaim = 10,
};

// Returns the highest priority ResizePriority.
constexpr ResizePriority HighestResizePriority() {
  return ResizePriority::kBalloonStall;
}

// Returns the lowest priority ResizePriority.
constexpr ResizePriority LowestResizePriority() {
  return ResizePriority::kMglruReclaim;
}

constexpr const char* kResizePriorityNames[] = {
    "Invalid",
    "BalloonStall",
    "NoKillCandidatesHost",
    "NoKillCandidatesGuest",
    "FocusedTab",
    "FocusedApp",
    "PerceptibleTab",
    "PerceptibleApp",
    "CachedTab",
    "CachedApp",
    "MglruReclaim",
};
static_assert(
    sizeof(kResizePriorityNames) / sizeof(kResizePriorityNames[0]) ==
        LowestResizePriority() + 1,
    "Ensure there is a text representation for every ResizePriority entry.");

// Allows for iteration of resize priorities in increasing priority order.
constexpr ResizePriority kAllResizePrioritiesIncreasing[] = {
    ResizePriority::kMglruReclaim,
    ResizePriority::kCachedApp,
    ResizePriority::kCachedTab,
    ResizePriority::kPerceptibleApp,
    ResizePriority::kPerceptibleTab,
    ResizePriority::kFocusedApp,
    ResizePriority::kFocusedTab,
    ResizePriority::kNoKillCandidatesGuest,
    ResizePriority::kNoKillCandidatesHost,
    ResizePriority::kBalloonStall};
static_assert(sizeof(kAllResizePrioritiesIncreasing) /
                      sizeof(kAllResizePrioritiesIncreasing[0]) ==
                  LowestResizePriority(),
              "Ensure there is an entry for every valid ResizePriority.");

// Returns the concierge internal ResizePriority value that corresponds to
// |proto_priority|. Note that concierge's internal ResizePriority is a superset
// of the protobuf ResizePriority enum, so not all ResizePriority values can be
// returned by this function.
constexpr ResizePriority FromProtoResizePriority(
    vm_tools::vm_memory_management::ResizePriority proto_priority) {
  switch (proto_priority) {
    case vm_tools::vm_memory_management::ResizePriority::
        RESIZE_PRIORITY_FOCUSED_TAB:
      return ResizePriority::kFocusedTab;
    case vm_tools::vm_memory_management::ResizePriority::
        RESIZE_PRIORITY_FOCUSED_APP:
      return ResizePriority::kFocusedApp;
    case vm_tools::vm_memory_management::ResizePriority::
        RESIZE_PRIORITY_PERCEPTIBLE_TAB:
      return ResizePriority::kPerceptibleTab;
    case vm_tools::vm_memory_management::ResizePriority::
        RESIZE_PRIORITY_PERCEPTIBLE_APP:
      return ResizePriority::kPerceptibleApp;
    case vm_tools::vm_memory_management::ResizePriority::
        RESIZE_PRIORITY_CACHED_TAB:
      return ResizePriority::kCachedTab;
    case vm_tools::vm_memory_management::ResizePriority::
        RESIZE_PRIORITY_CACHED_APP:
      return ResizePriority::kCachedApp;
    default:
      return ResizePriority::kInvalid;
  }
}

constexpr std::ostream& operator<<(std::ostream& os, ResizePriority priority) {
  os << kResizePriorityNames[priority];
  return os;
}

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_RESIZE_PRIORITY_H_

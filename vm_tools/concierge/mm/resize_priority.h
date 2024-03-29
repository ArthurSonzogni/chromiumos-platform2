// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_RESIZE_PRIORITY_H_
#define VM_TOOLS_CONCIERGE_MM_RESIZE_PRIORITY_H_

#include <base/containers/fixed_flat_map.h>

#include <vm_memory_management/vm_memory_management.pb.h>

namespace vm_tools::concierge::mm {

// When adding a new ResizePriority, a few places must be updated:
//
// Add the new ResizePriority to enum ResizePriority and the corresponding
// string representation (kResizePriorityNames), and priority list
// (kAllResizePrioritiesIncreasing).
//
// If this new ResizePriority is mirroring a new priority that was added to
// vm_memory_management.proto, update FromProtoResizePriority to support the new
// priority.
//
// Add a new UmaResizePriority to the end and update
// kResizePriorityToUmaResizePriority.
//
// Update tools/metrics/histograms/metadata/memory/enums.xml in the chromium
// repo to support the new UmaResizePriority entry.

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
  kAggressiveBalloon = 9,
  kCachedApp = 10,
  kStaleCachedTab = 11,
  kStaleCachedApp = 12,
  kMglruReclaim = 13,
};

// Contains the valid resize priorities for UMA metrics. This enum is append
// only to ensure historical UMA metrics remain accurate. Do not modify existing
// entries. Instead modify the kResizePriorityToUmaResizePriority mapping below
// when a new ResizePriority is added.
enum UmaResizePriority {
  kUmaInvalid = 0,
  kUmaBalloonStall = 1,
  kUmaNoKillCandidatesHost = 2,
  kUmaNoKillCandidatesGuest = 3,
  kUmaFocusedTab = 4,
  kUmaFocusedApp = 5,
  kUmaPerceptibleTab = 6,
  kUmaPerceptibleApp = 7,
  kUmaCachedTab = 8,
  kUmaAggressiveBalloon = 9,
  kUmaCachedApp = 10,
  kUmaMglruReclaim = 11,
  kUmaStaleCachedTab = 12,
  kUmaStaleCachedApp = 13,
  kMax = kStaleCachedApp,
};
static_assert(kUmaMglruReclaim == 11,
              "UmaResizePriority is append only. Do not change values of "
              "existing entries.");

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
    "AggressiveBalloon",
    "CachedApp",
    "StaleCachedTab",
    "StaleCachedApp",
    "MglruReclaim",
};
static_assert(
    sizeof(kResizePriorityNames) / sizeof(kResizePriorityNames[0]) ==
        LowestResizePriority() + 1,
    "Ensure there is a text representation for every ResizePriority entry.");

// Allows for iteration of resize priorities in increasing priority order.
constexpr ResizePriority kAllResizePrioritiesIncreasing[] = {
    ResizePriority::kMglruReclaim,
    ResizePriority::kStaleCachedApp,
    ResizePriority::kStaleCachedTab,
    ResizePriority::kCachedApp,
    ResizePriority::kAggressiveBalloon,
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
    case vm_tools::vm_memory_management::ResizePriority::
        RESIZE_PRIORITY_STALE_CACHED_TAB:
      return ResizePriority::kStaleCachedTab;
    case vm_tools::vm_memory_management::ResizePriority::
        RESIZE_PRIORITY_STALE_CACHED_APP:
      return ResizePriority::kStaleCachedApp;
    default:
      return ResizePriority::kInvalid;
  }
}

constexpr std::ostream& operator<<(std::ostream& os, ResizePriority priority) {
  os << kResizePriorityNames[priority];
  return os;
}

// Contains the mapping of ResizePriorities to the UMA value.
constexpr auto kResizePriorityToUmaResizePriority =
    base::MakeFixedFlatMap<ResizePriority, UmaResizePriority>({
#define UMA_RESIZE_PRIORITY_PAIR(A) \
  {ResizePriority::k##A, UmaResizePriority::kUma##A}
        UMA_RESIZE_PRIORITY_PAIR(Invalid),
        UMA_RESIZE_PRIORITY_PAIR(BalloonStall),
        UMA_RESIZE_PRIORITY_PAIR(NoKillCandidatesHost),
        UMA_RESIZE_PRIORITY_PAIR(NoKillCandidatesGuest),
        UMA_RESIZE_PRIORITY_PAIR(FocusedTab),
        UMA_RESIZE_PRIORITY_PAIR(FocusedApp),
        UMA_RESIZE_PRIORITY_PAIR(PerceptibleTab),
        UMA_RESIZE_PRIORITY_PAIR(PerceptibleApp),
        UMA_RESIZE_PRIORITY_PAIR(CachedTab),
        UMA_RESIZE_PRIORITY_PAIR(AggressiveBalloon),
        UMA_RESIZE_PRIORITY_PAIR(CachedApp),
        UMA_RESIZE_PRIORITY_PAIR(StaleCachedTab),
        UMA_RESIZE_PRIORITY_PAIR(StaleCachedApp),
        UMA_RESIZE_PRIORITY_PAIR(MglruReclaim),
    });
#undef UMA_RESIZE_PRIORITY_PAIR

static_assert(kResizePriorityToUmaResizePriority.size() ==
                  LowestResizePriority() + 1,
              "kResizePriorityToUmaResizePriority must have an entry for every "
              "ResizePriority.");

// The number of buckets to use for metrics that track resize priorities.
constexpr size_t kNumUmaResizePriorityBuckets = UmaResizePriority::kMax + 1;

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_RESIZE_PRIORITY_H_

// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_BPF_PROCESS_H_
#define SECAGENTD_BPF_PROCESS_H_

#ifdef __cplusplus
#include <stdint.h>
#define _Static_assert static_assert
namespace secagentd::bpf {
#endif

// The max arg size set by limits.h is ~128KB. To avoid consuming an absurd
// amount of memory arguments will be truncated to 512 bytes. If all 512
// bytes are used the consuming userspace daemon will scrape procfs for the
// entire command line.
#define MAX_REDUCED_ARG_SIZE (512)

// Although the maximum path size defined in linux/limits.h is larger we
// truncate path sizes to keep memory usage reasonable. If needed the full path
// name can be regenerated from the inode in image_info.
#define MAX_PATH_SIZE (512)

// The size of the buffer allocated from the BPF ring buffer. The size must be
// large enough to hold the largest BPF event structure and must also be of
// 2^N size.
#define MAX_STRUCT_SIZE (2048)

typedef uint64_t time_ns_t;

// TODO(b/243571230): all of these struct fields map to kernel types.
// Since including vmlinux.h directly in this file causes numerous compilation
// errors with a cpp compiler we must instead pick a standard type. There is a
// risk that the kernel types do not map well into these standard types for
// certain architectures; so add static asserts to make sure we detect this
// failure at compile time.

// The image_info struct contains the security metrics
// of interest for an executable file.
struct image_info {
  char path_name[MAX_PATH_SIZE];
  uint64_t mount_ns;
  uint32_t inode_device_id;
  uint32_t inode;
  uint32_t canonical_uid;
  uint32_t canonical_gid;
  uint16_t mode;
};

// The namespace_info struct contains the namespace information for a process.
struct namespace_info {
  uint64_t cgroup_ns;
  uint64_t pid_ns;
  uint64_t user_ns;
  uint64_t uts_ns;
  uint64_t mnt_ns;
  uint64_t net_ns;
  uint64_t ipc_ns;
};

// This is the process information collected when a process starts.
struct process_start {
  uint32_t pid;                  // The tgid.
  uint32_t ppid;                 // The tgid of parent.
  time_ns_t start_time;          // Nanoseconds since boot.
  time_ns_t parent_start_time;   // Nanoseconds since boot.
  char filename[MAX_PATH_SIZE];  // Defined in linux/limits.h.
  char command_line[MAX_REDUCED_ARG_SIZE];
  unsigned int uid;
  unsigned int gid;
  struct image_info image_info;
  struct namespace_info spawn_namespace;
};

// This is the process information collected when a process exits.
struct process_exit {
  uint32_t pid;                 // This is actually the tgid.
  uint32_t ppid;                // The tgid of parent.
  time_ns_t start_time;         // Nanoseconds since boot.
  time_ns_t parent_start_time;  // Nanoseconds since boot.
};

// Indicates the type of process event is contained within the
// event structure.
enum process_event_type { process_start_type, process_exit_type };

// Contains information needed to report process security
// event telemetry regarding processes.
struct process_event {
  enum process_event_type type;
  union {
    struct process_start process_start;
    struct process_exit process_exit;
  } data;
};

// The type of security event an event structure contains.
enum event_type { process_type };

// The security event structure that contains security event information
// provided by a BPF application.
struct event {
  union {
    struct process_event process_event;
  } data;
  enum event_type type;
};

// Ensure that the ring-buffer sample that is allocated is large enough.
_Static_assert(sizeof(struct event) <= MAX_STRUCT_SIZE,
               "Event structure exceeds maximum size.");

#ifdef __cplusplus
}  //  namespace secagentd::bpf
#endif

#endif  // SECAGENTD_BPF_PROCESS_H_

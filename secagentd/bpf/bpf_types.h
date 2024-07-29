// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_BPF_BPF_TYPES_H_
#define SECAGENTD_BPF_BPF_TYPES_H_

#ifdef __cplusplus
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#define _Static_assert static_assert
namespace secagentd::bpf {
#else  // else ifdef __cplusplus
#include "include/secagentd/vmlinux/vmlinux.h"

// Kernels older than v6.6  support fentry/fexit/lsm hooks for x86 only.
#if defined(__TARGET_ARCH_x86) || LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#define CROS_FENTRY_FEXIT_SUPPORTED (1)
#define CROS_LSM_BPF_SUPPORTED (1)
#endif

// If the kernel supports fentry/fexit for the platform arch then use
// fentry/fexit, otherwise fallback to using downstream cros_net tracepoints.
#ifdef CROS_FENTRY_FEXIT_SUPPORTED
#define CROS_IF_FUNCTION_HOOK(FENTRY_FEXIT_TYPE, CROS_NET_TP_TYPE) \
  SEC(FENTRY_FEXIT_TYPE)
#else
#define CROS_IF_FUNCTION_HOOK(FENTRY_FEXIT_TYPE, CROS_NET_TP_TYPE) \
  SEC(CROS_NET_TP_TYPE)
#endif

#endif  // ifdef __cplusplus

// The max arg size set by limits.h is ~128KB. To avoid consuming an absurd
// amount of memory arguments will be truncated to 512 bytes. If all 512
// bytes are used the consuming userspace daemon will scrape procfs for the
// entire command line.
#define CROS_MAX_REDUCED_ARG_SIZE (512)

// Although the maximum path size defined in linux/limits.h is larger we
// truncate path sizes to keep memory usage reasonable. If needed the full path
// name can be regenerated from the inode in image_info.
#define CROS_MAX_PATH_SIZE (512)

#define MAX_ALLOWLISTED_FILE_MOD_DEVICES 16
#define MAX_ALLOWLISTED_HARDLINKED_INODES 1024
#define MAX_ALLOWLISTED_DIRECTORY_INODES 128
#define MAX_PATH_DEPTH 32
#define MAX_PATH_SEGMENT_SIZE (128)
#define MAX_PATH_SIZE (MAX_PATH_DEPTH * MAX_PATH_SEGMENT_SIZE)

// The size of the buffer allocated from the BPF ring buffer. The size must be
// large enough to hold the largest BPF event structure and must also be of
// 2^N size.
#define CROS_MAX_STRUCT_SIZE (2048 * 8)

#define CROS_MAX_SOCKET (1024)
#define CROS_AVG_CONN_PER_SOCKET (2)
#define CROS_MAX_FLOW_MAP_ENTRIES (CROS_MAX_SOCKET * CROS_AVG_CONN_PER_SOCKET)

// FLAG LOOKUP CONSTANTS
#define O_TMPFILE_FLAG_KEY 0
#define O_DIRECTORY_FLAG_KEY 1
#define O_RDONLY_FLAG_KEY 2
#define O_ACCMODE_FLAG_KEY 3

#ifdef __cplusplus
constexpr uint32_t kMaxFlowMapEntries{CROS_MAX_FLOW_MAP_ENTRIES};
#endif
typedef uint64_t time_ns_t;

// TODO(b/243571230): all of these struct fields map to kernel types.
// Since including vmlinux.h directly in this file causes numerous compilation
// errors with a cpp compiler we must instead pick a standard type. There is a
// risk that the kernel types do not map well into these standard types for
// certain architectures; so add static asserts to make sure we detect this
// failure at compile time.

// Fixed width version of timespec.
struct cros_timespec {
  int64_t tv_sec;
  int64_t tv_nsec;
} __attribute__((aligned(8)));

// The image_info struct contains the security metrics
// of interest for an executable file.
struct cros_image_info {
  char pathname[CROS_MAX_PATH_SIZE];
  uint64_t mnt_ns;
  uint32_t inode_device_id;
  uint32_t inode;
  uint32_t uid;
  uint32_t gid;
  uint32_t pid_for_setns;
  uint16_t mode;
  struct cros_timespec mtime;
  struct cros_timespec ctime;
} __attribute__((aligned(8)));

// The namespace_info struct contains the namespace information for a process.
struct cros_namespace_info {
  uint64_t cgroup_ns;
  uint64_t pid_ns;
  uint64_t user_ns;
  uint64_t uts_ns;
  uint64_t mnt_ns;
  uint64_t net_ns;
  uint64_t ipc_ns;
} __attribute__((aligned(8)));

// This is the process information collected when a process starts or exits.
struct cros_process_task_info {
  uint32_t pid;                 // The tgid.
  uint32_t ppid;                // The tgid of parent.
  time_ns_t start_time;         // Nanoseconds since boot.
  time_ns_t parent_start_time;  // Nanoseconds since boot.
  char commandline[CROS_MAX_REDUCED_ARG_SIZE];
  uint32_t commandline_len;  // At most CROS_MAX_REDUCED_ARG_SIZE.
  uint32_t uid;
  uint32_t gid;
  uint32_t real_commandline_len;
} __attribute__((aligned(8)));

// This is the process information collected when a process starts.
struct cros_process_start {
  struct cros_process_task_info task_info;
  struct cros_image_info image_info;
  struct cros_namespace_info spawn_namespace;
} __attribute__((aligned(8)));

// This is the process information collected when a process exits.
struct cros_process_exit {
  struct cros_process_task_info task_info;
  struct cros_image_info image_info;
  bool has_full_info;  // includes information saved off from process exec.
  bool is_leaf;        // True if process has no children.
} __attribute__((aligned(8)));

struct cros_process_change_namespace {
  // PID and start_time together will form a unique identifier for a process.
  // This unique identifier can be used to retrieve the rest of the process
  // information from a userspace process cache.
  uint32_t pid;
  time_ns_t start_time;
  struct cros_namespace_info new_ns;  // The new namespace.
} __attribute__((aligned(8)));

// Indicates the type of process event is contained within the
// event structure.
enum cros_process_event_type {
  kProcessStartEvent,
  kProcessExitEvent,
  kProcessChangeNamespaceEvent
};

// Contains information needed to report process security
// event telemetry regarding processes.
struct cros_process_event {
  enum cros_process_event_type type;
  union {
    struct cros_process_start process_start;
    struct cros_process_exit process_exit;
    struct cros_process_change_namespace process_change_namespace;
  } data;
} __attribute__((aligned(8)));

// http://www.iana.org/assignments/protocol-numbers
#define CROS_IANA_HOPOPT (0)
#define CROS_IANA_ICMP (1)
#define CROS_IANA_TCP (6)
#define CROS_IANA_UDP (17)
#define CROS_IANA_ICMP6 (58)
#define CROS_IPPROTO_RAW (255)

enum cros_network_protocol {
  CROS_PROTOCOL_TCP = CROS_IANA_TCP,
  CROS_PROTOCOL_UDP = CROS_IANA_UDP,
  CROS_PROTOCOL_ICMP = CROS_IANA_ICMP,
  CROS_PROTOCOL_ICMP6 = CROS_IANA_ICMP6,
  CROS_PROTOCOL_RAW = 251,     // Unassigned IANA number. Not a protocol.
  CROS_PROTOCOL_UNKNOWN = 252  // Unassigned IANA number. Not a protocol.
};

// AF_INET, AF_INET6 are not found in vmlinux.h so use our own
// definition here.
// We only care about AF_INET and AF_INET6 (ipv4 and ipv6).
enum cros_network_family { CROS_FAMILY_AF_INET = 2, CROS_FAMILY_AF_INET6 = 10 };

// Enum to define different file monitoring modes
enum file_monitoring_mode {
  READ_WRITE_ONLY = 0,          // Monitored for read-write access only
  READ_AND_READ_WRITE_BOTH = 1  // Monitored for both read and read-write access
};

// Enum to specify different types of device file monitoring
enum device_monitoring_type {
  MONITOR_ALL_FILES,      // Monitor all files on the device
  MONITOR_SPECIFIC_FILES  // Monitor specific files allowlisted by folder/file
                          // allowlisting map
};

// Struct to hold device file monitoring settings
struct device_file_monitoring_settings {
  enum device_monitoring_type
      device_monitoring_type;  // Type of file monitoring to apply
  enum file_monitoring_mode
      file_monitoring_mode;  // Mode of file access to monitor
} __attribute__((aligned(8)));

#ifdef __cplusplus
// make sure that the values used for our definition of families matches
// the definition in the system header.
static_assert(CROS_FAMILY_AF_INET == AF_INET);
static_assert(CROS_FAMILY_AF_INET6 == AF_INET6);
#endif

enum cros_network_socket_direction {
  CROS_SOCKET_DIRECTION_IN,      // socket is a result of an accept.
  CROS_SOCKET_DIRECTION_OUT,     // socket had connect called on it.
  CROS_SOCKET_DIRECTION_UNKNOWN  // non-connection based socket.
};

union cros_ip_addr {
  uint32_t addr4;
  uint8_t addr6[16];
} __attribute__((aligned(8)));

struct cros_network_5_tuple {
  enum cros_network_family family;
  enum cros_network_protocol protocol;
  union cros_ip_addr local_addr;
  uint16_t local_port;
  union cros_ip_addr remote_addr;
  uint16_t remote_port;
} __attribute__((aligned(8)));

/**
 * Key structure for the BPF hash map `allowlisted_file_inodes`.
 *
 * This structure represents the key used in the BPF hash map
 * `allowlisted_file_inodes`. It consists of an inode ID (`inode_id`) and a
 * device ID (`dev_id`) where the file or directory resides. It is used to
 * uniquely identify entries in the map that store the monitoring mode (`enum
 * file_monitoring_mode`) for allowlisted files and directories.
 */
struct inode_dev_map_key {
  ino_t inode_id;
  dev_t dev_id;
} __attribute__((aligned(8)));

/* The design idea behind the flow_map is that the BPF will be responsible for
 * creating and updating entries in the map. Each entry corresponds to a socket
 * identifier and a 5-tuple.
 * Userspace will periodically scan this table and generate reports from it.
 * On the release of a socket, all entries associated with that socket will be
 * marked for garbage cleanup. A socket release can cause multiple entries to be
 * marked for cleanup, this is because a single socket could send datagrams to
 * different IP addresses and ports.
 */

struct cros_flow_map_key {
  struct cros_network_5_tuple five_tuple;
  uint64_t sock_id;  // Differentiates portless protocols (ICMP, RAW).
} __attribute__((aligned(8)));

struct cros_flow_map_value {
  enum cros_network_socket_direction direction;
  uint32_t tx_bytes;
  uint32_t rx_bytes;
  struct cros_process_start process_info;
  bool has_full_process_info;
  bool garbage_collect_me;
  uint64_t sock_id;
  // TODO(b/264550183): add remote_hostname
  // TODO(b/264550183): add application protocol
  // TODO(b/264550183): add http_host
  // TODO(b/264550183): add sni_host
} __attribute__((aligned(8)));

struct cros_network_socket_listen {
  int dev_if;  // The device interface index that this socket is bound to.
  enum cros_network_family family;
  enum cros_network_protocol protocol;
  struct cros_process_start process_info;
  bool has_full_process_info;
  uint8_t socket_type;  // SOCK_STREAM, SOCK_DGRAM etc..
  uint32_t port;
  uint32_t ipv4_addr;
  uint8_t ipv6_addr[16];
} __attribute__((aligned(8)));

enum cros_network_event_type { kSyntheticNetworkFlow, kNetworkSocketListen };

/* This is not actually generated by BPFs but rather by the userspace
 * BPF skeleton wrapper. The wrapper will scan the BPF maps and then generate
 * events from that. This is done to avoid excessive specialization of the
 * BPF skeleton wrapper generic.
 */
struct cros_synthetic_network_flow {
  // We use the slightly cumbersome map data structures to
  // minimize the amount of copying that is done in userspace.
  struct cros_flow_map_key flow_map_key;
  struct cros_flow_map_value flow_map_value;
} __attribute__((aligned(8)));

struct cros_network_event {
  enum cros_network_event_type type;
  union {
    struct cros_network_socket_listen socket_listen;
    struct cros_synthetic_network_flow flow;
  } data;
} __attribute__((aligned(8)));

// Structure to hold file path segment information.
// TODO(b/359261397): Convert this to a flat array.
struct file_path_info {
  char segment_names[MAX_PATH_DEPTH]
                    [MAX_PATH_SEGMENT_SIZE];  // Array of path segments, each up
                                              // to MAX_PATH_SEGMENT_SIZE in
                                              // length.
  uint32_t segment_lengths[MAX_PATH_DEPTH];  // Array storing the length of each
                                             // segment; segment_lengths[i]
                                             // corresponds to the length of
                                             // segment_names[i].
  uint32_t num_segments;  // Total number of segments collected.
} __attribute__((aligned(8)));

// File Events Structs
struct cros_file_image {
  struct file_path_info path_info;  // Contains file path segments and related
                                    // size information. This structure helps in
                                    // constructing the full path of the file.
  uint64_t mnt_ns;                  // The mount namespace of the inode
  dev_t device_id;                  // The device ID both major and minor.
  ino_t inode;                      // The inode of the file.
  mode_t mode;                      // Mode.
  uint32_t flags;                   // Open Flags
  uid_t uid;                        // File owner user
  gid_t gid;                        // File owner group
} __attribute__((aligned(8)));

enum cros_event_type { kProcessEvent, kNetworkEvent, kFileEvent };

// Indicates the type of file event is contained within the
// event structure.
enum cros_file_event_type {
  kFileCloseEvent,
  kFileAttributeModifyEvent,
};

struct cros_file_close_event {
  struct cros_process_start process_info;
  struct cros_file_image image_info;
  struct cros_namespace_info spawn_namespace;
  bool has_full_process_info;
} __attribute__((aligned(8)));

enum filemod_type {
  FMOD_READ_ONLY_OPEN,   // File opens for reads
  FMOD_READ_WRITE_OPEN,  // File opens for writes
  FMOD_LINK,  // Hard Link Created TODO(princya): Might not be needed, if we
              // update the map when new hard link is created
  FMOD_ATTR,  // File Attribute change
};

// Contains information needed to report process security
// event telemetry regarding processes.
struct cros_file_event {
  enum cros_file_event_type type;
  enum filemod_type mod_type;
  union {
    struct cros_file_close_event file_close;
  } data;
} __attribute__((aligned(8)));

// The security event structure that contains security event information
// provided by a BPF application.
struct cros_event {
  union {
    struct cros_process_event process_event;
    struct cros_network_event network_event;
    struct cros_file_event file_event;
  } data;
  enum cros_event_type type;
} __attribute__((aligned(8)));

// Ensure that the ring-buffer sample that is allocated is large enough.
_Static_assert(sizeof(struct cros_event) <= CROS_MAX_STRUCT_SIZE,
               "Event structure exceeds maximum size.");

#ifdef __cplusplus
}  //  namespace secagentd::bpf
#endif

#endif  // SECAGENTD_BPF_BPF_TYPES_H_

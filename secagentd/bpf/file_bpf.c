// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include vmlinux.h first to declare all kernel types.
// clang-format off
#include "include/secagentd/vmlinux/vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
// clang-format on

// TODO(b/243453873): Workaround to get code completion working in CrosIDE.
#undef __cplusplus

#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf/bpf_utils.h"

const char LICENSE[] SEC("license") = "Dual BSD/GPL";

// File Type Macros (from standard headers)
#define S_IFMT 00170000
#define S_IFSOCK 0140000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#define FS_ACCESS 0x00000001 /* File was accessed */
#define FS_MODIFY 0x00000002 /* File was modified */
#define FS_ATTRIB 0x00000004 /* Metadata changed */

/*
 * Attribute flags.  These should be or-ed together to figure out what
 * has been changed!
 * NOTE: This are copied from fs.h, careful before updating.
 */
#define CROS_ATTR_MODE (1 << 0)
#define CROS_ATTR_UID (1 << 1)
#define CROS_ATTR_GID (1 << 2)
#define CROS_ATTR_SIZE (1 << 3)
#define CROS_ATTR_ATIME (1 << 4)
#define CROS_ATTR_MTIME (1 << 5)
#define CROS_ATTR_CTIME (1 << 6)
#define CROS_ATTR_ATIME_SET (1 << 7)
#define CROS_ATTR_MTIME_SET (1 << 8)

// Ring Buffer for Event Storage
struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, CROS_MAX_STRUCT_SIZE * 1024);
} rb SEC(".maps");

// System Flags Map (Shared with other eBPF programs)
struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, 4);
  __type(key, uint32_t);
  __type(value, uint64_t);
  __uint(pinning, LIBBPF_PIN_BY_NAME);  // map will be shared across bpf objs.
} system_flags_shared SEC(".maps");

// Shared Process Info Map (Populated by another eBPF program)
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries,
         65536);  // up to 2^16 task info for processes can be stored.
  __type(key, pid_t);
  __type(value, struct cros_process_start);
  __uint(map_flags, BPF_F_NO_PREALLOC);
  __uint(pinning, LIBBPF_PIN_BY_NAME);  // map will be shared across bpf objs.
} shared_process_info SEC(".maps");

// Allowlist Maps

/**
 * BPF map for storing device file monitoring allowlisting settings.
 *
 * This BPF map uses a hash LRU structure to associate device IDs (dev_t) with
 * their corresponding file monitoring settings (struct
 * device_file_monitoring_settings). It is used to manage and efficiently look
 * up which devices are allowlisted for file monitoring and the specific
 * monitoring mode they require.
 */
struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, MAX_ALLOWLISTED_FILE_MOD_DEVICES);
  __type(key, dev_t);  // Key type: device ID (dev_t).
  __uint(key_size, sizeof(dev_t));
  __type(value, struct device_file_monitoring_settings);
  __uint(pinning, LIBBPF_PIN_BY_NAME);
} device_file_monitoring_allowlist SEC(".maps");

/**
 * BPF map for storing allowlisted hardlinked inodes and their monitoring modes.
 *
 * This BPF map uses an LRU hash structure to associate inode and device ID
 * pairs (represented by struct inode_dev_map_key) with their corresponding
 * monitoring mode (represented by enum file_monitoring_mode). It manages inodes
 * that are allowlisted but hardlinked from non-monitored paths and determines
 * the type of monitoring they require. This map is populated during program
 * initialization with all hard links found in the monitored directories, and it
 * is updated dynamically as new hard links are created or deleted within those
 * directories.
 */
struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, MAX_ALLOWLISTED_HARDLINKED_INODES);
  __type(key, struct inode_dev_map_key);  // Key structure for inode and device
                                          // ID pairs.
  __uint(key_size, sizeof(struct inode_dev_map_key));
  __type(value, enum file_monitoring_mode);
} allowlisted_hardlinked_inodes SEC(".maps");

/**
 * BPF map for storing allowlisted directory inodes and their monitoring modes.
 *
 * This BPF map uses a hash structure to associate inode and device ID pairs
 * (represented by struct inode_dev_map_key) with their corresponding monitoring
 * mode (represented by enum file_monitoring_mode). It manages which directories
 * are allowlisted and determines the type of monitoring they require. This map
 * is dynamically updated as new locations are mounted or unmounted.
 */
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, MAX_ALLOWLISTED_DIRECTORY_INODES);
  __type(key, struct inode_dev_map_key);  // Key structure for inode and device
                                          // ID pairs.
  __uint(key_size, sizeof(struct inode_dev_map_key));
  __type(value, enum file_monitoring_mode);
} allowlisted_directory_inodes SEC(".maps");

/**
 * Before Attributes Map.
 *
 * This BPF map is an LRU (Least Recently Used) per-CPU hash map that stores
 * the 'before' attributes of an inode. The key is a 64-bit value (pid_tgid) and
 * the value is a structure containing inode attributes (struct inode_attr).
 * The map can hold up to 8 entries and uses LRU eviction policy to manage
 * entries.
 */
struct {
  __uint(type,
         BPF_MAP_TYPE_LRU_PERCPU_HASH);  // Use LRU hash map for storing data
  __uint(max_entries, 8);                // Maximum number of entries in the map
  __type(key, uint64_t);                 // Key type (pid_tgid)
  __type(value, struct inode_attr);      // Value type (inode attributes)
} before_attr_map SEC(".maps");

/**
 * Looks up a flag value in the shared BPF map by its unique identifier.
 *
 * Return A pointer to the uint64_t value associated with the flag,
 *         or NULL if the flag is not found in the map.
 */
static __always_inline uint64_t* lookup_flag_value(uint32_t flag_name) {
  // Lookup the value in the BPF map
  return bpf_map_lookup_elem(&system_flags_shared, &flag_name);
}

/**
 * Lookup function to retrieve the monitoring mode for a specific inode
 * and device combination from the BPF LRU hash map.
 *
 * This function performs a lookup in the BPF LRU hash map
 * `allowlisted_hardlinked_inodes` using the provided `device_id` and `inode_id`
 * as the key. It returns a pointer to the monitoring mode (`enum
 * file_monitoring_mode`) if found, or NULL if the entry does not exist in the
 * map.
 *
 * Return Pointer to the monitoring mode if found, or NULL if not found.
 */
static __always_inline enum file_monitoring_mode*
bpf_map_lookup_allowlisted_hardlinked_inodes(dev_t device_id, ino_t inode_id) {
  struct inode_dev_map_key key;
  __builtin_memset(&key, 0, sizeof(key));
  key.inode_id = inode_id;
  key.dev_id = device_id;
  // Perform the map lookup
  return bpf_map_lookup_elem(&allowlisted_hardlinked_inodes, &key);
}

/**
 * Helper function to update the BPF LRU hash map with inode monitoring mode.
 *
 * This function updates the BPF LRU hash map `allowlisted_hardlinked_inodes`
 * with the monitoring mode (`monitoring_mode`) for the specified `inode_id` and
 * `dev_id`.
 *
 * Return 0 on success, or a negative error code on failure.
 */
static __always_inline int bpf_map_update_allowlisted_hardlinked_inodes(
    ino_t inode_id, dev_t dev_id, enum file_monitoring_mode monitoring_mode) {
  struct inode_dev_map_key key;
  __builtin_memset(&key, 0, sizeof(key));
  key.inode_id = inode_id;
  key.dev_id = dev_id;
  return bpf_map_update_elem(&allowlisted_hardlinked_inodes, &key,
                             &monitoring_mode, 0);
}

/**
 * Deletes an entry from the BPF map `allowlisted_hardlinked_inodes`.
 *
 * This function deletes the entry corresponding to the given `inode_id` and
 * `dev_id` from the BPF map `allowlisted_hardlinked_inodes`.
 *
 * Return 0 on success, or a negative error code on failure.
 */
static __always_inline int bpf_map_delete_allowlisted_hardlinked_inodes(
    ino_t inode_id, dev_t dev_id) {
  struct inode_dev_map_key key;
  __builtin_memset(&key, 0, sizeof(key));
  key.inode_id = inode_id;
  key.dev_id = dev_id;

  return bpf_map_delete_elem(&allowlisted_hardlinked_inodes, &key);
}

/**
 * Lookup function to retrieve the monitoring mode for a specific directory
 * inode and device combination from the BPF hash map.
 *
 * This function performs a lookup in the BPF hash map
 * `allowlisted_directory_inodes` using the provided `device_id` and `inode_id`
 * as the key. It returns a pointer to the monitoring mode (`enum
 * file_monitoring_mode`) if found, or NULL if the entry does not exist in the
 * map.
 * Return Pointer to the monitoring mode if found, or NULL if not found.
 */
static __always_inline enum file_monitoring_mode*
bpf_map_lookup_allowlisted_directory_inodes(dev_t device_id, ino_t inode_id) {
  struct inode_dev_map_key key;
  __builtin_memset(&key, 0, sizeof(key));
  key.inode_id = inode_id;
  key.dev_id = device_id;
  // Perform the map lookup
  return bpf_map_lookup_elem(&allowlisted_directory_inodes, &key);
}

/**
 * Helper function to retrieve device file monitoring settings for a given
 * device ID.
 *
 * This function checks if the specified device ID is allowlisted for file
 * monitoring and retrieves its associated monitoring settings if found.
 *
 */
static __always_inline struct device_file_monitoring_settings*
get_device_allowlist_settings(dev_t dev_id) {
  return bpf_map_lookup_elem(&device_file_monitoring_allowlist, &dev_id);
}

/**
 * Determines if a file is considered "valid" for monitoring based on its type
 * and flags.
 *
 * This function checks if the file is:
 *   1. A regular file (not a directory, character device, block device, FIFO,
 * or socket).
 *   2. Not opened with the O_TMPFILE flag (which indicates an unnamed temporary
 * file).
 */
static __always_inline bool is_valid_file(struct inode* file_inode,
                                          uint32_t flags) {
  umode_t mode = BPF_CORE_READ(file_inode, i_mode);
  // Check if the file type is not a regular file (e.g., directory, device,
  // FIFO, or socket)
  if (S_ISDIR(mode) || S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) ||
      S_ISSOCK(mode)) {
    return false;
  }

  // Check if the file was opened with the O_TMPFILE flag (for unnamed
  // temporary files)
  uint64_t* o_tmpfile = lookup_flag_value(O_TMPFILE_FLAG_KEY);

  if (o_tmpfile) {
    uint64_t value = *o_tmpfile;
    if ((flags & value) > 0) {
      return false;
    }
  }

  return true;
}

/**
 * Determines if a file monitoring mode allows a specific file operation type.
 *
 * This function checks if the specified `file_monitoring_mode` allows the given
 * `fmod_type` file operation.
 *
 * Conditions checked:
 * - Allows both read-only and read-write operations (READ_AND_READ_WRITE_BOTH).
 * - Allows read-write operation when in READ_WRITE_ONLY mode and `fmod_type` is
 * FMOD_READ_WRITE_OPEN.
 * - Allows link operations (`fmod_type` is FMOD_LINK).
 *
 */
static __always_inline bool allows_file_operation(
    enum file_monitoring_mode file_monitoring_mode,
    enum filemod_type fmod_type) {
  return (file_monitoring_mode == READ_AND_READ_WRITE_BOTH ||
          (file_monitoring_mode == READ_WRITE_ONLY &&
           fmod_type == FMOD_READ_WRITE_OPEN) ||
          fmod_type == FMOD_LINK || fmod_type == FMOD_ATTR);
}

/**
 * Helper function to check if the current inode is allowlisted and matches the
 * required file operation mode.
 *
 */
static __always_inline bool check_inode_allowlisted(
    struct dentry* file_dentry,
    dev_t dev_id,
    enum filemod_type fmod_type,
    enum file_monitoring_mode* monitoring_mode,
    bool is_directory) {
  // Read the inode number of the file
  ino_t current_ino = BPF_CORE_READ(file_dentry, d_inode, i_ino);

  // Look up monitoring mode for the current inode in the appropriate map
  enum file_monitoring_mode* mode_ptr;

  if (is_directory) {
    mode_ptr = bpf_map_lookup_allowlisted_directory_inodes(dev_id, current_ino);
  } else {
    mode_ptr =
        bpf_map_lookup_allowlisted_hardlinked_inodes(dev_id, current_ino);
  }

  // If monitoring mode not found, return false
  if (mode_ptr == NULL) {
    return false;
  }

  // If monitoring mode found and monitoring_mode pointer is provided
  if (monitoring_mode != NULL) {
    *monitoring_mode =
        *mode_ptr;  // Update the value pointed to by monitoring_mode
  }

  // Check if the retrieved monitoring mode allows the specified file operation
  if (allows_file_operation(*mode_ptr, fmod_type)) {
    return true;  // Inode is allowlisted and mode matches
  } else {
    return false;  // Inode is not allowlisted or mode does not match
  }
}

/**
 * Checks if any ancestor directory of current file is allowlisted for the
 * specified access mode.
 *
 * This function traverses the file's path upwards (towards the root directory)
 * for a maximum of MAX_PATH_DEPTH levels. It checks if any of the encountered
 * directories (including the file itself) are in the appropriate allowlist map
 * and have a matching `file_monitoring_mode`.
 *
 */
static __always_inline bool check_ancestor(
    struct dentry* file_dentry,
    enum filemod_type fmod_type,
    dev_t dev_id,
    enum file_monitoring_mode* monitoring_mode) {
  struct dentry* parent_dentry;

  // Iterate up the directory hierarchy
  for (int i = 0; i < MAX_PATH_DEPTH; i++) {
    // Check if the current directory inode is allowlisted and matches the
    // required mode
    if (check_inode_allowlisted(file_dentry, dev_id, fmod_type, monitoring_mode,
                                true)) {
      return true;  // Allowlisted ancestor found
    }
    parent_dentry = BPF_CORE_READ(file_dentry, d_parent);

    // Check if the current directory entry (file_dentry) is the same as the
    // parent directory entry. This is the root of the path, so break
    if (file_dentry == parent_dentry) {
      break;
    }

    file_dentry = parent_dentry;
  }

  return false;  // No allowlisted ancestor found
}

/**
 * Determines if a file (represented by its dentry) is allowlisted for
 * the specified file operation and device.
 *
 * This function checks if the file:
 *   1. Resides on a device that is allowlisted.
 *   2. Is directly allowlisted, or if any of its ancestor directories are
 *      allowlisted, with the correct `file_monitoring_mode` for the given
 *      `fmod_type`.
 *
 */
static __always_inline bool is_dentry_allowlisted(
    struct dentry* file_dentry,
    dev_t dev_id,
    enum filemod_type fmod_type,
    enum file_monitoring_mode* monitoring_mode) {
  // Check if the device is allowlisted
  struct device_file_monitoring_settings* device_settings =
      get_device_allowlist_settings(dev_id);

  // Check if device is not allowlisted
  if (device_settings == NULL) {
    return false;
  }

  // Check if monitoring all files on the device is enabled
  if (device_settings->device_monitoring_type == MONITOR_ALL_FILES) {
    // Check if the device allows the specified file operation
    if (allows_file_operation(device_settings->file_monitoring_mode,
                              fmod_type)) {
      // Update monitoring_mode if provided
      if (monitoring_mode != NULL) {
        *monitoring_mode = device_settings->file_monitoring_mode;
      }
      return true;  // Device is allowlisted for the specified file operation
    } else {
      return false;  // Device is not allowlisted for this file operation
    }
  }

  // Check if the current file or any ancestor directory is allowlisted
  if (check_inode_allowlisted(file_dentry, dev_id, fmod_type, monitoring_mode,
                              false)) {
    return true;  // File or ancestor is allowlisted
  }

  // Check if any ancestor directory is allowlisted
  if (check_ancestor(file_dentry, fmod_type, dev_id, monitoring_mode)) {
    return true;  // Ancestor directory is allowlisted
  }

  return false;  // File or ancestor is not allowlisted
}

/**
 * Populate inode_attr structure with attributes from a given inode.
 *
 * This function reads various attributes from the inode structure and populates
 * the corresponding fields in the inode_attr structure. The attributes include
 * mode, UID, GID, size, and timestamps (access, modification, and change
 * times).
 */
static __always_inline void get_inode_attributes(struct inode* inode,
                                                 struct inode_attr* attr) {
  // Check if either inode or attr is NULL
  if (!inode || !attr)
    return;

  // Read mode, UID, GID, and size
  attr->mode = BPF_CORE_READ(inode, i_mode);
  attr->uid = BPF_CORE_READ(inode, i_uid.val);
  attr->gid = BPF_CORE_READ(inode, i_gid.val);
  attr->size = BPF_CORE_READ(inode, i_size);

  struct timespec64 ts;

  // Starting with Linux kernel v6.7-rc1, commit 12cd4402365 ("fs: rename
  // inode i_atime and i_mtime fields"), the field name changed.

  // Read access time
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
  ts = BPF_CORE_READ(inode, __i_atime);
#else
  ts = BPF_CORE_READ(inode, i_atime);
#endif

  attr->atime.tv_sec = ts.tv_sec;
  attr->atime.tv_nsec = ts.tv_nsec;

  // Read modification time
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
  ts = BPF_CORE_READ(inode, __i_mtime);
#else
  ts = BPF_CORE_READ(inode, i_mtime);
#endif

  attr->mtime.tv_sec = ts.tv_sec;
  attr->mtime.tv_nsec = ts.tv_nsec;

// Starts from Linux kernel v6.6-rc1, commit 13bc24457850 ("fs: rename
// i_ctime field to __i_ctime"), the field name changed.
// Read change time
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
  ts = BPF_CORE_READ(inode, __i_ctime);
#else
  ts = BPF_CORE_READ(inode, i_ctime);
#endif
  attr->ctime.tv_sec = ts.tv_sec;
  attr->ctime.tv_nsec = ts.tv_nsec;
}

/**
 * Read the name of the current dentry and store it in the path_info structure.
 *
 * This function extracts the name of the current dentry and stores it in the
 * path_segment_info structure at the specified index. It also logs the segment
 * name and length using bpf_printk.
 */
static __always_inline int read_dentry_name(struct file_path_info* path_info,
                                            struct dentry* current_dentry,
                                            int index) {
  struct qstr dentry_name;
  uint32_t segment_length;

  // Read the dentry name
  dentry_name = BPF_CORE_READ(current_dentry, d_name);
  if (!dentry_name.name) {
    bpf_printk("Error: dentry name is NULL at depth %d", index);
    return -1;
  }

  // Read the segment name into the segments array
  segment_length = bpf_probe_read_str(path_info->segment_names[index],
                                      MAX_PATH_SEGMENT_SIZE, dentry_name.name);
  if (segment_length <= 0) {
    bpf_printk("Error: Failed to read segment at depth %d", index);
    return -1;
  }

  // Ensure segment length is within bounds
  if (segment_length > MAX_PATH_SEGMENT_SIZE - 1) {
    segment_length = MAX_PATH_SEGMENT_SIZE - 1;
  }

  // Store the segment length and increment the segment count
  path_info->segment_lengths[index] = segment_length;
  path_info->num_segments++;

  return 0;
}

/**
 * Populate the absolute file path segments from a given dentry.
 *
 * This function traverses the directory tree from the given dentry to the root,
 * populating the path_segment_info structure with the names of each directory
 * in the path. It stops when it reaches the root directory or a circular
 * reference.
 */
static __noinline int construct_absolute_file_path(
    struct dentry* current_dentry, struct file_path_info* path_info) {
  struct dentry* parent_dentry;

  // Check if path_info is NULL
  if (!path_info) {
    bpf_printk("Error: path_info is NULL");
    return -1;
  }

  // Initialize the segment count to 0
  path_info->num_segments = 0;

  // Traverse up the dentry tree, reading segments
  for (int i = 0; i < MAX_PATH_DEPTH && current_dentry; i++) {
    // Read the current dentry name and store it
    if (read_dentry_name(path_info, current_dentry, i) < 0) {
      bpf_printk("Error: Failed to read dentry name at depth %d", i);
      break;
    }

    // Move to the parent dentry
    parent_dentry = BPF_CORE_READ(current_dentry, d_parent);
    if (current_dentry != parent_dentry) {
      current_dentry = parent_dentry;
    } else {
      // If current dentry is the same as parent, stop (root dentry or circular
      // reference)
      break;
    }
  }
}

/**
 * Print the file path information.
 *
 * This function logs the complete file path stored in the file_path_info
 * structure.
 *
 *      - path_info Pointer to the file_path_info structure to be printed.
 */
static inline void print_file_path_info(
    const struct file_path_info* path_info) {
  // Check if path_info is NULL
  if (!path_info)
    return;

  // Print the segment count
  bpf_printk("Segment count: %d", path_info->num_segments);

  // Iterate through each segment and print its size and content
  for (size_t i = 0; i < path_info->num_segments && i < MAX_PATH_DEPTH; i++) {
    // Print the segment and its size
    bpf_printk("Segment %d: %s (size=%d)", i, path_info->segment_names[i],
               path_info->segment_lengths[i]);
  }
}

/**
 * Fills a cros_file_image structure with information about a file from its file
 * descriptor.
 *
 * This function extracts the following information from the kernel data
 * structures:
 *   - inode number
 *   - file mode (permissions)
 *   - device ID
 *   - file open flags
 *   - mount namespace ID
 */
static inline __attribute__((always_inline)) void fill_file_image_info(
    struct cros_file_image* image_info,
    struct file* file,
    struct dentry* dentry,
    struct inode_attr* before_attr) {
  if (!image_info || !dentry) {
    return;
  }

  // Read the inode from the dentry
  struct inode* inode = BPF_CORE_READ(dentry, d_inode);

  // Populate the absolute file path
  construct_absolute_file_path(dentry, &image_info->path_info);

  // Fill inode information
  image_info->inode = BPF_CORE_READ(inode, i_ino);
  image_info->device_id = BPF_CORE_READ(inode, i_sb, s_dev);

  // Fill file flags if the file is not NULL
  if (file != NULL) {
    image_info->flags = BPF_CORE_READ(file, f_flags);
  }

  // Fill mount namespace ID
  image_info->mnt_ns = BPF_CORE_READ(dentry, d_sb, s_user_ns, ns.inum);

  // If before_attr is provided, fill the before attributes
  if (before_attr != NULL) {
    image_info->before_attr.mode = before_attr->mode;
    image_info->before_attr.uid = before_attr->uid;
    image_info->before_attr.gid = before_attr->gid;
    image_info->before_attr.size = before_attr->size;
    image_info->before_attr.atime = before_attr->atime;
    image_info->before_attr.mtime = before_attr->mtime;
    image_info->before_attr.ctime = before_attr->ctime;
  }

  // Get the inode attributes for the after modification state
  get_inode_attributes(inode, &image_info->after_attr);
}

/**
 * Fills a cros_namespace_info structure with namespace information from a
 * task_struct.
 *
 * This function extracts the following namespace IDs (inum) from the
 * task_struct:
 *   - PID namespace
 *   - Mount namespace
 *   - Cgroup namespace
 *   - IPC namespace
 *   - Network namespace
 *   - User namespace
 *   - UTS namespace
 */
static inline __attribute__((always_inline)) void fill_ns_info(
    struct cros_namespace_info* ns_info, struct task_struct* t) {
  // Fill PID namespace ID
  ns_info->pid_ns = BPF_CORE_READ(t, nsproxy, pid_ns_for_children, ns.inum);
  // Fill Mount namespace ID
  ns_info->mnt_ns = BPF_CORE_READ(t, nsproxy, mnt_ns, ns.inum);
  // Fill Cgroup namespace ID
  ns_info->cgroup_ns = BPF_CORE_READ(t, nsproxy, cgroup_ns, ns.inum);
  // Fill IPC namespace ID
  ns_info->ipc_ns = BPF_CORE_READ(t, nsproxy, ipc_ns, ns.inum);
  // Fill Network namespace ID
  ns_info->net_ns = BPF_CORE_READ(t, nsproxy, net_ns, ns.inum);
  // Fill User namespace ID
  ns_info->user_ns = BPF_CORE_READ(t, nsproxy, uts_ns, user_ns, ns.inum);
  // Fill UTS namespace ID
  ns_info->uts_ns = BPF_CORE_READ(t, nsproxy, uts_ns, ns.inum);
}

/**
 * Fills a cros_process_start structure with information about a process.
 *
 * This function attempts to retrieve the process information from the
 * `shared_process_info` map, which is presumably populated by another eBPF
 * program during process execution. If the information is not found in the map,
 * it fills the task information directly from the provided `task_struct`.
 *
 * Return true if the process information was found in the shared map, false
 * otherwise.
 */
static inline __attribute__((always_inline)) bool fill_process_start(
    struct cros_process_start* process_start, struct task_struct* t) {
  // Get the PID from the task_struct
  pid_t pid = BPF_CORE_READ(t, tgid);
  // Attempt to retrieve process information from the shared map
  struct cros_process_start* process_start_from_exec =
      bpf_map_lookup_elem(&shared_process_info, &pid);
  if (process_start_from_exec != NULL) {
    // Copy task information from the shared map
    __builtin_memcpy(&process_start->task_info,
                     &process_start_from_exec->task_info,
                     sizeof(process_start->task_info));

    // Copy image information from the shared map
    __builtin_memcpy(&process_start->image_info,
                     &process_start_from_exec->image_info,
                     sizeof(process_start->image_info));

    // Copy namespace information from the shared map
    __builtin_memcpy(&process_start->spawn_namespace,
                     &process_start_from_exec->spawn_namespace,
                     sizeof(process_start->spawn_namespace));
    return true;
  }

  // Fill task information directly from the task_struct
  cros_fill_task_info(&process_start->task_info, t);
  return false;
}

/**
 * Prints the properties of a mount_data structure.
 *
 * This function prints the source device length and path, destination path
 * information, mount flags, and mount type of the given mount_data structure.
 *
 */
static inline __attribute__((always_inline)) void print_mount_data(
    const struct mount_data* data) {
  // Check for null pointer
  if (!data) {
    bpf_printk("Error: mount_data is NULL");
    return;
  }

  // Print the source device length and path
  bpf_printk("Source Device Length: %u", data->src_device_length);
  bpf_printk("Source Device Path: %s", data->src_device_path);

  // Print the destination path information using the existing helper function
  bpf_printk("Destination Path Info:");
  print_file_path_info(&data->dest_path_info);

  // Print the mount flags
  bpf_printk("Mount Flags: %lu", data->mount_flags);

  // Print the mount type
  bpf_printk("Mount Type: %s", data->mount_type);
}

/**
 * Prints inode attributes with a given prefix.
 *
 */
static inline __attribute__((always_inline)) void print_inode_attributes(
    struct inode_attr* attr, const char* prefix) {
  bpf_printk("%s Mode: %u\n", prefix, attr->mode);
  bpf_printk("%s UID: %u\n", prefix, attr->uid);
  bpf_printk("%s GID: %u\n", prefix, attr->gid);
  bpf_printk("%s Size: %llu\n", prefix, attr->size);
  bpf_printk("%s Access Time: %llu.%09lu\n", prefix, attr->atime.tv_sec,
             attr->atime.tv_nsec);
  bpf_printk("%s Modification Time: %llu.%09lu\n", prefix, attr->mtime.tv_sec,
             attr->mtime.tv_nsec);
  bpf_printk("%s Change Time: %llu.%09lu\n", prefix, attr->ctime.tv_sec,
             attr->ctime.tv_nsec);
}

/**
 * Prints the fields of a cros_file_image structure.
 *
 */
static inline __attribute__((always_inline)) void print_cros_file_image(
    struct cros_file_image* file_image) {
  print_file_path_info(&file_image->path_info);
  bpf_printk("Mnt_ns: %llu\n", file_image->mnt_ns);
  bpf_printk("Device ID: %llu\n", file_image->device_id);
  bpf_printk("Inode: %llu\n", file_image->inode);
  bpf_printk("Flags: %u\n", file_image->flags);
  print_inode_attributes(&file_image->before_attr, "Before");
  print_inode_attributes(&file_image->after_attr, "After");
}

// Function to print fields of cros_namespace_info
static inline __attribute__((always_inline)) int print_cros_namespace_info(
    struct cros_namespace_info* namespace_info) {
  bpf_printk("Cgroup NS: %llu\n", namespace_info->cgroup_ns);
  bpf_printk("PID NS: %llu\n", namespace_info->pid_ns);
  bpf_printk("User NS: %llu\n", namespace_info->user_ns);
  bpf_printk("UTS NS: %llu\n", namespace_info->uts_ns);
  bpf_printk("MNT NS: %llu\n", namespace_info->mnt_ns);
  bpf_printk("NET NS: %llu\n", namespace_info->net_ns);
  bpf_printk("IPC NS: %llu\n", namespace_info->ipc_ns);
  return 0;
}

// Function to print fields of cros_process_task_info
static inline __attribute__((always_inline)) int print_cros_process_task_info(
    struct cros_process_task_info* task_info) {
  bpf_printk("PID: %u\n", task_info->pid);
  bpf_printk("PPID: %u\n", task_info->ppid);
  bpf_printk("Start Time: %llu\n", task_info->start_time);
  bpf_printk("Parent Start Time: %llu\n", task_info->parent_start_time);
  bpf_printk("Command Line: %s\n", task_info->commandline);
  bpf_printk("Command Line Length: %u\n", task_info->commandline_len);
  bpf_printk("UID: %u\n", task_info->uid);
  bpf_printk("GID: %u\n", task_info->gid);
  bpf_printk("Real Command Line Length: %u\n", task_info->real_commandline_len);
  return 0;
}

// Function to print fields of cros_image_info
static inline __attribute__((always_inline)) int print_cros_image_info(
    struct cros_image_info* image_info) {
  bpf_printk("Pathname: %s\n", image_info->pathname);
  bpf_printk("Mnt_ns: %llu\n", image_info->mnt_ns);
  bpf_printk("Inode Device ID: %u\n", image_info->inode_device_id);
  bpf_printk("Inode: %u\n", image_info->inode);
  bpf_printk("UID: %u\n", image_info->uid);
  bpf_printk("GID: %u\n", image_info->gid);
  bpf_printk("PID for setns: %u\n", image_info->pid_for_setns);
  bpf_printk("Mode: %u\n", image_info->mode);
  return 0;
}

/**
 * Prints information about a process start event.
 *
 * This function logs the following information using bpf_printk:
 *   - Process task information (from cros_process_task_info)
 *   - Process image information (from cros_image_info)
 *   - Process spawn namespace information (from cros_namespace_info)
 *
 * Return 0 on success, a negative error code if any of the print functions
 * fail.
 */
static inline __attribute__((always_inline)) int print_cros_process_start(
    struct cros_process_start* process_start) {
  print_cros_process_task_info(&(process_start->task_info));
  print_cros_image_info(&(process_start->image_info));
  print_cros_namespace_info(&(process_start->spawn_namespace));
  return 0;
}

/**
 * Prints the fields of a cros_file_detailed_event structure.
 *
 * Return 0 on success, a negative error code if any step fails.
 */
static inline __attribute__((always_inline)) int print_cros_file_close_event(
    struct cros_file_detailed_event* file_detailed_event) {
  if (!file_detailed_event) {
    return -1;
  }
  print_cros_process_start(&(file_detailed_event->process_info));
  print_cros_file_image(&(file_detailed_event->image_info));
  print_cros_namespace_info(&(file_detailed_event->spawn_namespace));
  bpf_printk("Has Full Process Info: %d\n",
             file_detailed_event->has_full_process_info);
  return 0;
}

/**
 * Prints the fields of a cros_file_event structure.
 *
 */
static inline __attribute__((always_inline)) int print_cros_file_event(
    struct cros_file_event* file_event) {
  if (!file_event) {
    return -1;
  }
  bpf_printk("File Event Type: %d\n", file_event->type);
  bpf_printk("File Modification Type: %d\n", file_event->mod_type);
  if (file_event->mod_type == FMOD_MOUNT ||
      file_event->mod_type == FMOD_UMOUNT) {
    print_mount_data(&(file_event->data.mount_event));
  } else {
    print_cros_file_close_event(&(file_event->data.file_detailed_event));
  }
  return 0;
}

/**
 * Prints the fields of a cros_event structure.
 *
 */
static inline __attribute__((always_inline)) int print_cros_event(
    struct cros_event* event) {
  if (!event) {
    return -1;
  }
  bpf_printk("Event Type: %d\n", event->type);
  if (event->type == kFileEvent) {
    print_cros_file_event(&(event->data.file_event));
  }
  return 0;
}

/**
 * Populates the ring buffer (rb) with file event data.
 *
 * This function does the following:
 *   1. Reserves space in the ring buffer for a cros_event.
 *   2. Fills the cros_event with file modification information.
 *   3. Submits the event to the ring buffer.
 *
 * Return 0 on success, a negative error code if any step fails.
 */
static inline __attribute__((always_inline)) int populate_rb(
    enum filemod_type mod_type,
    enum cros_file_event_type event_type,
    struct file* file,
    struct dentry* dentry,
    struct inode_attr* before_attr) {
  // Get the current task
  struct task_struct* current_task =
      (struct task_struct*)bpf_get_current_task();

  // Reserve space in the ring buffer for the event
  struct cros_event* event =
      (struct cros_event*)(bpf_ringbuf_reserve(&rb, sizeof(*event), 0));
  if (!event) {
    bpf_printk("File Event unable to reserve buffer");
    return -1;
  }

  // Populate the event type and file modification information
  event->type = kFileEvent;
  event->data.file_event.mod_type = mod_type;
  event->data.file_event.type = event_type;

  // Fill file close event data
  struct cros_file_detailed_event* file_detailed_event =
      &(event->data.file_event.data.file_detailed_event);
  file_detailed_event->has_full_process_info =
      fill_process_start(&file_detailed_event->process_info, current_task);
  fill_ns_info(&file_detailed_event->spawn_namespace, current_task);
  fill_file_image_info(&file_detailed_event->image_info, file, dentry,
                       before_attr);

  // Submit the event to the ring buffer
  bpf_ringbuf_submit(event, 0);
  return 0;
}

/**
 * Check if file attributes should be tracked. Copied from fsnotify
 * @ia_valid: Bitmask representing the valid attributes
 *
 * This function checks if any of the relevant file attributes (UID, GID, size,
 * access time, modification time, or mode) are marked as changed in the
 * ia_valid bitmask.
 *
 * Return: true if any relevant attributes are marked as changed, false
 * otherwise
 */
bool inline __attribute__((always_inline))
should_track_file_attribute_change(unsigned int ia_valid) {
  __u32 attribute_mask = 0;

  // Check if UID change should be tracked
  if (ia_valid & CROS_ATTR_UID) {
    attribute_mask |= FS_ATTRIB;
  }

  // Check if GID change should be tracked
  if (ia_valid & CROS_ATTR_GID) {
    attribute_mask |= FS_ATTRIB;
  }

  // Check if size change should be tracked
  if (ia_valid & CROS_ATTR_SIZE) {
    attribute_mask |= FS_MODIFY;
  }

  // Check if access and modification time changes should be tracked (utime
  // call)
  if ((ia_valid & (CROS_ATTR_ATIME | CROS_ATTR_MTIME)) ==
      (CROS_ATTR_ATIME | CROS_ATTR_MTIME)) {
    attribute_mask |= FS_ATTRIB;
  } else if (ia_valid & CROS_ATTR_ATIME) {
    attribute_mask |= FS_ACCESS;
  } else if (ia_valid & CROS_ATTR_MTIME) {
    attribute_mask |= FS_MODIFY;
  }

  // Check if mode change should be tracked
  if (ia_valid & CROS_ATTR_MODE) {
    attribute_mask |= FS_ATTRIB;
  }

  // Return true if any relevant attributes are marked as changed
  return attribute_mask & FS_ATTRIB;
}

/**
 * Check if inode attributes have actually changed.
 * @before_attr: The attributes of the inode before the change
 * @new_iattr: The new attributes to set
 *
 * This function compares the before and after attributes of an inode to
 * determine if any significant changes have occurred.
 *
 * Return: true if any relevant attributes have changed, false otherwise
 */

static inline __attribute__((always_inline)) bool has_inode_attributes_changed(
    const struct inode_attr* before_attr,
    const struct iattr* new_iattr,
    uint32_t ia_valid_flags) {
  if (!before_attr || !new_iattr || ia_valid_flags == 0) {
    return false;
  }

  bool attribute_changed = false;

  // Check UID change
  if (ia_valid_flags & CROS_ATTR_UID) {
    if (before_attr->uid != BPF_CORE_READ(new_iattr, ia_uid.val)) {
      attribute_changed = true;
    }
  }

  // Check GID change
  if (ia_valid_flags & CROS_ATTR_GID) {
    if (before_attr->gid != BPF_CORE_READ(new_iattr, ia_gid.val)) {
      attribute_changed = true;
    }
  }

  struct timespec64 ts_mtime;
  struct timespec64 ts_atime;

  // Read access time
  ts_atime = BPF_CORE_READ(new_iattr, ia_atime);

  // Read modification time
  ts_mtime = BPF_CORE_READ(new_iattr, ia_mtime);

  // Check access and modification time change (utime call)
  if ((ia_valid_flags & (CROS_ATTR_ATIME | CROS_ATTR_MTIME)) ==
      (CROS_ATTR_ATIME | CROS_ATTR_MTIME)) {
    if ((before_attr->atime.tv_sec != ts_atime.tv_sec ||
         before_attr->atime.tv_nsec != ts_atime.tv_nsec) ||
        (before_attr->mtime.tv_sec != ts_mtime.tv_sec ||
         before_attr->mtime.tv_nsec != ts_mtime.tv_nsec)) {
      attribute_changed = true;
    }
  }

  // Check mode change
  if (ia_valid_flags & CROS_ATTR_MODE) {
    if (before_attr->mode != BPF_CORE_READ(new_iattr, ia_mode)) {
      attribute_changed = true;
    }
  }
  // Return true if any relevant attributes have changed
  return should_track_file_attribute_change(ia_valid_flags) &&
         attribute_changed;
}

/**
 * BPF program attached to the fexit of the filp_close() kernel function.
 *
 * return_value: Return value of the filp_close system call.
 */
SEC("fexit/filp_close")
int BPF_PROG(fexit__filp_close,
             struct file* file,
             fl_owner_t owner_id,
             int return_value) {
  struct dentry* file_dentry;
  struct inode* file_inode;

  // Check for successful file close operation
  if (return_value != 0) {
    return 0;
  }

  // Filter out kernel threads
  struct task_struct* t = (struct task_struct*)bpf_get_current_task();
  if (is_kthread(t)) {
    return 0;
  }

  // Retrieve the inode and dentry structures from the file
  file_inode = BPF_CORE_READ(file, f_inode);
  file_dentry = BPF_CORE_READ(file, f_path.dentry);
  uint32_t file_flags = BPF_CORE_READ(file, f_flags);

  // Check for valid file type
  if (!is_valid_file(file_inode, file_flags)) {
    return 0;
  }

  // Determine file modification type based on access mode
  enum filemod_type modification_type = FMOD_READ_WRITE_OPEN;
  uint64_t* accmode_flag = lookup_flag_value(O_ACCMODE_FLAG_KEY);
  uint64_t* rdonly_flag = lookup_flag_value(O_RDONLY_FLAG_KEY);

  if (accmode_flag != NULL && rdonly_flag != NULL) {
    if ((file_flags & *accmode_flag) == *rdonly_flag) {
      modification_type = FMOD_READ_ONLY_OPEN;
    }
  } else {
    // Unable to read flags populated from userspace, not able to determine
    // operation mode Exiting early
    bpf_printk(
        "Unable to read flags populated from userspace, not able to determine "
        "operation mode.");
    return 0;
  }

  // Check if the dentry is on the allowlist
  dev_t device_id = BPF_CORE_READ(file_inode, i_sb, s_dev);
  enum file_monitoring_mode monitoring_mode;
  if (!is_dentry_allowlisted(file_dentry, device_id, modification_type,
                             &monitoring_mode)) {
    return 0;
  }

  // Populate the ring buffer with the event data
  populate_rb(modification_type, kFileCloseEvent, file, file_dentry, NULL);

  return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
#define NOTIFY_CHANGE_ARGS                                            \
  struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *attr, \
      struct inode **delegated_inode
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
#define NOTIFY_CHANGE_ARGS                                  \
  struct user_namespace *mnt_userns, struct dentry *dentry, \
      struct iattr *attr, struct inode **delegated_inode
#else
#define NOTIFY_CHANGE_ARGS \
  struct dentry *dentry, struct iattr *attr, struct inode **delegated_inode
#endif

/**
 * BPF program to capture 'before' attributes on inode setattr
 *
 * This function is triggered on the entry of the notify_change function.
 * It captures the inode attributes before they are changed and stores them
 * in a BPF map using the pid_tgid as the key.
 *
 * Return: 0 on success
 */
SEC("fentry/notify_change")
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
int BPF_PROG(capture_before_inode_setattr,
             struct mnt_idmap* idmap,
             struct dentry* dentry,
             struct iattr* attr,
             struct inode** delegated_inode)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
int BPF_PROG(capture_before_inode_setattr,
             struct user_namespace* mnt_userns,
             struct dentry* dentry,
             struct iattr* attr,
             struct inode** delegated_inode)
#else
int BPF_PROG(capture_before_inode_setattr,
             struct dentry* dentry,
             struct iattr* attr,
             struct inode** delegated_inode)
#endif
{
  struct inode_attr attr_before =
      {};  // Structure to hold inode attributes before change
  struct task_struct* current_task;        // Current task structure
  struct inode* inode;                     // Inode structure
  struct file* file;                       // File structure
  uint32_t file_flags;                     // File flags
  dev_t dev_id;                            // Device ID
  enum file_monitoring_mode monitor_mode;  // File monitoring mode

  if (!attr || !dentry)
    return 0;
  // Retrieve the current task
  current_task = (struct task_struct*)bpf_get_current_task();

  // Filter out kernel threads
  if (is_kthread(current_task)) {
    return 0;
  }

  // Retrieve the file and inode structures
  file = BPF_CORE_READ(attr, ia_file);
  inode = BPF_CORE_READ(dentry, d_inode);
  if (!inode)
    return 0;
  file_flags = BPF_CORE_READ(file, f_flags);

  // Check for a valid file type
  if (!is_valid_file(inode, file_flags)) {
    return 0;
  }

  // Retrieve the device ID
  dev_id = BPF_CORE_READ(inode, i_sb, s_dev);

  // Check if the dentry is on the allowlist
  if (!is_dentry_allowlisted(dentry, dev_id, FMOD_ATTR, &monitor_mode)) {
    return 0;
  }

  // Get the inode attributes before the change
  get_inode_attributes(inode, &attr_before);

  uint32_t ia_valid_flags = BPF_CORE_READ(attr, ia_valid);
  // Check if inode attributes have actually changed
  bool has_changed =
      has_inode_attributes_changed(&attr_before, attr, ia_valid_flags);
  if (!has_changed) {
    return 0;
  }

  // Retrieve the PID and TGID
  uint64_t pid_tgid = bpf_get_current_pid_tgid();
  // Update the BPF map with the 'before' attributes
  bpf_map_update_elem(&before_attr_map, &pid_tgid, &attr_before, BPF_ANY);

  return 0;
}

/**
 * BPF program to capture 'after' attributes on inode setattr exit
 *
 * This function is triggered on the exit of the notify_change function.
 * It captures the inode attributes after they are changed if the operation was
 * successful, checks if processing is needed, and if so, it resets the
 * processing flag and populates the ring buffer with the relevant event data.
 *
 * Return: 0 on success, or appropriate error code.
 */
SEC("fexit/notify_change")
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
int BPF_PROG(capture_after_inode_setattr,
             struct mnt_idmap* idmap,
             struct dentry* dentry,
             struct iattr* attr,
             struct inode** delegated_inode,
             int ret)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
int BPF_PROG(capture_after_inode_setattr,
             struct user_namespace* mnt_userns,
             struct dentry* dentry,
             struct iattr* attr,
             struct inode** delegated_inode,
             int ret)
#else
int BPF_PROG(capture_after_inode_setattr,
             struct dentry* dentry,
             struct iattr* attr,
             struct inode** delegated_inode,
             int ret)
#endif
{
  struct inode* inode;             // Inode structure
  struct inode_attr* attr_before;  // Pointer to 'before' inode attributes
  uint64_t pid_tgid;               // PID and TGID combined

  // Check for successful setattr operation
  if (ret != 0) {
    return 0;
  }

  // Retrieve the PID and TGID
  pid_tgid = bpf_get_current_pid_tgid();
  // Lookup the 'before' attributes from the BPF map
  attr_before = bpf_map_lookup_elem(&before_attr_map, &pid_tgid);
  bpf_map_delete_elem(&before_attr_map, &pid_tgid);
  if (!attr_before) {
    return 0;
  }

  // Populate the ring buffer with the event data
  populate_rb(FMOD_ATTR, kFileAttributeModifyEvent, NULL, dentry, attr_before);

  return 0;
}

/**
 * eBPF program to monitor and potentially allowlist new hard link
 * creations.
 *
 * This function traces the exit of the security_inode_link function and
 * performs checks to determine if the operation involves allowlisted
 * directories and files. If the new link (new_dentry) is not allowlisted, it
 * updates the allowlist map with its inode ID and monitoring mode.
 */
SEC("fexit/security_inode_link")
int BPF_PROG(fexit__security_inode_link,
             struct dentry* old_dentry,
             struct inode* dir,
             struct dentry* new_dentry,
             int ret) {
  // Exit early if the operation failed or if `dir` or `old_dentry` is NULL
  if (ret != 0 || !dir || !old_dentry) {
    return 0;
  }

  // Initialize variables
  struct inode* old_inode = BPF_CORE_READ(old_dentry, d_inode);
  enum file_monitoring_mode monitoring_mode, new_monitoring_mode;

  if (!old_inode) {
    return 0;
  }

  // Read the device ID of the old inode and check allowlist status
  dev_t old_dev_id = BPF_CORE_READ(old_inode, i_sb, s_dev);

  // Check if the old dentry is allowlisted for monitoring
  if (!is_dentry_allowlisted(old_dentry, old_dev_id, FMOD_LINK,
                             &monitoring_mode)) {
    return 0;  // Skip if old dentry is not allowlisted for monitoring
  }

  // Read the device ID of the new parent directory and check allowlist status
  dev_t new_dev_id = BPF_CORE_READ(dir, i_sb, s_dev);

  // Check if the new dentry is allowlisted for monitoring
  if (!is_dentry_allowlisted(new_dentry, new_dev_id, FMOD_LINK,
                             &new_monitoring_mode)) {
    // If new dentry is not allowlisted, update the allowlist map with its
    // inode ID
    ino_t inode_id = BPF_CORE_READ(old_inode, i_ino);
    bpf_map_update_allowlisted_hardlinked_inodes(inode_id, old_dev_id,
                                                 monitoring_mode);
  }

  return 0;
}

/**
 * eBPF program to manage allowlist for hard link unlink operations.
 *
 * This function traces the exit of the security_inode_unlink function and
 * checks if the operation results in the removal of the last hard link to an
 * inode. If the inode's hard link count drops to 1, it identifies the inode
 * and its associated device ID, and removes the inode ID from the allowlist
 * map if present, assuming it was previously allowlisted for monitoring.
 *
 */
SEC("fexit/security_inode_unlink")
int BPF_PROG(fexit__security_inode_unlink,
             struct inode* dir,
             struct dentry* old_dentry,
             int ret) {
  // Exit early if the operation failed or if `old_dentry` is NULL
  if (ret != 0 || !old_dentry) {
    return 0;
  }

  // Initialize variables
  struct inode* old_inode = BPF_CORE_READ(old_dentry, d_inode);

  // If `old_inode` is NULL, return early as we cannot proceed further
  if (!old_inode) {
    return 0;
  }

  // Read the number of hard links for the inode
  unsigned int links_count = (unsigned int)BPF_CORE_READ(old_inode, i_nlink);

  // If this is the last link (nlink == 1), remove the inode ID from the map
  if (links_count == 1) {
    // Read the device ID of the old inode
    dev_t old_dev_id = BPF_CORE_READ(old_inode, i_sb, s_dev);

    // Retrieve the inode ID
    ino_t inode_id = BPF_CORE_READ(old_inode, i_ino);
    bpf_map_delete_allowlisted_hardlinked_inodes(inode_id, old_dev_id);
  }

  // Always return 0 to indicate success
  return 0;
}

/**
 * security_sb_mount() - Check permission for mounting a filesystem
 *
 * Check permission before an object specified by @dev_name is mounted on the
 * mount point named by @nd.  For an ordinary mount, @dev_name identifies a
 * device if the file system type requires a device.  For a remount
 * (@flags & MS_REMOUNT), @dev_name is irrelevant.  For a loopback/bind mount
 * (@flags & MS_BIND), @dev_name identifies the pathname of the object being
 * mounted.
 *
 * Return: Returns 0 if permission is granted.
 */

SEC("fexit/security_sb_mount")
int BPF_PROG(fexit__security_sb_mount,
             const char* dev_name,
             struct path* path,
             const char* mount_type,
             unsigned long flags,
             void* data,
             int ret) {
  // Check if the syscall was successful
  if (ret != 0) {
    return 0;
  }

  // Reserve space in the ring buffer for the event
  struct cros_event* event =
      (struct cros_event*)(bpf_ringbuf_reserve(&rb, sizeof(*event), 0));
  if (!event) {
    // Unable to reserve buffer space
    bpf_printk("Error: Unable to reserve ring buffer space");
    return 0;
  }

  // Populate the event type and file modification information
  event->type = kFileEvent;
  event->data.file_event.mod_type = FMOD_MOUNT;
  event->data.file_event.type = kFileMountEvent;

  // Initialize umount data structure
  struct mount_data* mount_data = &(event->data.file_event.data.mount_event);

  mount_data->mount_flags = flags;

  if (mount_type) {
    const char* type_ptr;
    bpf_core_read(&type_ptr, sizeof(type_ptr), &mount_type);
    int ret_type = bpf_probe_read_kernel_str(
        &mount_data->mount_type, sizeof(mount_data->mount_type), type_ptr);
    if (ret_type < 0) {
      bpf_printk("Error: Failed to read mount type %d", ret_type);
    }
  }

  if (dev_name) {
    const char* dev_name_ptr;
    bpf_core_read(&dev_name_ptr, sizeof(dev_name_ptr), &dev_name);
    mount_data->src_device_length = bpf_probe_read_kernel_str(
        &mount_data->src_device_path, MAX_PATH_SIZE, dev_name_ptr);
    if (mount_data->src_device_length < 0) {
      bpf_printk("Error: Failed to read device name");
    }
  }

  construct_absolute_file_path(BPF_CORE_READ(path, dentry),
                               &mount_data->dest_path_info);
  // Submit the event to the ring buffer
  bpf_ringbuf_submit(event, 0);
  return 0;
}

/**
 * @brief BPF program to handle the exit of the umount syscall.
 *
 * This function captures unmount operations and submits relevant information
 * to the ring buffer for user-space processing. It extracts the destination
 * path information and flags from the syscall parameters.
 */
SEC("fexit/security_sb_umount")
int BPF_PROG(fexit__security_sb_umount,
             struct vfsmount* mnt,
             int flags,
             int ret) {
  // Check if the syscall was successful
  if (ret != 0) {
    return 0;
  }

  // Reserve space in the ring buffer for the event
  struct cros_event* event =
      (struct cros_event*)(bpf_ringbuf_reserve(&rb, sizeof(*event), 0));
  if (!event) {
    // Unable to reserve buffer space
    bpf_printk("Error: Unable to reserve ring buffer space");
    return 0;
  }

  // Populate the event type and file modification information
  event->type = kFileEvent;
  event->data.file_event.mod_type = FMOD_UMOUNT;
  event->data.file_event.type = kFileMountEvent;

  // Initialize umount data structure
  struct mount_data* umount_data = &(event->data.file_event.data.mount_event);
  umount_data->mount_flags = flags;

  // Read the root directory entry (dentry) of the mount
  struct dentry* root_dentry = BPF_CORE_READ(mnt, mnt_root);
  if (root_dentry) {
    // Populate the destination path information
    construct_absolute_file_path(root_dentry, &umount_data->dest_path_info);
  } else {
    bpf_printk("Error: Failed to read root dentry");
  }

  // Submit the event to the ring buffer
  bpf_ringbuf_submit(event, 0);

  return 0;
}

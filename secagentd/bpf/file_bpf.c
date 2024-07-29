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
 * Looks up a flag value in the shared BPF map by its unique identifier.
 *
 * @param flag_name The unique identifier (key) of the flag to look up.
 * @return A pointer to the uint64_t value associated with the flag,
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
 * @param device_id The device ID (st_dev) of the inode.
 * @param inode_id The inode number (st_ino) of the file or directory.
 * @return Pointer to the monitoring mode if found, or NULL if not found.
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
 * @param inode_id The inode number of the file or directory.
 * @param dev_id The device ID of the inode.
 * @param monitoring_mode The monitoring mode to be associated with the inode.
 * @return 0 on success, or a negative error code on failure.
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
 * @param inode_id The inode number of the file or directory to delete from the
 * map.
 * @param dev_id The device ID of the inode to delete from the map.
 * @return 0 on success, or a negative error code on failure.
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
 *
 * @param device_id The device ID (st_dev) of the inode.
 * @param inode_id The inode number (st_ino) of the directory.
 * @return Pointer to the monitoring mode if found, or NULL if not found.
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
 * @param dev_id The device ID to check.
 * @return A pointer to struct device_file_monitoring_settings if the device ID
 * is allowlisted, or NULL if not found in the allowlist.
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
 *
 * @param file_inode A pointer to the inode structure of the file.
 * @param flags The open flags used to access the file.
 * @return true if the file is considered valid, false otherwise.
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
 * @param file_monitoring_mode The file monitoring mode to check.
 * @param fmod_type            The type of file operation (e.g., read-only,
 * read-write, link).
 * @return true if the file monitoring mode allows the file operation type,
 * false otherwise.
 */
static __always_inline bool allows_file_operation(
    enum file_monitoring_mode file_monitoring_mode,
    enum filemod_type fmod_type) {
  return (file_monitoring_mode == READ_AND_READ_WRITE_BOTH ||
          (file_monitoring_mode == READ_WRITE_ONLY &&
           fmod_type == FMOD_READ_WRITE_OPEN) ||
          fmod_type == FMOD_LINK);
}

/**
 * Helper function to check if the current inode is allowlisted and matches the
 * required file operation mode.
 *
 * @param file_dentry     The dentry of the file to check.
 * @param dev_id          The device ID where the file resides.
 * @param fmod_type       The type of file operation (e.g., read-only,
 *                        read-write, hard link).
 * @param[out] monitoring_mode  Pointer to store the file monitoring mode if the
 *                        file or ancestor is allowlisted. Pass NULL if not
 *                        needed.
 * @param is_directory     Boolean indicating if the dentry is monitored for
 *                         directory lookup.
 * @return true if the inode is allowlisted and matches the file operation mode,
 *         false otherwise.
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
 * @param file_dentry Pointer to the dentry of the file whose ancestors need to
 * be checked.
 * @param fmod_type   The type of file modification operation (e.g., read-only,
 *                    read-write, hard link).
 * @param dev_id      The device ID where the file resides.
 * @param monitoring_mode Pointer to store the `file_monitoring_mode` if an
 *                        ancestor is allowlisted.
 * @return true if an allowlisted ancestor with a matching mode is found, false
 *         otherwise.
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
 * @param file_dentry     The dentry of the file to check.
 * @param dev_id          The device ID where the file resides.
 * @param fmod_type       The type of file operation (e.g., read-only,
 *                        read-write, hard link).
 * @param[out] monitoring_mode Pointer to store the `file_monitoring_mode` if
 *                            the file or ancestor is allowlisted. Pass NULL if
 * not needed.
 * @return true if the file or any ancestor directory is allowlisted, false
 *         otherwise.
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
 *
 * @param image_info A pointer to the cros_file_image struct to be filled.
 * @param filp       A pointer to the file struct representing the opened file.
 */
static inline __attribute__((always_inline)) void fill_image_info(
    struct cros_file_image* image_info, struct file* filp) {
  if (image_info == NULL || filp == NULL) {
    return;
  }
  struct dentry* filp_dentry;
  struct inode* file_inode;
  file_inode = BPF_CORE_READ(filp, f_inode);
  filp_dentry = BPF_CORE_READ(filp, f_path.dentry);

  // Populate the absolute file path
  construct_absolute_file_path(filp_dentry, &image_info->path_info);

  image_info->inode = BPF_CORE_READ(file_inode, i_ino);
  image_info->mode = BPF_CORE_READ(file_inode, i_mode);
  image_info->uid = BPF_CORE_READ(file_inode, i_uid.val);
  image_info->gid = BPF_CORE_READ(file_inode, i_gid.val);
  image_info->device_id = BPF_CORE_READ(file_inode, i_sb, s_dev);
  image_info->flags = BPF_CORE_READ(filp, f_flags);
  image_info->mnt_ns = BPF_CORE_READ(filp_dentry, d_sb, s_user_ns, ns.inum);
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
 *
 * @param ns_info A pointer to the cros_namespace_info struct to be filled.
 * @param t        A pointer to the task_struct representing the process.
 */
static inline __attribute__((always_inline)) void fill_ns_info(
    struct cros_namespace_info* ns_info, struct task_struct* t) {
  ns_info->pid_ns = BPF_CORE_READ(t, nsproxy, pid_ns_for_children, ns.inum);
  ns_info->mnt_ns = BPF_CORE_READ(t, nsproxy, mnt_ns, ns.inum);
  ns_info->cgroup_ns = BPF_CORE_READ(t, nsproxy, cgroup_ns, ns.inum);
  ns_info->ipc_ns = BPF_CORE_READ(t, nsproxy, ipc_ns, ns.inum);
  ns_info->net_ns = BPF_CORE_READ(t, nsproxy, net_ns, ns.inum);
  ns_info->user_ns = BPF_CORE_READ(t, nsproxy, uts_ns, user_ns, ns.inum);
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
 * @param process_start A pointer to the cros_process_start structure to be
 * filled.
 * @param t             A pointer to the task_struct representing the process.
 * @return true if the process information was found in the shared map, false
 * otherwise.
 */
static inline __attribute__((always_inline)) bool fill_process_start(
    struct cros_process_start* process_start, struct task_struct* t) {
  pid_t pid = BPF_CORE_READ(t, tgid);
  struct cros_process_start* process_start_from_exec =
      bpf_map_lookup_elem(&shared_process_info, &pid);
  if (process_start_from_exec != NULL) {
    __builtin_memcpy(&process_start->task_info,
                     &process_start_from_exec->task_info,
                     sizeof(process_start->task_info));
    __builtin_memcpy(&process_start->image_info,
                     &process_start_from_exec->image_info,
                     sizeof(process_start->image_info));
    __builtin_memcpy(&process_start->spawn_namespace,
                     &process_start_from_exec->spawn_namespace,
                     sizeof(process_start->spawn_namespace));
    return true;
  }
  cros_fill_task_info(&process_start->task_info, t);
  return false;
}

// Function to print fields of cros_file_image
static inline __attribute__((always_inline)) void print_cros_file_image(
    struct cros_file_image* file_image) {
  print_file_path_info(&file_image->path_info);
  bpf_printk("Mnt_ns: %llu\n", file_image->mnt_ns);
  bpf_printk("Device ID: %llu\n", file_image->device_id);
  bpf_printk("Inode: %llu\n", file_image->inode);
  bpf_printk("Mode: %u\n", file_image->mode);
  bpf_printk("Flags: %u\n", file_image->flags);
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
 * @param process_start A pointer to the cros_process_start structure containing
 * the event data.
 * @return 0 on success, a negative error code if any of the print functions
 * fail.
 */
static inline __attribute__((always_inline)) int print_cros_process_start(
    struct cros_process_start* process_start) {
  print_cros_process_task_info(&(process_start->task_info));
  print_cros_image_info(&(process_start->image_info));
  print_cros_namespace_info(&(process_start->spawn_namespace));
  return 0;
}

// Function to print fields of cros_file_close_event
static inline __attribute__((always_inline)) int print_cros_file_close_event(
    struct cros_file_close_event* file_close) {
  print_cros_process_start(&(file_close->process_info));
  print_cros_file_image(&(file_close->image_info));
  print_cros_namespace_info(&(file_close->spawn_namespace));
  bpf_printk("Has Full Process Info: %d\n", file_close->has_full_process_info);
  return 0;
}

// Function to print fields of cros_file_event
static inline __attribute__((always_inline)) int print_cros_file_event(
    struct cros_file_event* file_event) {
  if (!file_event) {
    return -1;
  }
  bpf_printk("File Event Type: %d\n", file_event->type);
  bpf_printk("File Mod Type: %d\n", file_event->mod_type);
  if (file_event->type == kFileCloseEvent) {
    print_cros_file_close_event(&(file_event->data.file_close));
  }
  return 0;
}

// Function to print fields of cros_event
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
 *   2. Fills the cros_event with file modification information:
 *   3. Submits the event to the ring buffer.
 *
 * @param fmod_type The type of file modification operation.
 * @param cros_file_event_type Event type enum.
 * @param filp       The file pointer associated with the event.
 * @return 0 on success, a negative error code if any step fails.
 */
static inline __attribute__((always_inline)) int populate_rb(
    enum filemod_type fmod_type,
    enum cros_file_event_type type,
    struct file* filp) {
  struct task_struct* task = (struct task_struct*)bpf_get_current_task();
  struct cros_event* event =
      (struct cros_event*)(bpf_ringbuf_reserve(&rb, sizeof(*event), 0));
  if (event == NULL) {
    bpf_printk("flip_close unable to reserve buffer");
    return -1;
  }
  event->type = kFileEvent;

  event->data.file_event.mod_type = fmod_type;
  event->data.file_event.type = type;

  struct cros_file_close_event* fc = &(event->data.file_event.data.file_close);

  fc->has_full_process_info = fill_process_start(&fc->process_info, task);

  fill_ns_info(&fc->spawn_namespace, task);
  fill_image_info(&fc->image_info, filp);

  bpf_ringbuf_submit(event, 0);
  return 0;
}

/**
 * BPF program attached to the fexit of the filp_close() kernel function.
 *
 *
 * @param filp  A pointer to the file structure being closed.
 * @param id    The file descriptor lease ID (not used here).
 * @param ret   The return value of the filp_close system call.
 * @return 0 on success (always returns 0 as it's an fexit program).
 */
SEC("fexit/filp_close")
int BPF_PROG(fexit__filp_close, struct file* filp, fl_owner_t id, int ret) {
  struct dentry* filp_dentry;
  struct inode* file_inode;

  // 1. Check for Successful File Close
  if (ret != 0) {
    // unsuccessful filp_close operation
    return 0;
  }

  // 2. Filter Out Kernel Threads
  struct task_struct* t = (struct task_struct*)bpf_get_current_task();
  if (is_kthread(t)) {
    return 0;
  }

  // 3. Check for Valid File Type
  file_inode = BPF_CORE_READ(filp, f_inode);
  filp_dentry = BPF_CORE_READ(filp, f_path.dentry);
  uint32_t flags = BPF_CORE_READ(filp, f_flags);
  if (!is_valid_file(file_inode, flags)) {
    return 0;
  }

  // 4. Determine File Modification Type
  enum filemod_type fmod_type = FMOD_LINK;
  uint64_t* o_accmode = lookup_flag_value(O_ACCMODE_FLAG_KEY);
  uint64_t* o_rdonly = lookup_flag_value(O_RDONLY_FLAG_KEY);

  // Determine if the file was opened in read-only or read-write mode
  fmod_type = FMOD_READ_WRITE_OPEN;
  if (o_accmode != NULL && o_rdonly != NULL) {
    if ((flags & *o_accmode) == *o_rdonly) {
      fmod_type = FMOD_READ_ONLY_OPEN;
    }
  } else {
    // Unable to read flags populated from userspace, not able to determine
    // operation mode Exiting early
    bpf_printk(
        "Unable to read flags populated from userspace, not able to determine "
        "operation mode.");
    return 0;
  }

  // 5. Check Allowlist
  dev_t device_id = BPF_CORE_READ(file_inode, i_sb, s_dev);
  enum file_monitoring_mode monitoring_mode;
  if (!is_dentry_allowlisted(filp_dentry, device_id, fmod_type,
                             &monitoring_mode)) {
    return 0;
  }

  // 6. Populate Ring Buffer (if allowed)
  populate_rb(fmod_type, kFileCloseEvent, filp);
  return 0;
}

/**
 * BPF program attached to the fexit of the security_inode_setattr() kernel
 * function.
 *
 * @param mnt_userns The mount user namespace of the process (not used in this
 * version).
 * @param dentry     A pointer to the dentry of the file being modified.
 * @param attr       A pointer to the iattr struct containing the new
 * attributes.
 * @param ret        The return value of the security_inode_setattr system call.
 * @return 0 on success (always returns 0 as it's an fexit program).
 */
SEC("fexit/security_inode_setattr")
// TODO(princya): Handle different kernel version function signature
// TODO(princya): Need to capture both before and after attribute
int BPF_PROG(fexit__security_inode_setattr,
             struct user_namespace* mnt_userns,
             struct dentry* dentry,
             struct iattr* attr,
             int ret) {
  // 1. Check for Successful Setattr Operation
  if (ret != 0) {
    return 0;
  }

  // 2. Filter Out Kernel Threads
  struct task_struct* t = (struct task_struct*)bpf_get_current_task();
  if (is_kthread(t)) {
    return 0;
  }

  struct inode* file_inode = BPF_CORE_READ(dentry, d_inode);

  struct file* filp = BPF_CORE_READ(attr, ia_file);
  uint32_t flags = BPF_CORE_READ(filp, f_flags);

  // 3. Check for Valid File Type
  if (!is_valid_file(file_inode, flags)) {
    return 0;
  }

  // 4. Check Allowlist
  dev_t dev_id = BPF_CORE_READ(file_inode, i_sb, s_dev);
  enum file_monitoring_mode monitoring_mode;
  if (!is_dentry_allowlisted(dentry, dev_id, FMOD_ATTR, &monitoring_mode)) {
    return 0;
  }

  // 5. Populate Ring Buffer
  populate_rb(FMOD_ATTR, kFileAttributeModifyEvent, filp);
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
 *
 * @param old_dentry Existing file or link being referenced.
 * @param dir New parent directory into which the new hard link (new_dentry) is
 *            being created.
 * @param new_dentry New link that is being created.
 * @param ret Return value of the security_inode_link function, indicating the
 *            outcome of the operation.
 * @return int Always returns 0.
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
    // If new dentry is not allowlisted, update the allowlist map with its inode
    // ID
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
 * @param dir The inode of the parent directory from which `old_dentry` is
 * unlinked.
 * @param old_dentry Existing file or link being unlinked.
 * @param ret Return value of the security_inode_unlink function, indicating the
 *            outcome of the operation.
 * @return int Always returns 0.
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

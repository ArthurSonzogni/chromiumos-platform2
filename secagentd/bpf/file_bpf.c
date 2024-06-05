// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include vmlinux.h first to declare all kernel types.
#include "include/secagentd/vmlinux/vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>

// TODO(b/243453873): Workaround to get code completion working in CrosIDE.
#undef __cplusplus
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf/bpf_utils.h"

const char LICENSE[] SEC("license") = "Dual BSD/GPL";

#define MAX_ALLOWLISTED_FILE_MOD_DEVICES 16
#define MAX_ALLOWLISTED_FILE_INODES 1024
#define MAX_PATH_DEPTH 32

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
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __type(key, dev_t);   // device id
  __type(value, bool);  // value not used
  __uint(max_entries, MAX_ALLOWLISTED_FILE_MOD_DEVICES);
  __uint(pinning, LIBBPF_PIN_BY_NAME);
} allowlisted_devices SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, MAX_ALLOWLISTED_FILE_INODES);
  __type(key, ino_t);  // inode for directory or file
  __type(value,
         enum file_monitoring_mode);  // Checks whether this path/file is
                                      // monitored only for rw or both r/rw
} allowlisted_file_inodes SEC(
    ".maps");  // directories that are monitored for read-only operations

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
 * Checks if any ancestor directory of a file is allowlisted for the specified
 * access mode.
 *
 * This function traverses the file's path upwards (towards the root directory)
 * for a maximum of MAX_PATH_DEPTH levels. It checks if any of the encountered
 * directories (including the file itself) are in the `allowlisted_file_inodes`
 * map and have a matching file_monitoring_mode.
 *
 * @param file_dentry Pointer to dentry of the file whose ancestors need to be
 * checked.
 * @param fmod_type   The type of file modification operation (read-only,
 * read-write).
 * @return true if an allowlisted ancestor with a matching mode is found, false
 * otherwise.
 */
static __always_inline bool check_ancestor(struct dentry* file_dentry,
                                           enum filemod_type fmod_type) {
  struct dentry* parent_dentry;

  // Iterate up the directory hierarchy
  for (int i = 0; i < MAX_PATH_DEPTH; i++) {
    // Read the inode number of the current file
    u64 current_ino = BPF_CORE_READ(file_dentry, d_inode, i_ino);

    // Look up monitoring mode for the current inode in the allowlist map
    enum file_monitoring_mode* monitoring_mode =
        bpf_map_lookup_elem(&allowlisted_file_inodes, &current_ino);

    // If monitoring mode found, check if it matches the required mode
    if (monitoring_mode != NULL) {
      if (*monitoring_mode == READ_AND_READ_WRITE_BOTH ||
          (*monitoring_mode == READ_WRITE_ONLY &&
           fmod_type == FMOD_READ_WRITE_OPEN)) {
        return true;  // Allowlisted ancestor found
      } else {
        return false;  // Mode mismatch, return false
      }
    }

    parent_dentry = BPF_CORE_READ(file_dentry, d_parent);

    // Check if the current directory entry (file_dentry) is the same as the
    // parent directory entry this is root of the path, so break
    if (file_dentry == parent_dentry)
      break;

    file_dentry = parent_dentry;
  }

  return false;  // No allowlisted ancestor found
}

/**
 * Determines if a file (represented by its dentry) is allowlisted for the given
 * operation.
 *
 * This function checks if the file:
 *   1. Resides on an allowlisted device.
 *   2. Is directly allowlisted, or if any of its ancestor directories are
 * allowlisted, with the correct file_monitoring_mode for the given
 * filemod_type.
 *
 * @param file_dentry The dentry of the file to check.
 * @param dev_id      The device ID where the file resides.
 * @param fmod_type   The type of file operation (e.g., read-only, read-write).
 * @return true if the file or any ancestor directory is allowlisted, false
 * otherwise.
 */
static __always_inline bool is_dentry_allowlisted(struct dentry* file_dentry,
                                                  dev_t dev_id,
                                                  enum filemod_type fmod_type) {
  // Check if the device is allowlisted
  if (bpf_map_lookup_elem(&allowlisted_devices, &dev_id) == NULL) {
    return false;  // Device ID not allowlisted
  }

  // Read the inode number of the file
  u64 ino = BPF_CORE_READ(file_dentry, d_inode, i_ino);

  // Look up monitoring mode for the file inode in the appropriate allowlist map
  enum file_monitoring_mode* monitoring_mode =
      bpf_map_lookup_elem(&allowlisted_file_inodes, &ino);

  // If monitoring mode found, check if it matches the required mode
  if (monitoring_mode != NULL) {
    if (*monitoring_mode == READ_AND_READ_WRITE_BOTH ||
        (*monitoring_mode == READ_WRITE_ONLY &&
         fmod_type == FMOD_READ_WRITE_OPEN)) {
      return true;  // File or ancestor is allowlisted
    } else {
      return false;  // Mode mismatch, return false
    }
  }

  // Check if any ancestor directory is allowlisted
  if (check_ancestor(file_dentry, fmod_type)) {
    return true;  // Ancestor is allowlisted
  }

  return false;  // File or ancestor not allowlisted
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
  struct dentry* filp_dentry;
  struct inode* file_inode;
  file_inode = BPF_CORE_READ(filp, f_inode);
  filp_dentry = BPF_CORE_READ(filp, f_path.dentry);
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

  // print_cros_event(event);

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
  }

  // 5. Check Allowlist
  dev_t device_id = BPF_CORE_READ(file_inode, i_sb, s_dev);
  if (!is_dentry_allowlisted(filp_dentry, device_id, fmod_type)) {
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
  if (!is_dentry_allowlisted(dentry, dev_id, FMOD_ATTR)) {
    return 0;
  }

  // 5. Populate Ring Buffer
  populate_rb(FMOD_ATTR, kFileAttributeModifyEvent, filp);
  return 0;
}
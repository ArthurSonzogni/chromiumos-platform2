/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CROSID_H
#define _CROSID_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Common path constants */
#define PROC_FDT_PATH "/proc/device-tree"
#define PROC_FDT_COREBOOT_PATH PROC_FDT_PATH "/firmware/coreboot"
#define SYSFS_SMBIOS_ID_PATH "/sys/class/dmi/id"
#define SYSFS_VPD_RO_PATH "/sys/firmware/vpd/ro"
#define UNIBUILD_CONFIG_PATH "/usr/share/chromeos-config"

/* Logging functions */

enum log_level {
	/*
	 * For printing messages that indicate why crosid is exiting
	 * with failure status.
	 */
	LOG_ERR = 0,
	/*
	 * For printing messages for debugging device identity
	 * matching.
	 */
	LOG_DBG,
	/*
	 * For all other messages, including those which may not
	 * actually indicate a real issue.
	 */
	LOG_SPEW,
};

/**
 * crosid_set_log_level() - Set logging verbosity
 *
 * @log_level: The log level to use.
 */
void crosid_set_log_level(enum log_level log_level);

/**
 * crosid_log() - Log to stderr
 *
 * @log_level: The verbosity of the message.
 * @format: The format string.
 */
__attribute__((format(printf, 2, 3))) void
crosid_log(enum log_level log_level, const char *restrict format, ...);

/* File functions */

/**
 * crosid_set_sysroot() - Change the root used by file operations
 *
 * @path: The new root directory.
 */
void crosid_set_sysroot(const char *path);

/**
 * crosid_read_file() - Allocate a buffer and read a file into it
 *
 * @dir: The directory where the file is located
 * @file: The file name
 * @data_ptr: Set to the allocated buffer
 * @size_ptr: Set to the size of the allocated buffer.  If set to
 *     NULL, the size will not be stored.
 *
 * Returns: <0 on error, 0 on success
 *
 * Note: for convenience, the file contents will always be
 * null-terminated.  The size of the terminator is not included in the
 * size written to size_ptr.
 */
int crosid_read_file(const char *dir, const char *file, char **data_ptr,
		     size_t *size_ptr);

/* SKU functions */

/**
 * crosid_get_sku_id() - Get the SKU ID of the system
 *
 * @out: Output value, the SKU ID
 * @srctype: Output pointer set to static string of source type
 *     (either SMBIOS or FDT).  This is intended for printing in debug
 *     messages.
 *
 * Returns: <0 on error, 0 on success
 */
int crosid_get_sku_id(uint32_t *out, const char **srctype);

/* Device Probe Functions */

struct crosid_optional_string {
	bool present;
	char *value;
	size_t len;
};

struct crosid_probed_device_data {
	bool has_sku_id;
	uint32_t sku_id;
	struct crosid_optional_string smbios_name;
	struct crosid_optional_string fdt_compatible;
	struct crosid_optional_string whitelabel_tag;
	struct crosid_optional_string customization_id;
};

/**
 * crosid_probe() - Read firmware variables from device
 *
 * @out: A caller-allocated struct to write the probed data to
 *
 * Returns: <0 on error, 0 on success.
 *
 * Note: The struct must be passed to crosid_probe_free after usage to
 * cleanup allocated memory from files read.
 */
int crosid_probe(struct crosid_probed_device_data *out);

/**
 * crosid_match() - Match probed device data to a cros_config identity
 *
 * @data: The data struct, created from the result of a crosid_probe.
 *
 * Returns: <0 on error, or the index of the probed config upon
 * success.
 */
int crosid_match(struct crosid_probed_device_data *data);

/**
 * crosid_print_vars() - Print system info
 *
 * @out: Output file
 * @data: The data struct, created from the result of a crosid_probe.
 * @config_idx: The config index from crosid_match.
 */
void crosid_print_vars(FILE *out, struct crosid_probed_device_data *data,
		       int config_idx);

/**
 * crosid_probe_free() - Free memory allocated by a crosid_probe()
 *
 * @data - The struct created from a crosid_probe()
 */
void crosid_probe_free(struct crosid_probed_device_data *data);

/* Identity table */

/*
 * Bump this number when backwards-incompatible changes are made to
 * the struct format.  This must be kept in sync with the
 * cros_config_schema implementation.
 */
#define CROSID_TABLE_VERSION 1

enum crosid_table_flags {
	MATCH_SKU_ID = (1 << 0),
	MATCH_WHITELABEL_TAG = (1 << 1),
	MATCH_CUSTOMIZATION_ID = (1 << 2),
	MATCH_FDT_COMPATIBLE = (1 << 3),
	MATCH_SMBIOS_NAME = (1 << 4),
};

struct crosid_table_entry {
	uint32_t flags;
	uint32_t smbios_name_match;
	uint32_t fdt_compatible_match;
	uint32_t sku_id_match;
	union {
		uint32_t customization_id_match;
		uint32_t whitelabel_tag_match;
	};
} __attribute__((packed));

struct crosid_table_header {
	uint32_t version;
	uint32_t entry_count;
	struct crosid_table_entry entries[];
} __attribute__((packed));

#endif /* _CROSID_H */

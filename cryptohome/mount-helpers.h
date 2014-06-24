/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Header file for mount helpers.
 */
#ifndef _MOUNT_HELPERS_H_
#define _MOUNT_HELPERS_H_

/* General utility functions. */
uint64_t blk_size(const char *device);
int remove_tree(const char *tree);
int runcmd(const gchar *argv[], gchar **output);
int same_vfs(const char *mnt_a, const char *mnt_b);
char *stringify_hex(uint8_t *binary, size_t length);
uint8_t *hexify_string(char *string, uint8_t *binary, size_t length);
void shred(const char *keyfile);

/* Loopback device attach/detach helpers. */
gchar *loop_attach(int fd, const char *name);
int loop_detach(const gchar *loopback);
int loop_detach_name(const char *name);

/* Encrypted device mapper setup/teardown. */
int dm_setup(uint64_t bytes, const gchar *encryption_key, const char *name,
		const gchar *device, const char *path, int discard);
int dm_teardown(const gchar *device);
char *dm_get_key(const gchar *device);

/* Sparse file creation. */
int sparse_create(const char *path, uint64_t bytes);

/* Filesystem creation. */
int filesystem_build(const char *device, uint64_t block_bytes,
			uint64_t blocks_min, uint64_t blocks_max);
int filesystem_resize(const char *device, uint64_t blocks, uint64_t blocks_max);

/* Encrypted keyfile handling. */
char *keyfile_read(const char *keyfile, uint8_t *system_key);
int keyfile_write(const char *keyfile, uint8_t *system_key, char *plain);

#endif /* _MOUNT_HELPERS_H_ */

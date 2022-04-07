/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crosid.h"

static int read_optional_string(const char *dir, const char *name,
				struct crosid_optional_string *out)
{
	char *value;
	size_t len;

	if (crosid_read_file(dir, name, &value, &len) < 0) {
		out->present = false;
		out->value = NULL;
		out->len = 0;
		return -1;
	}

	/* Strip a trailing newline, if it exists */
	if (len > 0 && value[len - 1] == '\n') {
		len--;
		value[len] = '\0';
	}

	out->present = true;
	out->value = value;
	out->len = len;

	return 0;
}

static int read_custom_label_tag(struct crosid_probed_device_data *out)
{
	int rv;

	/* Newer devices may use custom_label_tag VPD entry */
	rv = read_optional_string(SYSFS_VPD_RO_PATH, "custom_label_tag",
				  &out->custom_label_tag);
	if (rv >= 0)
		return rv;

	/* If that's not specified, then try whitelabel_tag */
	return read_optional_string(SYSFS_VPD_RO_PATH, "whitelabel_tag",
				    &out->custom_label_tag);
}

int crosid_probe(struct crosid_probed_device_data *out)
{
	const char *sku_src;

	/* To be later populated by crosid_match */
	out->firmware_manifest_key = NULL;

	if (crosid_get_sku_id(&out->sku_id, &sku_src) >= 0) {
		out->has_sku_id = true;
		crosid_log(LOG_DBG, "Read SKU=%u (from %s)\n", out->sku_id,
			   sku_src);
	} else {
		out->has_sku_id = false;
		crosid_log(LOG_DBG,
			   "System has no SKU ID (this is normal on some "
			   "models, especially older ones)\n");
	}

	if (read_optional_string(SYSFS_SMBIOS_ID_PATH, "product_name",
				 &out->smbios_name) >= 0) {
		crosid_log(LOG_DBG, "Read SMBIOS name \"%s\"\n",
			   out->smbios_name.value);
	}

	if (read_optional_string(PROC_FDT_PATH, "compatible",
				 &out->fdt_compatible) >= 0) {
		crosid_log(LOG_DBG, "Read FDT compatible\n");
	}

	if (read_optional_string(SYSFS_VPD_RO_PATH, "customization_id",
				 &out->customization_id) >= 0) {
		crosid_log(LOG_DBG, "Read customization_id=\"%s\" (from VPD)\n",
			   out->customization_id.value);
	} else {
		crosid_log(LOG_DBG,
			   "Device has no customization_id (this is to be "
			   "expected on models released in 2018 and later)\n");
	}

	if (read_custom_label_tag(out) >= 0) {
		crosid_log(LOG_DBG, "Read custom_label_tag=\"%s\" (from VPD)\n",
			   out->custom_label_tag.value);
	} else {
		crosid_log(LOG_DBG,
			   "Device has no custom_label_tag (this is to be "
			   "expected, except of custom label devices)\n");
	}

	if (out->customization_id.present && out->custom_label_tag.present) {
		crosid_log(LOG_ERR, "Device has both a customization_id and a "
				    "custom_label_tag. VPD invalid?\n");
		crosid_probe_free(out);
		memset(out, 0, sizeof(*out));
		return -1;
	}

	return 0;
}

void crosid_print_vars(FILE *out, struct crosid_probed_device_data *data,
		       int config_idx)
{
	if (data->has_sku_id)
		fprintf(out, "SKU=%u\n", data->sku_id);
	else
		fprintf(out, "SKU=none\n");

	if (config_idx >= 0)
		fprintf(out, "CONFIG_INDEX=%d\n", config_idx);
	else
		fprintf(out, "CONFIG_INDEX=unknown\n");

	if (data->firmware_manifest_key)
		fprintf(out, "FIRMWARE_MANIFEST_KEY='%s'\n",
			data->firmware_manifest_key);
	else
		fprintf(out, "FIRMWARE_MANIFEST_KEY=\n");
}

void crosid_probe_free(struct crosid_probed_device_data *data)
{
	free(data->smbios_name.value);
	free(data->fdt_compatible.value);
	free(data->custom_label_tag.value);
	free(data->customization_id.value);
	free(data->firmware_manifest_key);
}

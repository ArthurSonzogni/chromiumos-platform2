#include "lib/cros_config_struct.h"

static struct config_map all_configs[] = {
    {.platform_name = "Some",
     .smbios_match_name = "Some",
     .sku_id = 0,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.brand = "",
              .model = "some",
              .customization = "some",
              .signature_id = "some"}},

    {.platform_name = "Some",
     .smbios_match_name = "Some",
     .sku_id = 1,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.brand = "",
              .model = "some",
              .customization = "some",
              .signature_id = "some"}},

    {.platform_name = "Another",
     .smbios_match_name = "Another",
     .sku_id = -1,
     .customization_id = "",
     .whitelabel_tag = "",
     .info = {.brand = "",
              .model = "another",
              .customization = "another",
              .signature_id = "another"}},

    {.platform_name = "SomeCustomization",
     .smbios_match_name = "SomeCustomization",
     .sku_id = -1,
     .customization_id = "SomeCustomization",
     .whitelabel_tag = "",
     .info = {.brand = "",
              .model = "some_customization",
              .customization = "SomeCustomization",
              .signature_id = "some_customization"}},

    {.platform_name = "Some",
     .smbios_match_name = "Some",
     .sku_id = 8,
     .customization_id = "",
     .whitelabel_tag = "whitelabel1",
     .info = {.brand = "",
              .model = "whitelabel",
              .customization = "whitelabel1",
              .signature_id = "whitelabel-whitelabel1"}},

    {.platform_name = "Some",
     .smbios_match_name = "Some",
     .sku_id = 9,
     .customization_id = "",
     .whitelabel_tag = "whitelabel1",
     .info = {.brand = "",
              .model = "whitelabel",
              .customization = "whitelabel1",
              .signature_id = "whitelabel-whitelabel1"}},

    {.platform_name = "Some",
     .smbios_match_name = "Some",
     .sku_id = 8,
     .customization_id = "",
     .whitelabel_tag = "whitelabel2",
     .info = {.brand = "",
              .model = "whitelabel",
              .customization = "whitelabel2",
              .signature_id = "whitelabel-whitelabel2"}},

    {.platform_name = "Some",
     .smbios_match_name = "Some",
     .sku_id = 9,
     .customization_id = "",
     .whitelabel_tag = "whitelabel2",
     .info = {.brand = "",
              .model = "whitelabel",
              .customization = "whitelabel2",
              .signature_id = "whitelabel-whitelabel2"}}
};

const struct config_map *cros_config_get_config_map(int *num_entries) {
  *num_entries = 8;
  return &all_configs[0];
}

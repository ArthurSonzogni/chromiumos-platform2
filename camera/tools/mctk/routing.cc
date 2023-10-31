/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NOTE: This code merely serves as an example usage of the class hierarchy,
 *       and has not been optimised for style or quality.
 *
 * For each sensor on a media controller, attempt to find and configure
 * a route to a /dev/videoX device using a depth-first search.
 *
 * This assumes that any free links can be used equally well, and hence
 * works best on homogeneous devices like IPU6.
 *
 * This is a remnant of the v1 tool:
 * https://chromium-review.googlesource.com/c/chromiumos/platform2/+/4055245
 */

// NOLINTNEXTLINE(build/include)
#include "tools/mctk/routing.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/mcdev.h"

// #define ROUTING_PRINT_EVERY_STEP

namespace {

__u32 SubFmtToV4lFmt(__u32 mbus_code) {
  static const __u32 lut_bayer[] = {
      V4L2_PIX_FMT_SBGGR8,       /* MEDIA_BUS_FMT_SBGGR8_1X8         0x3001 */
      V4L2_PIX_FMT_SGRBG8,       /* MEDIA_BUS_FMT_SGRBG8_1X8         0x3002 */
      V4L2_PIX_FMT_SBGGR10,      /* MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_BE 0x3003 */
      V4L2_PIX_FMT_SBGGR10,      /* MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE 0x3004 */
      V4L2_PIX_FMT_SBGGR10,      /* MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_BE 0x3005 */
      V4L2_PIX_FMT_SBGGR10,      /* MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_LE 0x3006 */
      V4L2_PIX_FMT_SBGGR10,      /* MEDIA_BUS_FMT_SBGGR10_1X10       0x3007 */
      V4L2_PIX_FMT_SBGGR12,      /* MEDIA_BUS_FMT_SBGGR12_1X12       0x3008 */
      V4L2_PIX_FMT_SGRBG10DPCM8, /* MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8  0x3009 */
      V4L2_PIX_FMT_SGRBG10,      /* MEDIA_BUS_FMT_SGRBG10_1X10       0x300a */
      V4L2_PIX_FMT_SBGGR10,      /* MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8  0x300b */
      V4L2_PIX_FMT_SGBRG10,      /* MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8  0x300c */
      V4L2_PIX_FMT_SRGGB10,      /* MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8  0x300d */
      V4L2_PIX_FMT_SGBRG10,      /* MEDIA_BUS_FMT_SGBRG10_1X10       0x300e */
      V4L2_PIX_FMT_SRGGB10,      /* MEDIA_BUS_FMT_SRGGB10_1X10       0x300f */
      V4L2_PIX_FMT_SGBRG12,      /* MEDIA_BUS_FMT_SGBRG12_1X12       0x3010 */
      V4L2_PIX_FMT_SGRBG12,      /* MEDIA_BUS_FMT_SGRBG12_1X12       0x3011 */
      V4L2_PIX_FMT_SRGGB12,      /* MEDIA_BUS_FMT_SRGGB12_1X12       0x3012 */
      V4L2_PIX_FMT_SGBRG8,       /* MEDIA_BUS_FMT_SGBRG8_1X8         0x3013 */
      V4L2_PIX_FMT_SRGGB8,       /* MEDIA_BUS_FMT_SRGGB8_1X8         0x3014 */
      V4L2_PIX_FMT_SBGGR10ALAW8, /* MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8  0x3015 */
      V4L2_PIX_FMT_SGBRG10ALAW8, /* MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8  0x3016 */
      V4L2_PIX_FMT_SGRBG10ALAW8, /* MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8  0x3017 */
      V4L2_PIX_FMT_SRGGB10ALAW8, /* MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8  0x3018 */
      /* V4L2_PIX_FMT_SBGGR14 defined in Linux v4.19 */
      v4l2_fourcc('B', 'G', '1', '4'), /* MEDIA_BUS_FMT_SBGGR14_1X14 0x3019 */
      /* V4L2_PIX_FMT_SGBRG14 defined in Linux v4.19 */
      v4l2_fourcc('G', 'B', '1', '4'), /* MEDIA_BUS_FMT_SGBRG14_1X14 0x301a */
      /* V4L2_PIX_FMT_SGRBG14 defined in Linux v4.19 */
      v4l2_fourcc('G', 'R', '1', '4'), /* MEDIA_BUS_FMT_SGRBG14_1X14 0x301b */
      /* V4L2_PIX_FMT_SRGGB14 defined in Linux v4.19 */
      v4l2_fourcc('R', 'G', '1', '4'), /* MEDIA_BUS_FMT_SRGGB14_1X14 0x301c */
      V4L2_PIX_FMT_SBGGR16, /* MEDIA_BUS_FMT_SBGGR16_1X16       0x301d */
      V4L2_PIX_FMT_SGBRG16, /* MEDIA_BUS_FMT_SGBRG16_1X16       0x301e */
      V4L2_PIX_FMT_SGRBG16, /* MEDIA_BUS_FMT_SGRBG16_1X16       0x301f */
      V4L2_PIX_FMT_SRGGB16, /* MEDIA_BUS_FMT_SRGGB16_1X16       0x3020 */
  };

  if (mbus_code >= MEDIA_BUS_FMT_SBGGR8_1X8 &&
      mbus_code <= MEDIA_BUS_FMT_SRGGB16_1X16) {
    return lut_bayer[mbus_code - MEDIA_BUS_FMT_SBGGR8_1X8];
  }

  return 0;
}

void AnydevSetFormatFromSubfmt(V4lMcPad& pad,
                               struct v4l2_mbus_framefmt& subfmt) {
  /* Try setting a "crop" selection showing the full frame.
   * We don't fail here, as the driver may work as intended
   * even if it doesn't support these options.
   */
  struct v4l2_rect crop = {
      .left = 0,
      .top = 0,
      .width = subfmt.width,
      .height = subfmt.height,
  };

  if (pad.entity_.desc_.type == MEDIA_ENT_F_IO_V4L) {
    /* This pad is a V4L maindev /dev/videoX */
    struct v4l2_pix_format pix = {};
    pix.width = subfmt.width;
    pix.height = subfmt.height;
    pix.pixelformat = SubFmtToV4lFmt(subfmt.code);
    pix.field = V4L2_FIELD_NONE;
    /* IPU6 may or may not work without proper bytesperline and sizeimage */
    // pix.bytesperline = 0;
    // pix.sizeimage = 0;
    pix.colorspace = subfmt.colorspace;

    struct v4l2_pix_format_mplane pix_mp = {};
    pix_mp.width = subfmt.width;
    pix_mp.height = subfmt.height;
    pix_mp.pixelformat = SubFmtToV4lFmt(subfmt.code);
    pix_mp.field = V4L2_FIELD_NONE;
    pix_mp.colorspace = subfmt.colorspace;
    /* IPU6 may or may not work without proper bytesperline and sizeimage */
    // pix_mp.plane_fmt[0].sizeimage = 0;
    // pix_mp.plane_fmt[0].bytesperline = 0;

    /* Did the format conversion fail? */
    if (!pix.pixelformat) {
      MCTK_ERR("Routing: Format conversion from subfmt to V4L fmt failed.");
      return;
    }

    /* Just set the formats as-is */
    pad.entity_.SetFmtVideoCapture(pix);
    pad.entity_.SetFmtVideoCaptureMplane(pix_mp);

    /* Set the full-frame crop selection */
    pad.entity_.SetSelection(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                             V4L2_SEL_TGT_CROP, crop);
  } else {
    /* This pad is a V4L subdev /dev/v4l-subdevX */
    pad.SetFmt(subfmt);

    /* Set the full-frame crop selection */
    pad.SetSelection(V4L2_SEL_TGT_CROP, crop);
  }
}

/* Recursively try to find a route from a given entity to a V4L device.
 * This currently assumes an Intel IPU6 like architecture:
 *  -> Directed graph
 *  -> No cycles
 *  -> At each level, all links are equal.
 *  -> At each level, we can pick any unused entity.
 *  -> There are no immutable links.
 * Even on IPU6, we have reduced the choices a bit by excluding entities
 * that break these assumptions.
 */
bool RouteFrom(V4lMcDev& mcdev,
               V4lMcEntity& entity,
               std::vector<V4lMcLink*>& route) {
#ifdef ROUTING_PRINT_EVERY_STEP
  fprintf(stderr, "%s: Looking at: %s\n", __func__, entity->desc_.name);
#endif

  /* IPU6 HACK:
   * Ignore Intel IPU6 CSI-2 capture.
   * Let's assume these devices are numbered in single digits only,
   * and match the string parts the simple way.
   */
  if (!memcmp(entity.desc_.name, "Intel IPU6 CSI-2 ", 17) &
      !memcmp(&entity.desc_.name[18], " capture", 9))
    return false;

  /* IPU6 HACK:
   * Ignore this unknown device.
   * We want the "BE SOC" targets for now.
   */
  if (!strcmp(entity.desc_.name, "Intel IPU6 CSI2 BE"))
    return false;

  /* If there is already en entity connected to us, backtrack.
   * Our caller will try the next entity.
   */
  for (auto link : mcdev.all_links_) {
    if (!link->IsDataLink())
      /* We only look at data links */
      continue;

    if (&link->sink_->entity_ != &entity)
      continue;

    if (link->IsImmutable())
      /* Ignore these for now.
       * This is likely to be seen in devices with more complex routing
       * requirements, and never occurs in IPU6.
       */
      continue;

    /* Something is already connected to this entity,
     * so drop it from routing.
     */
    if (link->IsEnabled())
      return false;
  }

  if (entity.desc_.type == MEDIA_ENT_F_IO_V4L)
    /* Done, we've found a route! */
    return true;

  /* We're not the end of the line, and we're yet unconnected.
   * Try all outgoing links (they are all in the entity's array).
   *
   * link->src->entity_ == entity
   */
  for (auto& link : entity.links_) {
    if (!link->IsDataLink())
      /* We only look at data links */
      continue;

    if (link->IsImmutable())
      /* Ignore these for now.
       * This is likely to be seen in devices with more complex routing
       * requirements, and never occurs in IPU6.
       */
      continue;

    /* If this is true, there is a path to a V4L video device. */
    if (RouteFrom(mcdev, link->sink_->entity_, route)) {
      route.insert(route.begin(), link.get());

      return true;
    }
  }

  return false;
}

}  // namespace

void V4lMcRouteSensors(V4lMcDev& mcdev) {
  /* IPU6 HACK:
   * Warn if this is not run on IPU6
   */
  if (strcmp(mcdev.info_.driver, "intel-ipu6-isys") ||
      strcmp(mcdev.info_.model, "ipu6"))
    MCTK_ERR("This is not an IPU6 device. Assumptions may not hold.");

  /* First, find a camera */
  for (auto& sensor_entity : mcdev.entities_) {
    std::vector<V4lMcLink*> route;
    V4lMcPad* camera_pad;
    struct v4l2_mbus_framefmt subfmt;

    if (sensor_entity->desc_.type != MEDIA_ENT_T_V4L2_SUBDEV_SENSOR)
      /* synonym: MEDIA_ENT_F_CAM_SENSOR */
      continue;

    /* Second, route it to whatever output we can */
    if (!(RouteFrom(mcdev, *sensor_entity, route))) {
      fprintf(stdout, "NO ROUTE FOR: %s\n", sensor_entity->desc_.name);
      continue;
    }

    /* Get camera's format */
    camera_pad = route.front()->src_;
    MCTK_ASSERT(camera_pad->subdev_.fmt);
    subfmt = *camera_pad->subdev_.fmt;

    /* Set all links and video formats */
    for (auto* hop : route) {
      AnydevSetFormatFromSubfmt(*hop->src_, subfmt);
      AnydevSetFormatFromSubfmt(*hop->sink_, subfmt);

      hop->SetEnable(true);
    }

    /* IPU6 HACK:
     * Disable Intel IPU6 compression
     */
    auto* cc = route.back()->sink_->entity_.ControlById(0x00981983);
    if (cc)
      cc->Set<__s32>(0);

    /* Print the routing */
    fprintf(stdout, "Routed: %s = %s\n",
            route.back()->sink_->entity_.devpath_.c_str(),
            sensor_entity->desc_.name);
  }
}

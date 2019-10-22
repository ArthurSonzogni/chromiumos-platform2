/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "MtkCam/HalSensor"

#include "MyUtils.h"
#ifdef USING_MTK_LDVT
#include <uvvf.h>
#endif
#include <fcntl.h>
#include <mtkcam/def/common.h>
#include <mtkcam/v4l2/IPCIHalSensor.h>
#include <sys/ioctl.h>
#include "mtkcam/drv/sensor/HalSensor.h"
#include "mtkcam/drv/sensor/img_sensor.h"
#include <linux/v4l2-subdev.h>
#include <cutils/compiler.h>  // for CC_LIKELY, CC_UNLIKELY

// STL
#include <array>   // std::array
#include <memory>  // shared_ptr
#include <string>
#include <vector>  // std::vector

using NSCam::IIPCHalSensor;
using NSCam::IIPCHalSensorListProv;
using NSCam::SensorCropWinInfo;
using NSCam::SensorVCInfo;

#if MTKCAM_HAVE_SANDBOX_SUPPORT
static IHalSensor* createIPCHalSensorByIdx(MUINT32 idx) {
  IIPCHalSensorListProv* pIPCSensorList = IIPCHalSensorListProv::getInstance();
  if (CC_UNLIKELY(pIPCSensorList == nullptr)) {
    CAM_LOGE(
        "get IIPCHalSensorListProv is nullptr, sendCommand to IPCSensor"
        "failed");
    return nullptr;
  }

  IHalSensor* pIPCSensor = pIPCSensorList->createSensor(LOG_TAG, idx);

  if (CC_UNLIKELY(pIPCSensor == nullptr)) {
    CAM_LOGE("create IIPCHalSensor failed, sendCommand failed");
    return nullptr;
  }
  return pIPCSensor;
}

template <class ARG1_T, class ARG2_T>
inline void _updateCommand(MUINT i,
                           MUINTPTR cmd,
                           ARG1_T* arg1,
                           ARG2_T* arg2,
                           IHalSensor* p,
                           IIPCHalSensor* q) {
  p->sendCommand(i, cmd, reinterpret_cast<MUINTPTR>(&(*arg1)), sizeof(*arg1),
                 reinterpret_cast<MUINTPTR>(&(*arg2)), sizeof(*arg2), 0, 0);

  q->updateCommand(i, cmd, reinterpret_cast<MUINTPTR>(&(*arg1)),
                   reinterpret_cast<MUINTPTR>(&(*arg2)), 0);
}

static void sendDataToIPCHalSensor(IHalSensor* pSource,
                                   IIPCHalSensor* pTarget,
                                   MUINT indexDual) {
  // supported scenarios
  const std::array<MINT32, 3> scenarios = {
      NSCam::SENSOR_SCENARIO_ID_NORMAL_PREVIEW,
      NSCam::SENSOR_SCENARIO_ID_NORMAL_CAPTURE,
      NSCam::SENSOR_SCENARIO_ID_NORMAL_VIDEO};

  // SENSOR_CMD_GET_SENSOR_CROP_WIN_INFO
  for (const auto& i : scenarios) {
    MINT32 arg1 = i;
    SensorCropWinInfo arg2;

    _updateCommand<MINT32, SensorCropWinInfo>(
        indexDual, NSCam::SENSOR_CMD_GET_SENSOR_CROP_WIN_INFO, &arg1, &arg2,
        pSource, pTarget);
  }

  // SENSOR_CMD_GET_PIXEL_CLOCK_FREQ
  do {
    MINT32 arg1 = 0, arg2 = 0;
    _updateCommand<MINT32, MINT32>(indexDual,
                                   NSCam::SENSOR_CMD_GET_PIXEL_CLOCK_FREQ,
                                   &arg1, &arg2, pSource, pTarget);
  } while (0);

  // SENSOR_CMD_GET_FRAME_SYNC_PIXEL_LINE_NUM
  do {
    MUINT32 arg1 = 0, arg2 = 0;
    _updateCommand<MUINT32, MUINT32>(
        indexDual, NSCam::SENSOR_CMD_GET_FRAME_SYNC_PIXEL_LINE_NUM, &arg1,
        &arg2, pSource, pTarget);
  } while (0);

  // SENSOR_CMD_GET_SENSOR_PDAF_INFO
  for (const auto& i : scenarios) {
    MINT32 arg1 = i;
    SET_PD_BLOCK_INFO_T arg2;
    _updateCommand<MINT32, SET_PD_BLOCK_INFO_T>(
        indexDual, NSCam::SENSOR_CMD_GET_SENSOR_PDAF_INFO, &arg1, &arg2,
        pSource, pTarget);
  }

  // SENSOR_CMD_GET_SENSOR_PDAF_CAPACITY
  for (const auto& i : scenarios) {
    MINT32 arg1 = i;
    MBOOL arg2 = MFALSE;
    _updateCommand<MINT32, MBOOL>(indexDual,
                                  NSCam::SENSOR_CMD_GET_SENSOR_PDAF_CAPACITY,
                                  &arg1, &arg2, pSource, pTarget);
  }

  // SENSOR_CMD_GET_SENSOR_VC_INFO
  for (const auto& i : scenarios) {
    SensorVCInfo arg1;
    MINT32 arg2 = i;
    _updateCommand<SensorVCInfo, MINT32>(indexDual,
                                         NSCam::SENSOR_CMD_GET_SENSOR_VC_INFO,
                                         &arg1, &arg2, pSource, pTarget);
  }

  // SENSOR_CMD_GET_DEFAULT_FRAME_RATE_BY_SCENARIO
  for (const auto& i : scenarios) {
    MINT32 arg1 = i;
    MUINT32 arg2 = 0;
    _updateCommand<MINT32, MUINT32>(
        indexDual, NSCam::SENSOR_CMD_GET_DEFAULT_FRAME_RATE_BY_SCENARIO, &arg1,
        &arg2, pSource, pTarget);
  }

  // SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER
  do {
    MUINT32 arg1 = 0, arg2 = 0;
    _updateCommand<MUINT32, MUINT32>(
        indexDual, NSCam::SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER, &arg1, &arg2,
        pSource, pTarget);
  } while (0);

  // SENSOR_CMD_GET_VERTICAL_BLANKING
  do {
    MINT32 arg1 = 0, arg2 = 0;
    _updateCommand<MINT32, MINT32>(indexDual,
                                   NSCam::SENSOR_CMD_GET_VERTICAL_BLANKING,
                                   &arg1, &arg2, pSource, pTarget);
  } while (0);
}
#endif

/******************************************************************************
 *
 ******************************************************************************/
HalSensor::~HalSensor() {}

/******************************************************************************
 *
 ******************************************************************************/
HalSensor::HalSensor()
    : mSensorIdx(IMGSENSOR_SENSOR_IDX_NONE),
      mScenarioId(0),
      mHdrMode(0),
      mPdafMode(0),
      mFramerate(0) {
  memset(&mSensorDynamicInfo, 0, sizeof(SensorDynamicInfo));
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
HalSensor::destroyInstance(char const* szCallerName) {
  HalSensorList::singleton()->closeSensor(this, szCallerName);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
HalSensor::onDestroy() {
  CAM_LOGD("#Sensor:%zu", mSensorData.size());

  std::unique_lock<std::mutex> lk(mMutex);

  if (mSensorIdx == IMGSENSOR_SENSOR_IDX_NONE) {
    mSensorData.clear();
  } else {
    CAM_LOGI("Forget to powerOff before destroying. mSensorIdx:%d", mSensorIdx);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HalSensor::onCreate(vector<MUINT> const& vSensorIndex) {
  CAM_LOGD("+ #Sensor:%zu", vSensorIndex.size());

  std::unique_lock<std::mutex> lk(mMutex);

  mSensorData.clear();
  for (MUINT i = 0; i < vSensorIndex.size(); i++) {
    MUINT const uSensorIndex = vSensorIndex[i];

    mSensorData.push_back(uSensorIndex);
  }

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HalSensor::isMatch(vector<MUINT> const& vSensorIndex) const {
  if (vSensorIndex.size() != mSensorData.size()) {
    return MFALSE;
  }

  auto iter = mSensorData.begin();
  for (MUINT i = 0; i < vSensorIndex.size(); i++, iter++) {
    if (vSensorIndex[i] != *iter) {
      return MFALSE;
    }
  }

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HalSensor::setupLink(int sensorIdx, int flag) {
  int srcEntId = HalSensorList::singleton()->querySensorEntId(sensorIdx);
  int sinkEntId = HalSensorList::singleton()->querySeninfEntId();
  int p1NodeEntId = HalSensorList::singleton()->queryP1NodeEntId();
  const char* dev_name = HalSensorList::singleton()->queryDevName();
  int rc = 0;
  int dev_fd = 0;
  struct media_link_desc linkDesc;
  struct media_pad_desc srcPadDesc, sinkPadDesc;

  CAM_LOGD("setupLink %s (%d %d %d)\n", dev_name, srcEntId, sinkEntId,
           p1NodeEntId);
  dev_fd = open(dev_name, O_RDWR | O_NONBLOCK);
  if (dev_fd < 0) {
    CAM_LOGD("Open media device error, fd %d\n", dev_fd);
    return MFALSE;
  }

  // setup link sensor & seninf
  memset(&linkDesc, 0, sizeof(struct media_link_desc));
  srcPadDesc.entity = srcEntId;
  srcPadDesc.index = 0;
  srcPadDesc.flags = MEDIA_PAD_FL_SOURCE;
  sinkPadDesc.entity = sinkEntId;
  sinkPadDesc.index = sensorIdx;
  sinkPadDesc.flags = MEDIA_PAD_FL_SINK;

  linkDesc.source = srcPadDesc;
  linkDesc.sink = sinkPadDesc;
  linkDesc.flags = flag;

  rc = ioctl(dev_fd, MEDIA_IOC_SETUP_LINK, &linkDesc);
  if (rc < 0) {
    CAM_LOGE("Link setup failed @1: %s", strerror(errno));
    close(dev_fd);
    return MFALSE;
  }

  close(dev_fd);

  return MTRUE;
}

MBOOL
HalSensor::powerOn(char const* szCallerName,
                   MUINT const uCountOfIndex,
                   MUINT const* pArrayOfIndex) {
  if (pArrayOfIndex == NULL) {
    CAM_LOGE("powerOn fail, pArrayOfIndex == NULL\n");
    return MFALSE;
  }

  IMGSENSOR_SENSOR_IDX sensorIdx =
      (IMGSENSOR_SENSOR_IDX)HalSensorList::singleton()
          ->queryEnumInfoByIndex(*pArrayOfIndex)
          ->getDeviceId();
  const char* sensorSubdevName =
      HalSensorList::singleton()->querySensorSubdevName(sensorIdx);
  const char* seninfSubdevName =
      HalSensorList::singleton()->querySeninfSubdevName();
  int sensorNum = HalSensorList::singleton()->queryNumberOfSensors();

  int sensor_fd = 0;
  int seninf_fd = 0;

  CAM_LOGI("powerOn %d %d\n", *pArrayOfIndex, sensorIdx);

  sensor_fd = open(sensorSubdevName, O_RDWR);
  if (sensor_fd < 0) {
    CAM_LOGE("[%s] open v4l2 sensor subdev fail\n", __FUNCTION__);
    HalSensorList::singleton()->setSensorFd(sensor_fd, sensorIdx);
    return MFALSE;
  }

  seninf_fd = open(seninfSubdevName, O_RDWR);
  if (sensor_fd < 0) {
    CAM_LOGE("[%s] open v4l2 seninf subdev fail\n", __FUNCTION__);
    HalSensorList::singleton()->setSeninfFd(seninf_fd);
    return MFALSE;
  }

  HalSensorList::singleton()->setSensorFd(sensor_fd, sensorIdx);
  HalSensorList::singleton()->setSeninfFd(seninf_fd);

  for (int i = 0; i < sensorNum; i++) {
    setupLink(i, 0);  // reset link for all sensors
  }
  setupLink(sensorIdx, MEDIA_LNK_FL_ENABLED);
  mSensorIdx = sensorIdx;

#if MTKCAM_HAVE_SANDBOX_SUPPORT
  // send poweron command and other dynamically data to IPCIHalSensor
  do {
    IHalSensor* pIPCSensor = createIPCHalSensorByIdx(mSensorIdx);
    if (CC_UNLIKELY(pIPCSensor == nullptr)) {
      CAM_LOGE("create IIPCHalSensor failed, sendCommand failed");
      break;
    }

    pIPCSensor->powerOn(nullptr, 1 << mSensorIdx, 0);
    sendDataToIPCHalSensor(this, static_cast<IIPCHalSensor*>(pIPCSensor),
                           1 << mSensorIdx);
    pIPCSensor->destroyInstance();
  } while (0);
#endif

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
HalSensor::powerOff(char const* szCallerName,
                    MUINT const uCountOfIndex,
                    MUINT const* pArrayOfIndex) {
  IMGSENSOR_SENSOR_IDX sensorIdx =
      (IMGSENSOR_SENSOR_IDX)HalSensorList::singleton()
          ->queryEnumInfoByIndex(*pArrayOfIndex)
          ->getDeviceId();
  int sensor_fd = HalSensorList::singleton()->querySensorFd(sensorIdx);
  int seninf_fd = HalSensorList::singleton()->querySeninfFd();

  CAM_LOGI("powerOff %d %d\n", *pArrayOfIndex, sensorIdx);

  if (sensor_fd >= 0) {
    close(sensor_fd);
  }
  if (seninf_fd >= 0) {
    close(seninf_fd);
  }
#if MTKCAM_HAVE_SANDBOX_SUPPORT
  // send poweron command to
  do {
    IHalSensor* pIPCSensor = createIPCHalSensorByIdx(mSensorIdx);
    if (CC_UNLIKELY(pIPCSensor == nullptr)) {
      CAM_LOGE("create IIPCHalSensor failed, sendCommand failed");
      break;
    }
    pIPCSensor->powerOff(nullptr, 0, 0);
    pIPCSensor->destroyInstance();
  } while (0);
#endif

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL HalSensor::querySensorDynamicInfo(MUINT32 indexDual,
                                        SensorDynamicInfo* pSensorDynamicInfo) {
  if (pSensorDynamicInfo == NULL) {
    CAM_LOGE("querySensorDynamicInfo fail, pSensorDynamicInfo is NULL\n");
    return MFALSE;
  }
  memcpy(pSensorDynamicInfo, &mSensorDynamicInfo, sizeof(SensorDynamicInfo));

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL HalSensor::configure(MUINT const uCountOfParam,
                           ConfigParam const* pConfigParam) {
  MINT32 ret = MFALSE;

  if (pConfigParam == NULL) {
    CAM_LOGE("configure fail, pConfigParam is NULL\n");
    return MFALSE;
  }

  IMGSENSOR_SENSOR_IDX sensorIdx =
      (IMGSENSOR_SENSOR_IDX)HalSensorList::singleton()
          ->queryEnumInfoByIndex(pConfigParam->index)
          ->getDeviceId();
  int sensor_fd = HalSensorList::singleton()->querySensorFd(sensorIdx);
  int seninf_fd = HalSensorList::singleton()->querySeninfFd();

  struct v4l2_subdev_format aFormat;
  SensorDynamicInfo* pSensorDynamicInfo = &mSensorDynamicInfo;
  unsigned int width = 0;
  unsigned int height = 0;
  unsigned int framelength = 0;
  unsigned int line_length = 0;
  unsigned int pix_clk = 0;

  (void)uCountOfParam;
  std::unique_lock<std::mutex> lk(mMutex);

  CAM_LOGI("configure sensorIdx (%d)\n", sensorIdx);

  struct imgsensor_info_struct* pImgsensorInfo =
      HalSensorList::singleton()->getSensorInfo(sensorIdx);
  if (pImgsensorInfo == NULL) {
    CAM_LOGE("configure fail, cannot get sensor info\n");
    return MFALSE;
  }

  if (mSensorIdx == IMGSENSOR_SENSOR_IDX_NONE || mSensorIdx != sensorIdx) {
    CAM_LOGE("configure fail. mSensorIdx = %d, sensorIdx = %d\n", mSensorIdx,
             sensorIdx);
    return MFALSE;
  }

  pSensorDynamicInfo->pixelMode = SENINF_PIXEL_MODE_CAM;
  pSensorDynamicInfo->HDRPixelMode = pSensorDynamicInfo->PDAFPixelMode =
      SENINF_PIXEL_MODE_CAMSV;

  pSensorDynamicInfo->TgInfo = pSensorDynamicInfo->HDRInfo =
      pSensorDynamicInfo->PDAFInfo = CAM_TG_NONE;

  mScenarioId = pConfigParam->scenarioId;
  CAM_LOGE("pConfigParam->scenarioId %d\n", pConfigParam->scenarioId);

  switch (mScenarioId) {
    case SENSOR_SCENARIO_ID_NORMAL_CAPTURE:
      width = pImgsensorInfo->cap.grabwindow_width;
      height = pImgsensorInfo->cap.grabwindow_height;
      pix_clk = pImgsensorInfo->cap.pclk;
      line_length = pImgsensorInfo->cap.linelength;
      framelength = pImgsensorInfo->cap.framelength;
      break;
    case SENSOR_SCENARIO_ID_NORMAL_PREVIEW:
      width = pImgsensorInfo->pre.grabwindow_width;
      height = pImgsensorInfo->pre.grabwindow_height;
      pix_clk = pImgsensorInfo->pre.pclk;
      line_length = pImgsensorInfo->pre.linelength;
      framelength = pImgsensorInfo->pre.framelength;
      break;
    case SENSOR_SCENARIO_ID_NORMAL_VIDEO:
      width = pImgsensorInfo->normal_video.grabwindow_width;
      height = pImgsensorInfo->normal_video.grabwindow_height;
      pix_clk = pImgsensorInfo->normal_video.pclk;
      line_length = pImgsensorInfo->normal_video.linelength;
      framelength = pImgsensorInfo->normal_video.framelength;
      break;
    case SENSOR_SCENARIO_ID_SLIM_VIDEO1:
      width = pImgsensorInfo->hs_video.grabwindow_width;
      height = pImgsensorInfo->hs_video.grabwindow_height;
      pix_clk = pImgsensorInfo->hs_video.pclk;
      line_length = pImgsensorInfo->hs_video.linelength;
      framelength = pImgsensorInfo->hs_video.framelength;
      break;
    case SENSOR_SCENARIO_ID_SLIM_VIDEO2:
      width = pImgsensorInfo->slim_video.grabwindow_width;
      height = pImgsensorInfo->slim_video.grabwindow_height;
      pix_clk = pImgsensorInfo->slim_video.pclk;
      line_length = pImgsensorInfo->slim_video.linelength;
      framelength = pImgsensorInfo->slim_video.framelength;
      break;
    default:
      width = pImgsensorInfo->cap.grabwindow_width;
      height = pImgsensorInfo->cap.grabwindow_height;
      pix_clk = pImgsensorInfo->cap.pclk;
      line_length = pImgsensorInfo->cap.linelength;
      framelength = pImgsensorInfo->cap.framelength;
      break;
  }

  m_vblank = framelength - height;
  m_pixClk = pix_clk;
  m_linelength = line_length;
  m_framelength = framelength;
  m_LineTimeInus = (line_length * 1000000 + ((pix_clk / 1000) - 1)) /
                   (pix_clk / 1000);  // 1000 base , 33657 mean 33.657 us
  m_SensorGainFactor = pImgsensorInfo->SensorGainfactor;

  aFormat.pad = 0;
  aFormat.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  aFormat.format.width = width;
  aFormat.format.height = height;
  ret = ioctl(sensor_fd, VIDIOC_SUBDEV_S_FMT, &aFormat);
  if (ret < 0) {
    CAM_LOGE("set sensor format fail\n");
    return MFALSE;
  }
  // set seninf format the same as sensor format to avoid link invalid
  ret = ioctl(sensor_fd, VIDIOC_SUBDEV_G_FMT, &aFormat);
  if (ret < 0) {
    CAM_LOGE("get sensor format fail\n");
    return MFALSE;
  }

  aFormat.pad = sensorIdx;
  ret = ioctl(seninf_fd, VIDIOC_SUBDEV_S_FMT, &aFormat);
  if (ret < 0) {
    CAM_LOGE("set seninf format fail\n");
    return MFALSE;
  }

  /* send data to IPC sensor again */
  do {
    IHalSensor* pIPCSensor = createIPCHalSensorByIdx(mSensorIdx);
    if (CC_UNLIKELY(pIPCSensor == nullptr)) {
      CAM_LOGE("create IIPCHalSensor failed, sendCommand failed");
      break;
    }

    sendDataToIPCHalSensor(this, static_cast<IIPCHalSensor*>(pIPCSensor),
                           1 << mSensorIdx);
    pIPCSensor->destroyInstance();
  } while (0);

  return (ret == 0);
}

/******************************************************************************
 *
 ******************************************************************************/
MINT HalSensor::sendCommand(MUINT indexDual,
                            MUINTPTR cmd,
                            MUINTPTR arg1,
                            MUINT arg1_size,
                            MUINTPTR arg2,
                            MUINT arg2_size,
                            MUINTPTR arg3,
                            MUINT arg3_size) {
  MINT32 ret = 0;
  IMGSENSOR_SENSOR_IDX sensorIdx = IMGSENSOR_SENSOR_IDX_MAP(indexDual);
  int sensor_fd = HalSensorList::singleton()->querySensorFd(sensorIdx);
  v4l2_control control;
  unsigned int u32temp = 0, u32temp1 = 0;

  switch (cmd) {
    case SENSOR_CMD_GET_SENSOR_PIXELMODE:
      if ((reinterpret_cast<MUINT32*>(arg3) != NULL) &&
          (arg3_size == sizeof(MUINT32))) {
        *reinterpret_cast<MUINT32*>(arg3) = mSensorDynamicInfo.pixelMode;
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_SENSOR_POWER_ON_STATE:  // LSC funciton need open after
                                                // sensor Power On
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        *(reinterpret_cast<MUINT32*>(arg1)) =
            (mSensorIdx != IMGSENSOR_SENSOR_IDX_NONE) ? 1 << mSensorIdx : 0;
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_SENSOR_CROP_WIN_INFO:
      if (((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
           (arg1_size == sizeof(MUINT32))) &&
          ((reinterpret_cast<MUINT32*>(arg2) != NULL) &&
           (arg2_size == sizeof(SENSOR_WINSIZE_INFO_STRUCT)))) {
        SENSOR_WINSIZE_INFO_STRUCT* ptr;
        ptr = HalSensorList::singleton()->getWinSizeInfo(
            sensorIdx, *reinterpret_cast<MUINT32*>(arg1));
        if (ptr) {
          memcpy(reinterpret_cast<void*>(arg2), reinterpret_cast<void*>(ptr),
                 sizeof(SENSOR_WINSIZE_INFO_STRUCT));
        }
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_SET_MAX_FRAME_RATE_BY_SCENARIO:
      if ((reinterpret_cast<MUINT32*>(arg2) != NULL) &&
          (arg2_size == sizeof(MUINT32))) {
        u32temp = *reinterpret_cast<MUINT32*>(arg2);  // get framerate is 10x,
                                                      // namely 100 for 10 fps
        u32temp = ((1000 * 1000000) / u32temp / m_LineTimeInus) * 10;
        mFramerate = u32temp;
        control.id = V4L2_CID_VBLANK;
        control.value = (u32temp > m_framelength)
                            ? (u32temp - m_framelength + m_vblank)
                            : m_vblank;
        ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
          CAM_LOGE("[%s] set max framerate fail %d\n", __FUNCTION__,
                   control.value);
        }
        CAM_LOGE("set max framerate %d, mFramerate %d control.value %d\n",
                 *(MUINT32*)arg2, mFramerate, control.value);
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_SET_SENSOR_GAIN:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        control.id = V4L2_CID_ANALOGUE_GAIN;
        u32temp = *reinterpret_cast<MUINT32*>(arg1);
        control.value = u32temp >> m_SensorGainFactor;
        CAM_LOGD("SENSOR_GAIN %d(%d) m_SensorGainFactor %d\n", control.value,
                 u32temp, m_SensorGainFactor);
        ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
          CAM_LOGE("[%s] set SENSOR GAIN fail %d\n", __FUNCTION__,
                   control.value);
        }
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_SET_SENSOR_EXP_TIME:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        u32temp = *reinterpret_cast<MUINT32*>(arg1);
        u32temp = ((1000 * (u32temp)) / m_LineTimeInus);
        u32temp1 = (u32temp > mFramerate) ? u32temp : mFramerate;
        control.id = V4L2_CID_VBLANK;
        control.value = (u32temp1 > m_framelength)
                            ? (u32temp1 - m_framelength + m_vblank)
                            : m_vblank;
        ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
          CAM_LOGE("[%s] set SENSOR VBLANK fail %d\n", __FUNCTION__,
                   control.value);
        }
        control.id = V4L2_CID_EXPOSURE;
        control.value = u32temp;
        CAM_LOGD("EXP_TIME %d(%d) m_LineTimeInus %d vblank %d\n", u32temp,
                 *(MUINT32*)arg1, m_LineTimeInus, m_vblank);
        ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
          CAM_LOGE("[%s] set SENSOR EXPOSURE fail %d\n", __FUNCTION__,
                   control.value);
        }
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_PIXEL_CLOCK_FREQ:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        *reinterpret_cast<MUINT32*>(arg1) = m_pixClk;
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_FRAME_SYNC_PIXEL_LINE_NUM:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        *reinterpret_cast<MUINT32*>(arg1) =
            (m_framelength << 16) + m_linelength;
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_SET_TEST_PATTERN_OUTPUT:
      if ((reinterpret_cast<MUINT32*>(arg1) != NULL) &&
          (arg1_size == sizeof(MUINT32))) {
        u32temp = *reinterpret_cast<MUINT32*>(arg1);
        // api color bar arg is 2, but sensor driver color bar index is 1
        control.value = u32temp - 1;
        if (control.value < 0) {
          CAM_LOGE("[%s] invalid pattern mode %d\n", __FUNCTION__,
                   control.value);
          break;
        }
        control.id = V4L2_CID_TEST_PATTERN;
        ret = ioctl(sensor_fd, VIDIOC_S_CTRL, &control);
        if (ret < 0) {
          CAM_LOGE("[%s] set SENSOR TEST PATTERN fail 0x%x\n", __FUNCTION__,
                   control.value);
        }
      } else {
        CAM_LOGE("%s(0x%x) wrong input params\n", __FUNCTION__, cmd);
        ret = MFALSE;
      }
      break;

    case SENSOR_CMD_GET_SENSOR_VC_INFO:
    case SENSOR_CMD_GET_SENSOR_PDAF_INFO:
    case SENSOR_CMD_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
    case SENSOR_CMD_GET_SENSOR_PDAF_CAPACITY:
    case SENSOR_CMD_GET_SENSOR_ROLLING_SHUTTER:
    case SENSOR_CMD_GET_VERTICAL_BLANKING:
      CAM_LOGD("TODO sendCommand(0x%x)\n", cmd);
      ret = MFALSE;
      break;

    default:
      CAM_LOGE("Unsupported sendCommand(0x%x)\n", cmd);
      ret = MFALSE;
      break;
  }

  return ret;
}

/*******************************************************************************
 *
 ******************************************************************************/
MINT32 HalSensor::setDebugInfo(IBaseCamExif* pIBaseCamExif) {
  (void)pIBaseCamExif;
  return 0;
}
MINT32 HalSensor::reset() {
  return 0;
}

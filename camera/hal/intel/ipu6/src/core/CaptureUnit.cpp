/*
 * Copyright (C) 2015-2020 Intel Corporation.
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

#define LOG_TAG "CaptureUnit"

#include <poll.h>

#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "iutils/Utils.h"

#include "PlatformData.h"
#include "MediaControl.h"
#include "CaptureUnit.h"

using std::vector;
using std::map;

namespace icamera {

CaptureUnit::CaptureUnit(int cameraId, int memType) :
    StreamSource(memType),
    mCameraId(cameraId),
    mDevice(nullptr),
    mMaxBufferNum(PlatformData::getMaxRawDataNum(cameraId)),
    mState(CAPTURE_UNINIT),
    mExitPending(false)
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    mPollThread = new PollThread(this);

    mMaxBuffersInDevice = PlatformData::getExposureLag(mCameraId) + 1;
    if (mMaxBuffersInDevice < 2) {
        mMaxBuffersInDevice = 2;
    }
}

CaptureUnit::~CaptureUnit()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    delete mPollThread;
}

int CaptureUnit::init()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    mState = CAPTURE_INIT;

    return OK;
}

void CaptureUnit::deinit()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    if (mState == CAPTURE_UNINIT) {
        LOG1("%s: deinit without init", __func__);
        return;
    }

    destroyDevices();
    mPollThread->join();

    mState = CAPTURE_UNINIT;
}

int CaptureUnit::createDevices()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    destroyDevices();

    // Default INVALID_PORT means the device isn't associated with any outside consumers.
    const Port kDefaultPort = INVALID_PORT;
    Port portOfMainDevice = findDefaultPort(mOutputFrameInfo);
    // Use the config for main port as the default one.
    const stream_t& kDefaultStream = mOutputFrameInfo.at(portOfMainDevice);

    // Use VIDEO_GENERIC by default.
    VideoNodeType nodeType = VIDEO_GENERIC;

    mDevice = new MainDevice(mCameraId, nodeType, this);

    // Open and configure the device. The stream and port that are used by the device is
    // decided by whether consumer has provided such info, use the default one if not.
    int ret = mDevice->openDevice();
    CheckError(ret != OK, ret, "Open device(%s) failed:%d", mDevice->getName(), ret);

    bool hasPort = mOutputFrameInfo.find(portOfMainDevice) != mOutputFrameInfo.end();
    const stream_t& stream = hasPort ? mOutputFrameInfo.at(portOfMainDevice) : kDefaultStream;

    ret = mDevice->configure(hasPort ? portOfMainDevice : kDefaultPort, stream, mMaxBufferNum);
    CheckError(ret != OK, ret, "Configure device(%s) failed:%d", mDevice->getName(), ret);

    return OK;
}

void CaptureUnit::destroyDevices()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    if (mDevice) {
        mDevice->closeDevice();
        delete mDevice;
        mDevice = nullptr;
    }

}

/**
 * Find the device that can handle the given port.
 */
DeviceBase* CaptureUnit::findDeviceByPort(Port port)
{
    if (mDevice && mDevice->getPort() == port) {
        return mDevice;
    }

    return nullptr;
}

int CaptureUnit::streamOn()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    if (mDevice) {
        int ret = mDevice->streamOn();
        CheckError(ret < 0, INVALID_OPERATION, "Device:%s stream on failed.", mDevice->getName());
    }

    return OK;
}

int CaptureUnit::start()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    AutoMutex l(mLock);
    CheckWarning(mState == CAPTURE_START, OK, "@%s: device already started", __func__);

    int ret = streamOn();
    if (ret != OK) {
        streamOff();
        LOGE("Devices stream on failed:%d", ret);
        return ret;
    }

    mPollThread->run("CaptureUnit", PRIORITY_URGENT_AUDIO);
    mState = CAPTURE_START;
    mExitPending = false;
    LOG2("@%s: automation checkpoint: flag: poll_started", __func__);

    return OK;
}

void CaptureUnit::streamOff()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    if (mDevice) {
        mDevice->streamOff();
    }
}

int CaptureUnit::stop()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    if (mState != CAPTURE_START) {
        LOGW("@%s: device not started", __func__);
        return OK;
    }

    mExitPending = true;
    mPollThread->requestExit();
    streamOff();
    mPollThread->requestExitAndWait();

    AutoMutex   l(mLock);
    mState = CAPTURE_STOP;

    if (mDevice) {
        mDevice->resetBuffers();
    }
    LOG2("@%s: automation checkpoint: flag: poll_stopped", __func__);

    mExitPending = false; // It's already stopped.

    return OK;
}

/**
 * Check if the given outputFrames are different from the previous one.
 * Only return false when the config for each port is exactly same.
 */
bool CaptureUnit::isNewConfiguration(const map<Port, stream_t>& outputFrames)
{
    for (const auto& item : outputFrames) {
        if (mOutputFrameInfo.find(item.first) == mOutputFrameInfo.end()) {
            return true;
        }

        const stream_t& oldStream = mOutputFrameInfo[item.first];
        const stream_t& newStream = item.second;

        bool isNewConfig = (oldStream.width != newStream.width || oldStream.height != newStream.height
               || oldStream.format != newStream.format || oldStream.field != newStream.field
               || oldStream.memType != newStream.memType);
        if (isNewConfig) {
            return true;
        }
    }

    return false;
}

int CaptureUnit::configure(const map<Port, stream_t>& outputFrames,
                           const vector<ConfigMode>& configModes)
{
    PERF_CAMERA_ATRACE();

    CheckError(outputFrames.empty(), BAD_VALUE, "No frame info configured.");
    CheckError(mState != CAPTURE_CONFIGURE && mState != CAPTURE_INIT && mState != CAPTURE_STOP,
          INVALID_OPERATION, "@%s: Configure in wrong state %d", __func__, mState);

    Port port = findDefaultPort(outputFrames);
    const stream_t& mainStream = outputFrames.at(port);

    for (const auto& item : outputFrames) {
        LOG1("%s, mCameraId:%d, port:%d, w:%d, h:%d, f:%s", __func__, mCameraId, item.first,
              item.second.width, item.second.height,
              CameraUtils::format2string(item.second.format).c_str());
    }

    mConfigModes = configModes;
    mOutputFrameInfo = outputFrames;

    /* media ctl setup */
    MediaCtlConf *mc = PlatformData::getMediaCtlConf(mCameraId);
    CheckError(!mc, BAD_VALUE, "get format configuration failed for %s (%dx%d)",
               CameraUtils::format2string(mainStream.format).c_str(),
               mainStream.width, mainStream.height);

    int status = MediaControl::getInstance()->mediaCtlSetup(mCameraId, mc,
            mainStream.width, mainStream.height, mainStream.field);
    CheckError(status != OK, status, "set up mediaCtl failed");

    // Create, open, and configure all of needed devices.
    status = createDevices();
    CheckError(status != OK, status, "Create devices failed:%d", status);

    mState = CAPTURE_CONFIGURE;

    // mExitPending should also be set false in configure to make buffers queued before start
    mExitPending = false;

    return OK;
}

Port CaptureUnit::findDefaultPort(const map<Port, stream_t>& frames) const
{
    Port availablePorts[] = {MAIN_PORT, SECOND_PORT, THIRD_PORT, FORTH_PORT};
    for (unsigned int i = 0; i < ARRAY_SIZE(availablePorts); i++) {
        if (frames.find(availablePorts[i]) != frames.end()) {
            return availablePorts[i];
        }
    }
    return INVALID_PORT;
}

int CaptureUnit::allocateMemory(Port port, const std::shared_ptr<CameraBuffer> &camBuffer)
{
    const struct v4l2_buffer* v = camBuffer->getV4L2Buffer().Get();
    CheckError(v->index >= mMaxBufferNum, -1
        ,"index %d is larger than max count %d", v->index, mMaxBufferNum);
    CheckError(v->memory != V4L2_MEMORY_MMAP, -1
        ,"Allocating Memory Capture device only supports MMAP mode.");

    DeviceBase* device = findDeviceByPort(port);
    CheckError(!device, BAD_VALUE, "No device available for port:%d", port);

    int ret = camBuffer->allocateMemory(device->getV4l2Device());
    CheckError(ret < 0, ret, "Failed to allocate memory ret(%d) for port:%d", ret, port);

    return OK;
}

int CaptureUnit::qbuf(Port port, const std::shared_ptr<CameraBuffer> &camBuffer)
{
    CheckError(camBuffer == nullptr, BAD_VALUE, "Camera buffer is null");
    CheckError((mState == CAPTURE_INIT || mState == CAPTURE_UNINIT), INVALID_OPERATION,
          "@%s: qbuf in wrong state %d", __func__, mState);

    DeviceBase* device = findDeviceByPort(port);
    CheckError(!device, BAD_VALUE, "No device available for port:%d", port);

    LOG2("@%s, mCameraId:%d, queue CameraBuffer: %p to port:%d",
         __func__, mCameraId, camBuffer.get(), port);

    device->addPendingBuffer(camBuffer);

    return processPendingBuffers();
}

int CaptureUnit::queueAllBuffers()
{
    PERF_CAMERA_ATRACE();

    if (mExitPending) return OK;

    if (mDevice) {
        int ret = mDevice->queueBuffer(-1);
        if (mExitPending) return OK;
        CheckError(ret != OK, ret, "queueBuffer fails, dev:%s, ret:%d", mDevice->getName(), ret);
        mDevice->getPredictSequence();
    }

    return OK;
}

void CaptureUnit::onDequeueBuffer()
{
    processPendingBuffers();
}

int CaptureUnit::processPendingBuffers()
{
    if (mDevice && mDevice->getBufferNumInDevice() < mMaxBuffersInDevice) {
        LOG2("%s: buffers in device:%d", __func__, mDevice->getBufferNumInDevice());

        if (!mDevice->hasPendingBuffer()) {
            return OK;
        }

        int ret = queueAllBuffers();
        if (mExitPending) return OK;
        CheckError(ret != OK, ret, "Failed to queue buffers, ret=%d", ret);
    }

    return OK;
}

int CaptureUnit::poll()
{
    PERF_CAMERA_ATRACE();
    int ret = 0;
    const int poll_timeout_count = 10;
    const int poll_timeout = gSlowlyRunRatio ? (gSlowlyRunRatio * 1000000) : 1000;

    LOG2("@%s, mCameraId:%d", __func__, mCameraId);

    CheckError((mState != CAPTURE_CONFIGURE && mState != CAPTURE_START), INVALID_OPERATION,
          "@%s: poll buffer in wrong state %d", __func__, mState);

    int timeOutCount = poll_timeout_count;

    std::vector<V4L2Device*> pollDevs, readyDevices;
    if (mDevice) {
        pollDevs.push_back(mDevice->getV4l2Device());
        LOG2("@%s: device:%s has %d buffers queued.", __func__,
             mDevice->getName(), mDevice->getBufferNumInDevice());
    }

    while (timeOutCount-- && ret == 0) {
        // If stream off, no poll needed.
        if (mExitPending) {
            LOG2("%s: mExitPending is true, exit", __func__);
            //Exiting, no error
            return -1;
        }

        V4L2DevicePoller poller {pollDevs, -1};
        ret = poller.Poll(poll_timeout, POLLPRI | POLLIN | POLLOUT | POLLERR, &readyDevices);

        LOG2("@%s: automation checkpoint: flag: poll_buffer, ret:%d", __func__, ret);
    }

    //In case poll error after stream off
    if (mExitPending) {
        LOG2("%s: mExitPending is true, exit", __func__);
        //Exiting, no error
        return -1;
    }

    CheckError(ret < 0, UNKNOWN_ERROR, "%s: Poll error, ret:%d", __func__, ret);

    if (ret == 0) {
        LOG1("%s, cameraId: %d: timeout happens, wait recovery", __func__, mCameraId);
        return OK;
    }

    for (const auto& readyDevice : readyDevices) {
        if (mDevice && mDevice->getV4l2Device() == readyDevice) {
            int ret = mDevice->dequeueBuffer();
            if (mExitPending) return -1;

            if (ret != OK) {
                LOGE("Device:%s grab frame failed:%d", mDevice->getName(), ret);
            }
            break;
        }
    }

    return OK;
}

void CaptureUnit::addFrameAvailableListener(BufferConsumer *listener)
{
    LOG1("%s camera id:%d", __func__, mCameraId);

    AutoMutex   l(mLock);
    if (mDevice) {
        mDevice->addFrameListener(listener);
    }
}

void CaptureUnit::removeFrameAvailableListener(BufferConsumer *listener)
{
    LOG1("%s camera id:%d", __func__, mCameraId);

    AutoMutex   l(mLock);
    if (mDevice) {
        mDevice->removeFrameListener(listener);
    }
}

void CaptureUnit::removeAllFrameAvailableListener()
{
    LOG1("%s camera id:%d", __func__, mCameraId);

    AutoMutex   l(mLock);
    if (mDevice) {
        mDevice->removeAllFrameListeners();
    }
}

void CaptureUnit::registerListener(EventType eventType, EventListener* eventListener)
{
    if (mDevice) {
        mDevice->registerListener(eventType, eventListener);
    }
}

void CaptureUnit::removeListener(EventType eventType, EventListener* eventListener)
{
    if (mDevice) {
        mDevice->removeListener(eventType, eventListener);
    }
}
} // namespace icamera


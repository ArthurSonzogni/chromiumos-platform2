/*
 * Copyright (C) 2017 Intel Corporation.
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

#include <utils/Errors.h>
#include <vector>

#include "v4l2device.h"
#include "MessageQueue.h"
#include "MessageThread.h"
#include "CaptureBuffer.h"
#include "CaptureUnitSettings.h"
#include "IPU3CapturedStatistics.h"
#include "GraphConfig.h"
#include "ItemPool.h"
#include "InputSystem.h"
#include "SyncManager.h"
#include "LensHw.h"
#include <linux/intel-ipu3.h>

#ifndef __CAPTURE_UNIT_H__
#define __CAPTURE_UNIT_H__

namespace android {
namespace camera2 {

class Camera3Request;
class IStreamConfigProvider;
class BufferPools;
class SettingsProcessor;

static const uint8_t DEFAULT_PIPELINE_DEPTH = 4;

/**
 * \class ICaptureEventListener
 *
 * Abstract interface implemented by entities interested on receiving notifications
 * from the input system.
 *
 * Notifications are sent for AF/2A statistics, histogram and RAW frames.
 */
class ICaptureEventListener {
public:

    enum CaptureMessageId {
        CAPTURE_MESSAGE_ID_EVENT = 0,
        CAPTURE_MESSAGE_ID_ERROR
    };

    enum CaptureEventType {
        CAPTURE_EVENT_MIPI_COMPRESSED = 0,
        CAPTURE_EVENT_MIPI_UNCOMPRESSED,
        CAPTURE_EVENT_RAW_BAYER,
        CAPTURE_EVENT_RAW_BAYER_SCALED,
        CAPTURE_EVENT_AF_STATISTICS,
        CAPTURE_EVENT_2A_STATISTICS,
        CAPTURE_EVENT_AE_HISTOGRAM,
        CAPTURE_EVENT_NEW_SENSOR_DESCRIPTOR,
        CAPTURE_EVENT_NEW_SOF,
        CAPTURE_EVENT_SHUTTER,
        CAPTURE_EVENT_YUV,
        CAPTURE_EVENT_MAX
    };
    // Buffers output from CaptureUnit
    struct CaptureBuffers {
        std::shared_ptr<CaptureBuffer>   rawBuffer;
        std::shared_ptr<CaptureBuffer>   rawNonScaledBuffer;
    };

    // For MESSAGE_ID_EVENT
    struct CaptureMessageEvent {
        CaptureEventType type;
        struct timeval   timestamp;
        unsigned int     sequence;
        std::shared_ptr<CaptureBuffer> pixelBuffer;   /**< Single buffer for output from CaptureUnit. To be identified by CaptureEventType  */
        std::shared_ptr<IPU3CapturedStatistics> stats;
        std::shared_ptr<ipu3_uapi_params> param;
        std::shared_ptr<CameraBuffer> yuvBuffer;
        unsigned int     reqId;
        ia_aiq_exposure_sensor_descriptor exposureDesc;
        ia_aiq_frame_params frameParams;
        CaptureMessageEvent() : type(CAPTURE_EVENT_MAX),
            sequence(0),
            pixelBuffer(nullptr),
            reqId(0)
        {
            CLEAR(timestamp);
            CLEAR(exposureDesc);
            CLEAR(frameParams);
        }
    };

    // For MESSAGE_ID_ERROR
    struct CaptureMessageError {
        status_t code;
        CaptureMessageError() : code(CAPTURE_MESSAGE_ID_ERROR) {}
    };

    struct CaptureMessageData {
        CaptureMessageEvent event;
        CaptureMessageError error;
    };

    struct CaptureMessage {
        CaptureMessageId   id;
        CaptureMessageData data;
        CaptureMessage() : id(CAPTURE_MESSAGE_ID_ERROR) {}
    };

    virtual bool notifyCaptureEvent(CaptureMessage *msg) = 0;
    virtual ~ICaptureEventListener() {}

}; // ICaptureEventListener


class CaptureUnit :
    public IMessageHandler,
    public IISysObserver,
    public ISofListener
{
// public methods
public:
    CaptureUnit(int camId, IStreamConfigProvider &aStreamCfgProv, std::shared_ptr<MediaController> mc);
    ~CaptureUnit();

    status_t init();
    void setSettingsProcessor(SettingsProcessor *settingsProcessor);

    status_t flush();
    status_t configStreams(std::vector<camera3_stream_t*> &activeStreams,
                           bool configChanged);

    status_t doCapture(Camera3Request* request,
                       std::shared_ptr<CaptureUnitSettings>  &aiqCaptureSettings,
                       std::shared_ptr<GraphConfig> graphConfig);
    LensHw* getLensControlInterface();

    /* IMessageHandler overloads */
    virtual void messageThreadLoop(void);

    /* IISysObserver interface */
    virtual void notifyIsysEvent(IsysMessage &msg);

    virtual uint8_t getPipelineDepth() { return mPipelineDepth; }

    /************************************************************
     * Listener management methods
     */
    status_t attachListener(ICaptureEventListener *aListener);
    void cleanListeners();

    /* ISofListener interface */
    virtual bool notifySofEvent(uint32_t sequence);
private:
    // thread message IDs
    enum MessageId {
        MESSAGE_ID_EXIT = 0,
        MESSAGE_ID_CAPTURE,
        MESSAGE_ID_CONFIGSTREAM,
        MESSAGE_ID_GET_SCALED_OUTPUT_CONFIG,
        MESSAGE_ID_GET_NON_SCALED_OUTPUT_CONFIG,
        MESSAGE_ID_FLUSH,
        MESSAGE_ID_RETURN_BUFFER,
        MESSAGE_ID_ISYS_EVENT,
        MESSAGE_ID_PSYS_EVENT,
        MESSAGE_ID_MAX
    };

    struct MessageSensorData {
        ia_aiq_exposure_sensor_descriptor *descriptor;

        MessageSensorData() : descriptor(nullptr) {}
    };

    /**
     * Similar state structure for a request than the one in control unit.
     * It is stored in a pool.
     */
    struct  InflightRequestState {
        Camera3Request *request;
        std::shared_ptr<CaptureUnitSettings> aiqCaptureSettings;
        std::shared_ptr<GraphConfig> graphConfig;
        uint32_t mediaRequestId;
        bool shutterDone;

        static void reset(InflightRequestState *me) {
            me->aiqCaptureSettings.reset();
            me->graphConfig.reset();
            me->mediaRequestId = 0;
            me->request = nullptr;
        }

        InflightRequestState() :
            request(nullptr),
            mediaRequestId(0),
            shutterDone(0) {}

        ~InflightRequestState() {
            aiqCaptureSettings = nullptr;
            graphConfig = nullptr;
        }
    };

    struct MessageRequest {
        std::shared_ptr<InflightRequestState> inFlightRequest;
    };

    struct MessageBuffer {
        CaptureBuffer* captureBufPtr;
        struct v4l2_buffer_info v4l2Buf;
        IPU3NodeNames isysNodeName;
        int requestId;

        MessageBuffer() :
            captureBufPtr(nullptr),
            isysNodeName(IMGU_NODE_NULL),
            requestId(-999)
        {
            CLEAR(v4l2Buf);
        }
    };

    struct MessageConfig {
        bool configChanged;
        std::vector<camera3_stream_t*> *activeStreams; /* IPU3 Camera Hw has the ownership */

        MessageConfig() :
            configChanged(false),
            activeStreams(nullptr) {}
    };

    struct MessageOutputConfig {
        FrameInfo *frameInfo; // output parameter

        MessageOutputConfig() :
            frameInfo(nullptr) {}
    };

    struct MessageMediaInfo {
        media_device_info *mediaInfo;

        MessageMediaInfo() :
            mediaInfo(nullptr) {}
    };

    struct MessageData {
        MessageSensorData sensorData;
        MessageRequest request;
        MessageBuffer  buffer;
        MessageOutputConfig outputConfig;
        MessageConfig config;
        MessageMediaInfo info;

        MessageData() {};
    };

    struct Message {
        MessageId id;
        MessageData data;

        Message() :
            id(MESSAGE_ID_MAX) {}
    };

private:
    status_t handleMessageFlush(Message &msg);
    status_t handleMessageConfigStreams(Message &msg);
    status_t handleMessageCapture(Message &msg);
    status_t handleMessageIsysEvent(Message &msg);

    status_t processIsysBuffer(Message &msg);

    status_t notifyListeners(ICaptureEventListener::CaptureMessage *msg);

    status_t getSensorModeData(ia_aiq_exposure_sensor_descriptor &desc);

    status_t setSensorFrameTimings();

    status_t enqueueBuffers(std::shared_ptr<InflightRequestState> &reqState, bool skip);
    status_t enqueueIsysBuffer(std::shared_ptr<InflightRequestState> &reqState, bool skip);
    int getActiveIsysNodes(std::shared_ptr<GraphConfig> graphConfig);
    status_t issueSkips(int count, bool buffers, bool settings, bool isys);
    status_t applyAeParams(std::shared_ptr<CaptureUnitSettings> &aiqCaptureSettings);
    status_t initStaticMetadata();

private:
    friend class std::shared_ptr<InflightRequestState>;
    int     mCameraId;
    int mActiveIsysNodes; /**< A bitmask value records the IPU3NodeNames of all active ISYS nodes */

    std::shared_ptr<MediaController>             mMediaCtl;

    /**
     * Thread control members
     */
    bool mThreadRunning;
    MessageQueue<Message, MessageId> mMessageQueue;
    std::unique_ptr<MessageThread> mMessageThread;
    /*
     * Stream config provider
     */
    IStreamConfigProvider &mStreamCfgProvider;
    std::vector<camera3_stream_t *>      mActiveStreams; /* mActiveStreams doesn't own camera3_stream_t objects */

    /* Input system event listeners */
    std::vector<ICaptureEventListener*> mListeners; /* mListeners doesn't own ICaptureEventListener objects */

    BufferPools *mBufferPools;

    SettingsProcessor *mSettingProcessor; /* CaptureUnit doesn't own mSettingProcessor */
    uint8_t mPipelineDepth;

    std::shared_ptr<InputSystem>                 mIsys;
    std::shared_ptr<SyncManager>    mSyncManager;

    /* Queue of requests */
    std::map<int, std::shared_ptr<InflightRequestState>> mInflightRequests;
    SharedItemPool<InflightRequestState>  mInflightRequestPool;
    std::shared_ptr<InflightRequestState> mLastInflightRequest;

    std::vector<int32_t> mSkipRequestIdQueue; /**< Queue of skip request Id */

    std::vector<std::shared_ptr<CaptureBuffer>> mQueuedCaptureBuffers;

    int mSensorSettingsDelay;
    int mGainDelay;
    bool mLensSupported;
    std::shared_ptr<LensHw> mLensController;

    /**
     * map to link each of the ISYS nodes to a concrete destination port.
     * The uid is the terminal id of the peer port.
     * ex:
     *  ISYS_NODE_ISA_OUT is in graph config the port named
     *    isa:non_scaled_output
     * In a particular configuration this port may be linked the port of the
     * video or any other new pipe. This input port is referred as peer port.
     * Each port in graph config has a terminal ID.
     * We store in this map the terminal id of the peer port of the ISA ports.
     * This map is re-generated on every stream reconfiguration.
     */
    std::map<IPU3NodeNames, uid_t> mNodeToPortMap;
};

} // namespace camera2
} // namespace android

#endif // __CAPTURE_UNIT_H__

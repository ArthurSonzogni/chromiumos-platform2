/*
 * Copyright (C) 2019-2020 Intel Corporation.
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

#pragma once

#include "FaceBase.h"
#include "modules/sandboxing/IPCCommon.h"

namespace icamera {
class IPCIntelFD {
 public:
    IPCIntelFD();
    virtual ~IPCIntelFD();

    bool clientFlattenInit(unsigned int max_face_num, int cameraId,
                           FaceDetectionInitParams* params);
    bool serverUnflattenRun(const FaceDetectionRunParams& inParams, void* imageData,
                            pvl_image* image, int* cameraId);
};
} /* namespace icamera */

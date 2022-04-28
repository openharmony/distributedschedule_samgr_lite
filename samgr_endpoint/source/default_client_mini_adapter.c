/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "default_client_adapter.h"
#include "samgr_server.h"
#include "dbinder_service.h"
static pthread_mutex_t g_handleMutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_handle = 0;

static int32_t GetNextHandle(void)
{
    pthread_mutex_lock(&g_handleMutex);
    int32_t handle = ++g_handle;
    pthread_mutex_unlock(&g_handleMutex);
    return handle;
}

uintptr_t GetRemoteSaIdInner(const char *service, const char *feature)
{
    SaNode *saNode = GetSaNodeBySaName(service, feature);
    if (saNode != NULL) {
        return saNode->saId;
    }
    return 0;
}

void ProxyInvokeArgInner(IpcIo *reply, IClientHeader *header)
{
    WriteInt32(reply, (int32_t)header->saId);
}

SvcIdentity QueryRemoteIdentityInner(const char *deviceId, const char *service, const char *feature)
{
    char saName[2 * MAX_NAME_LEN + 2];
    (void)sprintf_s(saName, 2 * MAX_NAME_LEN + 2, "%s#%s", service?service:"", feature?feature:"");
    HILOG_INFO(HILOG_MODULE_SAMGR, "saName %s, make remote binder start", saName);

    SvcIdentity target = {INVALID_INDEX, INVALID_INDEX, INVALID_INDEX};
    SaNode *saNode = GetSaNodeBySaName(service, feature);
    if (saNode == NULL) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "service: %s feature: %s have no saId in sa map", service, feature);
        return target;
    }
    int32_t ret = MakeRemoteBinder(saName, strlen(saName), deviceId, strlen(deviceId), saNode->saId, 0,
                                   &target);
    if (ret != EC_SUCCESS) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "MakeRemoteBinder failed");
        return target;
    }
    target.handle = GetNextHandle();
    extern void WaitForProxyInit(SvcIdentity *svc);
    WaitForProxyInit(&target);
    HILOG_ERROR(HILOG_MODULE_SAMGR, "MakeRemoteBinder sid handle=%d", target.handle);
    return target;
}
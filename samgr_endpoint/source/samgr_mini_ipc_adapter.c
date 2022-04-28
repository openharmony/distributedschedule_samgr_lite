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
#include "samgr_ipc_adapter.h"
#include "samgr_server.h"
#include "dbinder_service.h"
static int Dispatch(uint32_t code, IpcIo *data, IpcIo *reply, MessageOption option);
static void HandleIpc(const Request *request, const Response *response);
static void GetRemotePolicy(IpcIo *reply, PolicyTrans **policy, uint32 *policyNum);
int ClientRegisterRemoteEndpoint(SvcIdentity *identity, int token, const char *service, const char *feature)
{
    IpcObjectStub *objectStubOne = (IpcObjectStub *)calloc(1, sizeof(IpcObjectStub));
    if (objectStubOne == NULL) {
        return -1;
    }
    objectStubOne->func = Dispatch;
    objectStubOne->isRemote = true;
    // handle is used by rpc, should be bigger than 0
    identity->handle = token + 1;
    identity->cookie = objectStubOne;
    // token is used by router index, should be itself, and save in SaNode
    identity->token = token;
    extern int AddEndpoint(SvcIdentity identity, const char *service, const char *feature);
    return AddEndpoint(*identity, service, feature);
}

void Listen(Endpoint *endpoint, int token, const char *service, const char *feature)
{
    endpoint->registerEP(&endpoint->identity, token, service, feature);
}

static int Dispatch(uint32_t code, IpcIo *data, IpcIo *reply, MessageOption option)
{
    extern RemoteRegister g_remoteRegister;
    Endpoint *endpoint = g_remoteRegister.endpoint;
    int token = GetRemoteToken(data);
    if (token == EC_INVALID) {
        goto ERROR;
    }
    if (TB_CheckMessage(&endpoint->bucket) == BUCKET_BUSY) {
        HILOG_WARN(HILOG_MODULE_SAMGR, "Flow Control <%u> is NULL", token);
        goto ERROR;
    }

    Router *router = VECTOR_At(&endpoint->routers, token);
    if (router == NULL) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "Router <%s, %u> is NULL", endpoint->name, token);
        goto ERROR;
    }
    Response resp = {0};
    resp.data = endpoint;
    Request request = {0};
    request.msgId = token;
    request.data = data;
    resp.reply = reply;
    request.msgValue = code;
    uint32 *ref = NULL;
    int ret = SAMGR_SendSharedDirectRequest(&router->identity, &request, &resp, &ref, HandleIpc);
    if (ret != EC_SUCCESS) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "Router[%u] Service<%d, %d> is busy",
                    token, router->identity.serviceId, router->identity.featureId);
        goto ERROR;
    }

    return EC_SUCCESS;
ERROR:
    return EC_INVALID;
}

static void HandleIpc(const Request *request, const Response *response)
{
    Endpoint *endpoint = (Endpoint *)response->data;
    Router *router = VECTOR_At(&endpoint->routers, request->msgId);

    router->proxy->Invoke(router->proxy, request->msgValue, NULL, request->data, response->reply);
}

int RegisterIdentity(const SaName *saName, SvcIdentity *saInfo, PolicyTrans **policy, uint32 *policyNum)
{
    IpcIo req;
    uint8 data[MAX_DATA_LEN];
    IpcIoInit(&req, data, MAX_DATA_LEN, 0);
    WriteUint32(&req, RES_FEATURE);
    WriteUint32(&req, OP_PUT);
    WriteString(&req, saName->service);
    WriteBool(&req, saName->feature == NULL);
    if (saName->feature != NULL) {
        WriteBool(&req, saName->feature);
    }
    WriteUint32(&req, saInfo->token);
    IpcIo reply;
    void *replyBuf = NULL;
    SvcIdentity *samgr = GetContextObject();
    MessageOption option;
    MessageOptionInit(&option);
    int ret = SendRequest(*samgr, INVALID_INDEX, &req, &reply, option,
                          (uintptr_t *)&replyBuf);
    ret = -ret;
    int32_t ipcRet = EC_FAILURE;
    if (ret == EC_SUCCESS) {
        ret = ReadInt32(&reply, &ipcRet);
    }
    if (ipcRet == EC_SUCCESS) {
        SvcIdentity target;
        (void)ReadRemoteObject(&reply, &target);
        GetRemotePolicy(&reply, policy, policyNum);
    }
    if (replyBuf != NULL) {
        FreeBuffer(replyBuf);
    }
    return ret;
}

int GetRemoteToken(IpcIo *data)
{
    uintptr_t saId = 0;
    ReadInt32(data, &saId);
    SaNode *saNode = GetSaNodeBySaId(saId);
    if (saNode == NULL) {
        HILOG_WARN(HILOG_MODULE_SAMGR, "get sa node by sa id %d is NULL", saId);
        return EC_INVALID;
    }
    return saNode->token;
}

static void GetRemotePolicy(IpcIo *reply, PolicyTrans **policy, uint32 *policyNum)
{
    if (reply == NULL) {
        return;
    }
    ReadUint32(reply, policyNum);
    if (*policyNum > MAX_POLICY_NUM) {
        *policyNum = MAX_POLICY_NUM;
    }
    SAMGR_Free(*policy);
    if (*policyNum == 0) {
        *policy = NULL;
        return;
    }
    *policy = (PolicyTrans *)SAMGR_Malloc(sizeof(PolicyTrans) * (*policyNum));
    if (*policy == NULL) {
        return;
    }
    for (uint32 i = 0; i < *policyNum; i++) {
        if (ReadInt32(reply, &(*policy)[i].type)) {
            switch ((*policy)[i].type) {
                case RANGE:
                    ReadInt32(reply, &((*policy)[i].uidMin));
                    ReadInt32(reply, &((*policy)[i].uidMax));
                    break;
                case FIXED:
                    for (uint32 j = 0; j < UID_SIZE; j++) {
                        ReadInt32(reply, &((*policy)[i].fixedUid[j]));
                    }
                    break;
                case BUNDLENAME:
                    ReadInt32(reply, &((*policy)[i].fixedUid[0]));
                    break;
                default:
                    break;
            }
        }
    }
}
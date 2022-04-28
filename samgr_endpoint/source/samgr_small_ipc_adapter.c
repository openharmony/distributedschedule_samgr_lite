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

typedef struct IRegisterEpArg IRegisterEpArg;
struct IRegisterEpArg {
    Endpoint *endpoint;
    int token;
    char *service;
    char *feature;
};
static int Dispatch(uint32_t code, IpcIo *data, IpcIo *reply, MessageOption option);
static void HandleIpc(const Request *request, const Response *response);
static void *Receive(void *argv);
static int RegisterRemoteFeatures(Endpoint *endpoint);
static void OnSamgrServerExit(void *argv);
static void GetRemotePolicy(IpcIo *reply, PolicyTrans **policy, uint32 *policyNum);
static boolean JudgePolicy(uid_t callingUid, const PolicyTrans *policy, uint32 policyNum);
static boolean SearchFixedPolicy(uid_t callingUid, PolicyTrans policy);
int ClientRegisterRemoteEndpoint(SvcIdentity *identity, int token, const char *service, const char *feature)
{
    IpcIo req;
    uint8 data[MAX_DATA_LEN];
    IpcIoInit(&req, data, MAX_DATA_LEN, 1);
    // add samgr server token
    WriteInt32(&req, 0);
    WriteUint32(&req, RES_ENDPOINT);
    WriteUint32(&req, OP_POST);
    bool ret = WriteRemoteObject(&req, identity);
    if (!ret) {
        return EC_FAILURE;
    }
    uint32 retry = 0;
    while (retry < MAX_RETRY_TIMES) {
        ++retry;
        IpcIo reply;
        void *replyBuf = NULL;
        MessageOption option;
        MessageOptionInit(&option);
        SvcIdentity *samgr = GetContextObject();

        int err = SendRequest(*samgr, INVALID_INDEX, &req, &reply, option, (uintptr_t *)&replyBuf);
        if (err == EC_SUCCESS) {
            ret = ReadInt32(&reply, &identity->handle);
            if (!ret || identity->handle == INVALID_INDEX) {
                continue;
            }
            if (replyBuf != NULL) {
                FreeBuffer(replyBuf);
            }
            return EC_SUCCESS;
        }
        usleep(RETRY_INTERVAL);
    }
    return EC_FAILURE;
}

void Listen(Endpoint *endpoint, int token, const char *service, const char *feature)
{
    if (endpoint->boss != NULL) {
        return;
    }
    ThreadAttr attr = {endpoint->name, MAX_STACK_SIZE, PRI_ABOVE_NORMAL, 0, 0};
    IRegisterEpArg *registerEpArg = SAMGR_Malloc(sizeof(IRegisterEpArg));
    if (registerEpArg == NULL) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "IRegisterEpArg Memory is not enough!");
        return;
    }
    IpcObjectStub *objectStubOne = (IpcObjectStub *)calloc(1, sizeof(IpcObjectStub));
    if (objectStubOne == NULL) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "IpcObjectStub Memory is not enough!");
        return;
    }

    objectStubOne->func = Dispatch;
    objectStubOne->args = endpoint;
    objectStubOne->isRemote = false;
    endpoint->identity.cookie = objectStubOne;
    // handle must -1
    endpoint->identity.handle = INVALID_INDEX;
    endpoint->identity.token = SERVICE_TYPE_NORMAL;

    registerEpArg->endpoint = endpoint;
    registerEpArg->token = token;
    registerEpArg->service = service;
    registerEpArg->feature = feature;
    endpoint->boss = (ThreadId)THREAD_Create(Receive, registerEpArg, &attr);
}

static void *Receive(void *argv)
{
    IRegisterEpArg *registerEpArg = (IRegisterEpArg *)argv;
    if (registerEpArg == NULL || registerEpArg->endpoint->registerEP == NULL) {
        return NULL;
    }
    int ret = EC_INVALID;
    uint32 retry = 0;
    while (retry < MAX_RETRY_TIMES) {
        ret = registerEpArg->endpoint->registerEP(&registerEpArg->endpoint->identity,
            registerEpArg->token, registerEpArg->service, registerEpArg->feature);
        if (ret == EC_SUCCESS) {
            SvcIdentity *samgr = GetContextObject();
            (void)RemoveDeathRecipient(*samgr, registerEpArg->endpoint->deadId);
            (void)AddDeathRecipient(*samgr, OnSamgrServerExit, registerEpArg->endpoint,
                &registerEpArg->endpoint->deadId);
            break;
        }
        ++retry;
        usleep(RETRY_INTERVAL);
    }
    if (ret != EC_SUCCESS) {
        HILOG_FATAL(HILOG_MODULE_SAMGR, "Register endpoint<%s>, handle<%u> failed! will exit to recover!",
                    registerEpArg->endpoint->name, registerEpArg->endpoint->identity.handle);
        SAMGR_Free(registerEpArg);
        return NULL;
    }
    registerEpArg->endpoint->running = TRUE;
    if (strcmp(registerEpArg->endpoint->name, SAMGR_SERVICE) != 0) {
        int remain = RegisterRemoteFeatures(registerEpArg->endpoint);
        HILOG_INFO(HILOG_MODULE_SAMGR, "Register endpoint<%s> and iunknown finished! remain<%d> iunknown!",
            registerEpArg->endpoint->name, remain);
    }
    SAMGR_Free(registerEpArg);
    JoinWorkThread();
    return NULL;
}

static int RegisterRemoteFeatures(Endpoint *endpoint)
{
    int nums = 0;
    int size = VECTOR_Size(&endpoint->routers);
    int i;
    SvcIdentity identity;
    for (i = 0; i < size; ++i) {
        Router *router = VECTOR_At(&endpoint->routers, i);
        if (router == NULL) {
            continue;
        }
        identity.handle = endpoint->identity.handle;
        identity.token = i;
        int ret = RegisterIdentity(&(router->saName), &identity, &(router->policy),
                                   &(router->policyNum));
        if (ret == EC_SUCCESS) {
            ++nums;
        }
        HILOG_DEBUG(HILOG_MODULE_SAMGR, "RegisterRemoteFeatures<%s, %s> ret:%d",
                    router->saName.service, router->saName.feature,  ret);
    }
    return VECTOR_Num(&endpoint->routers) - nums;
}

int RegisterIdentity(const SaName *saName, SvcIdentity *saInfo, PolicyTrans **policy, uint32 *policyNum)
{
    IpcIo req;
    uint8 data[MAX_DATA_LEN];
    IpcIoInit(&req, data, MAX_DATA_LEN, 0);
    WriteInt32(&req, 0);
    WriteUint32(&req, RES_FEATURE);
    WriteUint32(&req, OP_PUT);
    WriteString(&req, saName->service);
    WriteBool(&req, saName->feature == NULL);
    if (saName->feature != NULL) {
        WriteString(&req, saName->feature);
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
        ReadInt32(&reply, &ipcRet);
    }
    if (ipcRet == EC_SUCCESS) {
        SvcIdentity target;
        (void)ReadRemoteObject(&reply, &target);
        GetRemotePolicy(&reply, policy, policyNum);
    }
    if (replyBuf != NULL) {
        FreeBuffer(replyBuf);
    }
    return ipcRet;
}
static int Dispatch(uint32_t code, IpcIo *data, IpcIo *reply, MessageOption option)
{
    Endpoint *endpoint = (Endpoint *)option.args;
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
    resp.reply = reply;
    Request request = {0};
    request.msgId = token;
    request.data = data;
    request.msgValue = code;

    HandleIpc(&request, &resp);
    return EC_SUCCESS;
ERROR:
    return EC_INVALID;
}

static void HandleIpc(const Request *request, const Response *response)
{
    Endpoint *endpoint = (Endpoint *)response->data;
    Router *router = VECTOR_At(&endpoint->routers, request->msgId);
    if (router == NULL || router->proxy == NULL || router->proxy->Invoke == NULL) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "Invalid IPC router!");
        return;
    }
    uid_t uid = GetCallingUid();
    if ((strcmp(router->saName.service, SAMGR_SERVICE) != 0) &&
        !JudgePolicy(uid, (const PolicyTrans *)(router->policy), router->policyNum)) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "Consumer uid<%d> has no permission to access<%s, %d, %d>!",
                    uid, router->saName.service, router->identity.serviceId, router->identity.featureId);
        return;
    }
    router->proxy->Invoke(router->proxy, request->msgValue, NULL, request->data, response->reply);
}

static boolean JudgePolicy(uid_t callingUid, const PolicyTrans *policy, uint32 policyNum)
{
    if (policy == NULL) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "Policy is NULL! Num is %u", policyNum);
        return FALSE;
    }
    uint32 i;
    for (i = 0; i < policyNum; i++) {
        if (policy[i].type == RANGE && callingUid >= policy[i].uidMin && callingUid <= policy[i].uidMax) {
            return TRUE;
        }
        if (policy[i].type == FIXED && SearchFixedPolicy(callingUid, policy[i])) {
            return TRUE;
        }
    }
    return FALSE;
}
static boolean SearchFixedPolicy(uid_t callingUid, PolicyTrans policy)
{
    int i;
    for (i = 0; i < UID_SIZE; i++) {
        if (callingUid == policy.fixedUid[i]) {
            return TRUE;
        }
    }
    return FALSE;
}
static void OnSamgrServerExit(void *argv)
{
    HILOG_ERROR(HILOG_MODULE_SAMGR, "Disconnect to samgr server!");
    Endpoint *endpoint = (Endpoint *)argv;
    if (endpoint == NULL || endpoint->registerEP == NULL) {
        return;
    }
    int size = VECTOR_Size(&endpoint->routers);
    int i;
    for (i = 0; i < size; i++) {
        Router *router = VECTOR_At(&endpoint->routers, i);
        if (router == NULL) {
            continue;
        }
        SAMGR_Free(router->policy);
        router->policy = NULL;
        router->policyNum = 0;
    }

    SvcIdentity old = endpoint->identity;
    while (endpoint->registerEP(&endpoint->identity, 0, "", "") != EC_SUCCESS) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "Reconnect to samgr server failed!");
        usleep(RETRY_INTERVAL);
    }
    SvcIdentity new = endpoint->identity;
    if (old.handle != new.handle || old.cookie != new.cookie || old.token != new.token) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "Samgr server identity error!");
        exit(-1);
    }

    SvcIdentity identity = {SAMGR_HANDLE, SAMGR_TOKEN, SAMGR_COOKIE};
    ReleaseSvc(identity);
    (void)AddDeathRecipient(identity, OnSamgrServerExit, endpoint, &endpoint->deadId);
    int remain = RegisterRemoteFeatures(endpoint);
    HILOG_INFO(HILOG_MODULE_SAMGR, "Reconnect and register finished! remain<%d> iunknown!", remain);
}

int GetRemoteToken(IpcIo *data)
{
    int32_t token;
    if (ReadInt32(data, &token)) {
        return token;
    }
    return EC_INVALID;
}

static void GetRemotePolicy(IpcIo *reply, PolicyTrans **policy, uint32 *policyNum)
{
    if (reply == NULL) {
        return;
    }
    uint32 i;
    uint32 j;
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
    for (i = 0; i < *policyNum; i++) {
        if (ReadInt32(reply, &(*policy)[i].type)) {
            switch ((*policy)[i].type) {
                case RANGE:
                    ReadInt32(reply, &((*policy)[i].uidMin));
                    ReadInt32(reply, &((*policy)[i].uidMax));
                    break;
                case FIXED:
                    for (j = 0; j < UID_SIZE; j++) {
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
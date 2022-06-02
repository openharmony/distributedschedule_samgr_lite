/*
 * Copyright (c) 2020-2022 Huawei Device Co., Ltd.
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
#include <stdlib.h>
#include "default_client_adapter.h"

static int AddRef(IUnknown *iUnknown);
static int Release(IUnknown *proxy);
static int ProxyInvoke(IClientProxy *proxy, int funcId, IpcIo *request, IOwner owner, INotify notify);
static void OnServiceExit(void *argv);
static SvcIdentity QueryIdentity(const char *service, const char *feature);
static SvcIdentity QueryRemoteIdentity(const char *deviceId, const char *service, const char *feature);
static const IClientEntry DEFAULT_ENTRY = {CLIENT_IPROXY_BEGIN, .Invoke = ProxyInvoke, IPROXY_END};
static MutexId g_mutex = NULL;

IUnknown *SAMGR_CreateIProxy(const char *service, const char *feature)
{
    SvcIdentity identity = QueryIdentity(service, feature);
    if (identity.handle == INVALID_INDEX) {
        return NULL;
    }

    IDefaultClient *client = SAMGR_CreateIClient(service, feature, sizeof(IClientHeader));
    if (client == NULL) {
        client = SAMGR_Malloc(sizeof(IDefaultClient));
        if (client == NULL) {
            return NULL;
        }
        client->entry = DEFAULT_ENTRY;
    }

    IClientHeader *header = &client->header;
    header->target = identity;
    header->key.service = service;
    header->key.feature = feature;
    header->saId = 0;
    (void)AddDeathRecipient(identity, OnServiceExit, client, &header->deadId);

    IClientEntry *entry = &client->entry;
    entry->iUnknown.Invoke = ProxyInvoke;
    entry->iUnknown.AddRef = AddRef;
    entry->iUnknown.Release = Release;
    return GET_IUNKNOWN(*entry);
}

IUnknown *SAMGR_CreateIRemoteProxy(const char* deviceId, const char *service, const char *feature)
{
    SvcIdentity identity = QueryRemoteIdentity(deviceId, service, feature);

    IDefaultClient *client = SAMGR_CreateIClient(service, feature, sizeof(IClientHeader));
    if (client == NULL) {
        client = SAMGR_Malloc(sizeof(IDefaultClient));
        if (client == NULL) {
            return NULL;
        }
        client->entry = DEFAULT_ENTRY;
    }

    IClientHeader *header = &client->header;
    header->target = identity;
    header->key.service = service;
    header->key.feature = feature;
    header->saId = GetRemoteSaIdInner(service, feature);

    IClientEntry *entry = &client->entry;
    entry->iUnknown.Invoke = ProxyInvoke;
    entry->iUnknown.AddRef = AddRef;
    entry->iUnknown.Release = Release;
    return GET_IUNKNOWN(*entry);
}

SvcIdentity SAMGR_GetRemoteIdentity(const char *service, const char *feature)
{
    SvcIdentity identity = {INVALID_INDEX, INVALID_INDEX, INVALID_INDEX};
    IUnknown *iUnknown = SAMGR_FindServiceApi(service, feature);
    if (iUnknown == NULL) {
        return identity;
    }
    IClientProxy *proxy = NULL;
    if (iUnknown->QueryInterface(iUnknown, CLIENT_PROXY_VER, (void **)&proxy) != EC_SUCCESS || proxy == NULL) {
        return identity;
    }
    struct IDefaultClient *client = GET_OBJECT(proxy, struct IDefaultClient, entry.iUnknown);
    identity = client->header.target;
    proxy->Release((IUnknown *)proxy);
    return identity;
}

SaName *SAMGR_GetSAName(const IUnknown *proxy)
{
    IDefaultClient *client = GET_OBJECT(proxy, IDefaultClient, entry.iUnknown);
    return &(client->header.key);
}

int SAMGR_CompareSAName(const SaName *key1, const SaName *key2)
{
    if (key1 == key2) {
        return 0;
    }

    if (key1->service != key2->service) {
        int ret = strcmp(key1->service, key2->service);
        if (ret != 0) {
            return ret;
        }
    }

    if (key1->feature == key2->feature) {
        return 0;
    }

    if (key1->feature == NULL) {
        return -1;
    }

    if (key2->feature == NULL) {
        return 1;
    }

    return strcmp(key1->feature, key2->feature);
}

static int AddRef(IUnknown *iUnknown)
{
    MUTEX_Lock(g_mutex);
    int ref = IUNKNOWN_AddRef(iUnknown);
    MUTEX_Unlock(g_mutex);
    return ref;
}

static int Release(IUnknown *proxy)
{
    MUTEX_Lock(g_mutex);
    int ref = IUNKNOWN_Release(proxy);
    MUTEX_Unlock(g_mutex);
    if (ref != 0) {
        return ref;
    }
    IDefaultClient *client = GET_OBJECT(proxy, IDefaultClient, entry.iUnknown);
    int ret = SAMGR_ReleaseIClient(client->header.key.service, client->header.key.feature, client);
    if (ret == EC_NOHANDLER) {
        SAMGR_Free(client);
        return EC_SUCCESS;
    }
    return ret;
}

static int ProxyInvoke(IClientProxy *proxy, int funcId, IpcIo *request, IOwner owner, INotify notify)
{
    if (proxy == NULL) {
        return EC_INVALID;
    }

    IDefaultClient *client = GET_OBJECT(proxy, IDefaultClient, entry.iUnknown);
    IClientHeader *header = &client->header;

    IpcIo reply;
    void *replyBuf = NULL;
    MessageOption flag;
    MessageOptionInit(&flag);
    flag.flags = (notify == NULL) ? TF_OP_ASYNC : TF_OP_SYNC;
    IpcIo requestWrapper;
    uint8_t *data = (uint8_t *) malloc(MAX_IO_SIZE);
    if (data == NULL) {
        HILOG_ERROR(HILOG_MODULE_SAMGR, "malloc data for ipc io failed\n");
        return EC_INVALID;
    } else {
        IpcIoInit(&requestWrapper, data, MAX_IO_SIZE, MAX_OBJ_NUM);
    }
    ProxyInvokeArgInner(&requestWrapper, header);
    if(request != NULL) {
        if (!IpcIoAppend(&requestWrapper, request)) {
            HILOG_ERROR(HILOG_MODULE_SAMGR, "ipc io append fail\n");
            free(data);
            return EC_INVALID;
        }
    }
    int ret = SendRequest(header->target, funcId, &requestWrapper, &reply, flag, (uintptr_t *)&replyBuf);
    free(data);

    if (notify != NULL) {
        notify(owner, ret, &reply);
    }

    if (replyBuf != NULL) {
        FreeBuffer(replyBuf);
    }
    return ret;
}

static void OnServiceExit(void *argv)
{
    IClientHeader *header = (IClientHeader *)argv;
    ReleaseSvc(header->target);
    header->deadId = INVALID_INDEX;
    header->target.handle = INVALID_INDEX;
    header->target.token = INVALID_INDEX;
    header->target.cookie = INVALID_INDEX;
    HILOG_ERROR(HILOG_MODULE_SAMGR, "Miss the remote service<%u, %u>!", header->target.handle, header->target.token);
}

static SvcIdentity QueryIdentity(const char *service, const char *feature)
{
    IpcIo req;
    uint8 data[MAX_DATA_LEN];
    IpcIoInit(&req, data, MAX_DATA_LEN, 0);
    WriteInt32(&req, 0);
    WriteUint32(&req, RES_FEATURE);
    WriteUint32(&req, OP_GET);
    WriteString(&req, service);
    WriteBool(&req, feature == NULL);
    if (feature != NULL) {
        WriteString(&req, feature);
    }
    IpcIo reply;
    void *replyBuf = NULL;
    const SvcIdentity *samgr = GetContextObject();
    MessageOption flag;
    MessageOptionInit(&flag);
    int ret = SendRequest(*samgr, INVALID_INDEX, &req, &reply, flag, (uintptr_t *)&replyBuf);
    int32_t sa_ret = EC_FAILURE;
    ret = (ret != EC_SUCCESS) ? EC_FAILURE : ReadInt32(&reply, &sa_ret);
    SvcIdentity target = {INVALID_INDEX, INVALID_INDEX, INVALID_INDEX};
    if (sa_ret == EC_SUCCESS) {
        ReadRemoteObject(&reply, &target);
        uint32_t token;
        ReadUint32(&reply, &token);
        target.token = (uintptr_t)token;
    }
    if (ret == EC_PERMISSION) {
        HILOG_INFO(HILOG_MODULE_SAMGR, "Cannot Access<%s, %s> No Permission!", service, feature);
    }
    if (replyBuf != NULL) {
        FreeBuffer(replyBuf);
    }
    return target;
}

static SvcIdentity QueryRemoteIdentity(const char *deviceId, const char *service, const char *feature)
{
    return QueryRemoteIdentityInner(deviceId, service, feature);
}

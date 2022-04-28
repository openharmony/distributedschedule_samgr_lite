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
#ifndef LITE_DEFAULT_CLIENT_ADAPTER_H
#define LITE_DEFAULT_CLIENT_ADAPTER_H
#include "default_client.h"

#include <log.h>
#include <ohos_errno.h>
#include <pthread.h>
#include <string.h>
#include <service_registry.h>
#include "client_factory.h"
#include "endpoint.h"
#include "ipc_skeleton.h"
#include "iproxy_client.h"
#include "memory_adapter.h"
#include "thread_adapter.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif
#undef LOG_TAG
#undef LOG_DOMAIN
#define LOG_TAG "Samgr"
#define LOG_DOMAIN 0xD001800

typedef struct IClientHeader IClientHeader;
typedef struct IDefaultClient IDefaultClient;
typedef struct IClientEntry IClientEntry;
struct IClientHeader {
    SaName key;
    SvcIdentity target;
    uint32 deadId;
    uintptr_t saId;
};

struct IClientEntry {
    INHERIT_IUNKNOWNENTRY(IClientProxy);
};

#pragma pack(1)
struct IDefaultClient {
    IClientHeader header;
    IClientEntry entry;
};
#pragma pack()

uintptr_t GetRemoteSaIdInner(const char *service, const char *feature);
void ProxyInvokeArgInner(IpcIo *reply, IClientHeader *header);
SvcIdentity QueryRemoteIdentityInner(const char *deviceId, const char *service, const char *feature);
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif
#endif // LITE_DEFAULT_CLIENT_H

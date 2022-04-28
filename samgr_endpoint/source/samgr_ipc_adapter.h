/*
 * Copyright (c) 2020 Huawei Device Co., Ltd.
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
#ifndef SAMGR_IPC_ADAPTER_H
#define SAMGR_IPC_ADAPTER_H

#include "endpoint.h"

#include <log.h>
#include <ohos_errno.h>
#include <securec.h>
#include <service.h>
#include <stdlib.h>
#include <unistd.h>

#include "default_client.h"
#include "ipc_skeleton.h"
#include "iproxy_server.h"
#include "memory_adapter.h"
#include "policy_define.h"
#include "pthread.h"
#include "serializer.h"
#include "thread_adapter.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif
typedef struct RemoteRegister RemoteRegister;
struct RemoteRegister {
    MutexId mtx;
    Endpoint *endpoint;
    Vector clients;
};

#undef LOG_TAG
#undef LOG_DOMAIN
#define LOG_TAG "Samgr"
#define LOG_DOMAIN 0xD001800

#ifdef LITE_LINUX_BINDER_IPC
#define MAX_STACK_SIZE 0x100000
#else
#define MAX_STACK_SIZE 0x1000
#endif
#define MAX_OBJECT_NUM 5
#define MAX_RETRY_TIMES 300
#define RETRY_INTERVAL (50 * 1000)
#define MAX_REGISTER_RETRY_TIMES 10
#define REGISTER_RETRY_INTERVAL 2
#define MAX_POLICY_NUM 8

#ifndef MAX_BUCKET_RATE
#define MAX_BUCKET_RATE 1000
#endif

#ifndef MAX_BURST_RATE
#define MAX_BURST_RATE (MAX_BUCKET_RATE + (MAX_BUCKET_RATE >> 1))
#endif

#define SAMGR_SERVICE "samgr"

typedef struct Router {
    SaName saName;
    Identity identity;
    IServerProxy *proxy;
    PolicyTrans *policy;
    uint32 policyNum;
} Router;

int ClientRegisterRemoteEndpoint(SvcIdentity *identity, int token, const char *service, const char *feature);
void Listen(Endpoint *endpoint, int token, const char *service, const char *feature);
int RegisterIdentity(const SaName *saName, SvcIdentity *saInfo, PolicyTrans **policy, uint32 *policyNum);
int GetRemoteToken(IpcIo *data);
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif
#endif // LITE_DEFAULT_CLIENT_H

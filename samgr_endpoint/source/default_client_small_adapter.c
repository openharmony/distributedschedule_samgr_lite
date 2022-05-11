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

uintptr_t GetRemoteSaIdInner(const char *service, const char *feature)
{
    return 0;
}
void ProxyInvokeArgInner(IpcIo *reply, IClientHeader *header)
{
    WriteInt32(reply, (int32_t)header->target.token);
}

SvcIdentity QueryRemoteIdentityInner(const char *deviceId, const char *service, const char *feature)
{
    SvcIdentity target = {INVALID_INDEX, INVALID_INDEX, INVALID_INDEX};
    return target;
}
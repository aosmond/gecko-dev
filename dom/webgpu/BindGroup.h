/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGPU_BindGroup_H_
#define WEBGPU_BindGroup_H_

#include "nsWrapperCache.h"
#include "ObjectModel.h"

namespace mozilla {
namespace webgpu {

class Device;

class BindGroup final
    : public ChildOf<Device>
{
public:
    WEBGPU_DECL_GOOP(BindGroup)

private:
    BindGroup() = delete;
    virtual ~BindGroup();
};

} // namespace webgpu
} // namespace mozilla

#endif // WEBGPU_BindGroup_H_

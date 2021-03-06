// Copyright (c) 2014 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vehicle/vehicle_extension.h"

#include "vehicle/vehicle_instance.h"

common::Extension* CreateExtension() {
  return new VehicleExtension();
}

extern const char kSource_vehicle_api[];

VehicleExtension::VehicleExtension() {
  SetExtensionName("tizen.vehicle");
  SetJavaScriptAPI(kSource_vehicle_api);
}

VehicleExtension::~VehicleExtension() {
}

common::Instance* VehicleExtension::CreateInstance() {
  return new VehicleInstance;
}

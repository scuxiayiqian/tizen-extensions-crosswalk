// Copyright (c) 2013 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSTEM_INFO_SYSTEM_INFO_STORAGE_H_
#define SYSTEM_INFO_SYSTEM_INFO_STORAGE_H_

#include <glib.h>
#include <libudev.h>
#include <map>
#include <string>

#include "common/picojson.h"
#include "common/utils.h"
#include "system_info/system_info_instance.h"
#include "system_info/system_info_utils.h"

struct SysInfoDeviceStorageUnit {
  enum Type { UNKNOWN, INTERNAL, USB_HOST, MMC };
  Type type;
  bool is_removable;
  double available_capacity;
  double capacity;
  int capability;
  int id;
  std::string name;
};

class SysInfoStorage : public SysInfoObject {
 public:
  static SysInfoObject& GetInstance() {
    static SysInfoStorage instance;
    return instance;
  }
  ~SysInfoStorage();
  void Get(picojson::value& error, picojson::value& data);
  void StartListening();
  void StopListening();

  static const std::string name_;

 private:
  explicit SysInfoStorage();
  static void StorageDevicesListener(gpointer data);
  void Update(picojson::value& error);
  bool GetAllAvailableStorageDevices(picojson::value& error,
                                     picojson::array& units_arr) const;
  bool InitStorageMonitor();
  bool IsRealStorageDevice(udev_device *dev) const;
  void QueryAllAvailableStorageUnits(picojson::value& error);
  bool MakeStorageUnit(picojson::value& error,
                       SysInfoDeviceStorageUnit& unit,
                       udev_device* dev) const;
  void AddStorageUnit(picojson::value& error, udev_device* dev);
  void OnDeviceUpdate(picojson::value& error);

  const int internal_disk = 50;
  const int u_flash = 51;
  GThread* usb_host_listener_;
  int udev_monitor_fd_;
  picojson::value units_;
  udev* udev_;
  udev_enumerate* enumerate_;
  udev_monitor* udev_monitor_;

  typedef std::map<int, SysInfoDeviceStorageUnit> StoragesMap;
  StoragesMap storages_;

  DISALLOW_COPY_AND_ASSIGN(SysInfoStorage);
};

#endif  // SYSTEM_INFO_SYSTEM_INFO_STORAGE_H_

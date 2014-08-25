// Copyright (c) 2013 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system_info/system_info_storage.h"

#include <locale.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "common/picojson.h"

const std::string SysInfoStorage::name_ = "STORAGE";

SysInfoStorage::SysInfoStorage()
    : enumerate_(NULL),
      udev_(udev_new()),
      udev_monitor_(NULL),
      udev_monitor_fd_(-1),
      usb_host_listener_(NULL) {
  units_ = picojson::value(picojson::array(0));
}

SysInfoStorage::~SysInfoStorage() {
  if (usb_host_listener_ != NULL) {
    g_thread_unref(usb_host_listener_);
    usb_host_listener_ = NULL;
  }
}

void SysInfoStorage::Get(picojson::value& error,
                         picojson::value& data) {
  QueryAllAvailableStorageUnits(error);
  units_ = picojson::value(picojson::array(0));
  picojson::array& units_arr = units_.get<picojson::array>();
  units_arr.clear();
  if (!GetAllAvailableStorageDevices(error, units_arr)) {
    if (error.get("message").to_str().empty())
      system_info::SetPicoJsonObjectValue(error, "message",
          picojson::value("Get storage faild."));
    return;
  }

  system_info::SetPicoJsonObjectValue(data, "units", units_);
  system_info::SetPicoJsonObjectValue(error, "message", picojson::value(""));
}

bool SysInfoStorage::GetAllAvailableStorageDevices(
    picojson::value& error,
    picojson::array& units_arr) const {
  if (storages_.empty())
    return false;

  std::map<int, SysInfoDeviceStorageUnit>::const_iterator it;

  for (it = storages_.begin(); it != storages_.end(); ++it) {
    if (it->second.capacity == 0)
      continue;

    picojson::value unit = picojson::value(picojson::object());
    system_info::SetPicoJsonObjectValue(unit, "capacity",
          picojson::value(it->second.capacity));
    if (it->second.type == SysInfoDeviceStorageUnit::USB_HOST) {
      // TODO(yiqian): Need to find a way to calculate available capacity
      // of usb flash devices.
      system_info::SetPicoJsonObjectValue(unit, "availableCapacity",
          picojson::value(0.0));
      system_info::SetPicoJsonObjectValue(unit, "type",
          picojson::value("USB_HOST"));
    } else if (it->second.type == SysInfoDeviceStorageUnit::INTERNAL) {
      system_info::SetPicoJsonObjectValue(unit, "availableCapacity",
          picojson::value(it->second.available_capacity));
      system_info::SetPicoJsonObjectValue(unit, "type",
          picojson::value("INTERNAL"));
    } else if (it->second.type == SysInfoDeviceStorageUnit::UNKNOWN) {
      system_info::SetPicoJsonObjectValue(unit, "availableCapacity",
          picojson::value(it->second.available_capacity));
      system_info::SetPicoJsonObjectValue(unit, "type",
          picojson::value("UNKNOWN"));
    }
    // From the SystemInfo spec, isRemoveable is a deprecated prop.
    system_info::SetPicoJsonObjectValue(unit, "isRemoveable",
          picojson::value(it->second.is_removable));
    system_info::SetPicoJsonObjectValue(unit, "isRemovable",
          picojson::value(it->second.is_removable));
    units_arr.push_back(unit);
  }

  return true;
}

bool SysInfoStorage::InitStorageMonitor() {
  if (!udev_)
    return false;

  // Init udev_monitor_.
  udev_monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_monitor_)
    return false;
  udev_monitor_filter_add_match_subsystem_devtype(udev_monitor_,
                                                  "block",
                                                  NULL);
  udev_monitor_enable_receiving(udev_monitor_);
  udev_monitor_fd_ = udev_monitor_get_fd(udev_monitor_);

  // Init enumerate_.
  enumerate_ = udev_enumerate_new(udev_);
  if (!enumerate_)
    return false;
  udev_enumerate_add_match_subsystem(enumerate_, "block");

  return true;
}

void SysInfoStorage::QueryAllAvailableStorageUnits(picojson::value& error) {
  udev_list_entry* dev_list_entry;
  udev_device* dev;

  storages_.clear();

  udev_enumerate_scan_devices(enumerate_);
  udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate_);
  udev_list_entry_foreach(dev_list_entry, devices) {
    const char* path = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(udev_, path);

    if (!IsRealStorageDevice(dev))
      continue;

    SysInfoDeviceStorageUnit unit;
    if (MakeStorageUnit(error, unit, dev))
      storages_[unit.id] = unit;
  }
}

void SysInfoStorage::AddStorageUnit(picojson::value& error,
                                    udev_device* dev) {
    if (!IsRealStorageDevice(dev))
      return;

    SysInfoDeviceStorageUnit unit;
    if (MakeStorageUnit(error, unit, dev))
      storages_[unit.id] = unit;
}

bool SysInfoStorage::IsRealStorageDevice(udev_device *dev) const {
  const char* dev_type =
      udev_device_get_sysattr_value(dev, "removable");
  const char* dev_capability =
      udev_device_get_sysattr_value(dev, "capability");
  const char* dev_size =
      udev_device_get_sysattr_value(dev, "size");

  if (dev_capability && dev_type && dev_size) {
    int capability = std::stoi(dev_capability);

    // Capability meaning:
    // internal_disk: 1. disk device for Linux, such as "sda" "sdb";
    // u_flash: 1. U-flash device and sdcard for Linux
    if (capability == internal_disk || capability == u_flash)
      return true;
    else
      return false;
  } else if (!dev_capability || !dev_type || !dev_size){
    return false;
  }
}

bool SysInfoStorage::MakeStorageUnit(picojson::value& error,
                                     SysInfoDeviceStorageUnit& unit,
                                     udev_device* dev) const {
  unit.id = udev_device_get_devnum(dev);
  unit.name = udev_device_get_devnode(dev);
  if (std::stoi(udev_device_get_sysattr_value(dev, "removable")) == 1)
    unit.is_removable = true;
  else
    unit.is_removable = false;
  unit.capability = std::stoi(udev_device_get_sysattr_value(dev, "capability"));

  if (unit.capability == internal_disk && !unit.is_removable) {
    FILE *aFile;
    aFile = setmntent("/proc/mounts", "r");
    if (!aFile) {
      system_info::SetPicoJsonObjectValue(error, "message",
      picojson::value("Read mount table failed."));
      return false;
    }
    struct mntent *entry;
    while (entry = getmntent(aFile)) {
      if (entry->mnt_fsname[0] == '/' && entry->mnt_dir == std::string("/")) {
        struct statvfs buf;
        int ret = statvfs(entry->mnt_dir, &buf);

        unit.available_capacity = static_cast<double>(buf.f_bsize) *
                         static_cast<double>(buf.f_bavail);
        unit.capacity = static_cast<double>(buf.f_bsize) *
                   static_cast<double>(buf.f_blocks);
      }
    }
    endmntent(aFile);
    unit.type = SysInfoDeviceStorageUnit::INTERNAL;
  } else if (unit.capability == u_flash && unit.is_removable) {
    unit.capacity = std::stof(udev_device_get_sysattr_value(dev, "size")) * 512;
    unit.available_capacity = -1;
    unit.type = SysInfoDeviceStorageUnit::USB_HOST;
  } else {
    unit.capacity = std::stof(udev_device_get_sysattr_value(dev, "size")) * 512;
    unit.available_capacity = -1;
    unit.type = SysInfoDeviceStorageUnit::UNKNOWN;
  }

  return true;
}

void SysInfoStorage::StorageDevicesListener(gpointer data) {
  SysInfoStorage* instance = static_cast<SysInfoStorage*>(data);

  // Can't to take a reference (&), just copy.
  picojson::array old_units_arr = instance->units_.get<picojson::array>();
  picojson::value error = picojson::value(picojson::object());
  instance->Update(error);
}

void SysInfoStorage::Update(picojson::value& error) {
  units_ = picojson::value(picojson::array(0));
  InitStorageMonitor();
  QueryAllAvailableStorageUnits(error);

  picojson::array& units_arr = units_.get<picojson::array>();
  units_arr.clear();
  GetAllAvailableStorageDevices(error, units_arr);

  // int device_number = storages_.size();
  // std::cerr << "in update: size = " << device_number << std::endl;

  while (1) {
    int device_number = storages_.size();
    int after_receive_device_number = device_number;
    fd_set fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET(udev_monitor_fd_, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    // select() will only operate on a single file descriptor, which is related
    // to the udev_monitor. The timeval object is set to 0 for not blocking
    // select().
    int ret = select(udev_monitor_fd_ + 1, &fds, NULL, NULL, &tv);
    if (ret > 0 && FD_ISSET(udev_monitor_fd_, &fds)) {
      udev_device* dev = udev_monitor_receive_device(udev_monitor_);

      if (!dev) {
        usleep(1000);
        continue;
      }

      int dev_id = udev_device_get_devnum(dev);
      std::string action = udev_device_get_action(dev);
      if (action == "add") {
        AddStorageUnit(error, dev);
      } else if (action == "remove") {
        storages_.erase(dev_id);
      }

      after_receive_device_number = storages_.size();
      if (after_receive_device_number == device_number)
        continue;
      else
        OnDeviceUpdate(error);

      udev_device_unref(dev);
    }
  }
}

void SysInfoStorage::OnDeviceUpdate(picojson::value& error) {
  units_ = picojson::value(picojson::array(0));
  picojson::array& units_arr = units_.get<picojson::array>();
  units_arr.clear();

  GetAllAvailableStorageDevices(error, units_arr);

  picojson::value output = picojson::value(picojson::object());
  picojson::value data = picojson::value(picojson::object());

  system_info::SetPicoJsonObjectValue(data, "units", units_);
  system_info::SetPicoJsonObjectValue(output, "cmd",
      picojson::value("SystemInfoPropertyValueChanged"));
  system_info::SetPicoJsonObjectValue(output, "prop",
      picojson::value("STORAGE"));
  system_info::SetPicoJsonObjectValue(output, "data", data);

  PostMessageToListeners(output);
}

void SysInfoStorage::StartListening() {
  usb_host_listener_ = g_thread_new("storage_devices_listener",
                                    (GThreadFunc)StorageDevicesListener,
                                    (gpointer)this);
}

void SysInfoStorage::StopListening() {
  if (usb_host_listener_ != NULL) {
    g_thread_unref(usb_host_listener_);
    usb_host_listener_ = NULL;
  }
}

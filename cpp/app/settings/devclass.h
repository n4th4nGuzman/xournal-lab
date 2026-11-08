// Which class an input device is treated as (Preferencias > Sistema de entrada). The key is the name Qt gives the device plus what kind of device it is: stable
// between runs, and different for the pen and the eraser end of a tablet only if the driver names them differently.
#pragma once
#include "xpppref.h"
#include <QPointingDevice>

inline QString deviceKey(const QPointingDevice* d) { return d ? d->name() + QStringLiteral("|") + QString::number(int(d->type())) : QString(); }
inline DeviceClass deviceClassOf(const XppPrefs& x, const QPointingDevice* d) {
  const auto it = x.deviceClass.constFind(deviceKey(d));
  return it == x.deviceClass.constEnd() ? DeviceClass::Auto : DeviceClass(it.value());
}

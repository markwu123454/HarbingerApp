#pragma once
// winsock2 must precede windows.h to avoid winsock1 conflicts
#include <winsock2.h>
#include <windows.h>
#include <ws2bth.h>
#include <QString>

struct BtDevice {
    QString  name;
    BTH_ADDR address;
    QString  addressStr;
    bool     connected;
};

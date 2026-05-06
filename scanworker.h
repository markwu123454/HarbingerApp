#pragma once
#include <QObject>
#include <QList>
#include <bluetoothapis.h>
#include "btdevice.h"

class ScanWorker : public QObject {
    Q_OBJECT
public slots:
    void scan() {
        QList<BtDevice> found;
        BLUETOOTH_DEVICE_SEARCH_PARAMS params{};
        params.dwSize               = sizeof(params);
        params.fReturnAuthenticated = TRUE;
        params.fReturnRemembered    = TRUE;
        params.fReturnUnknown       = TRUE;
        params.fReturnConnected     = TRUE;
        params.fIssueInquiry        = TRUE;
        params.cTimeoutMultiplier   = 4;
        BLUETOOTH_DEVICE_INFO info{};
        info.dwSize = sizeof(info);
        HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&params, &info);
        if (hFind) {
            do {
                BtDevice dev;
                dev.name      = QString::fromWCharArray(info.szName);
                dev.address   = info.Address.ullLong;
                dev.connected = info.fConnected;
                BTH_ADDR a    = info.Address.ullLong;
                dev.addressStr = QString("%1:%2:%3:%4:%5:%6")
                    .arg((a >> 40) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >> 32) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >> 24) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >> 16) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >>  8) & 0xFF, 2, 16, QChar('0'))
                    .arg((a >>  0) & 0xFF, 2, 16, QChar('0'))
                    .toUpper();
                found.append(dev);
            } while (BluetoothFindNextDevice(hFind, &info));
            BluetoothFindDeviceClose(hFind);
        }
        emit scanDone(found);
    }
signals:
    void scanDone(QList<BtDevice> devices);
};

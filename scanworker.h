#pragma once
#include <QObject>
#include <QList>
#include "btdevice.h"

class ScanWorker : public QObject {
    Q_OBJECT
public slots:
    void scan();
signals:
    void scanDone(QList<BtDevice> devices);
};

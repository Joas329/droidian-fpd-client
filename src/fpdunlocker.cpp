// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>

#include <QCoreApplication>
#include <QTextStream>
#include <QDebug>
#include <QProcess>
#include <QThread>
#include <QFile>
#include "fpdinterface.h"
#include "journallistener.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QStringList args = QCoreApplication::arguments();

    if(args.count() != 2) {
        qCritical() << "Invalid number of arguments. Usage: droidian-fpd-unlocker <session_id>";
        return 1;
    }

    QString sessionId = args[1];

    FPDInterface fpdInterface;
    JournalctlListener* listener = new JournalctlListener();
    QThread* listenerThread = new QThread();

    listener->moveToThread(listenerThread);

    QObject::connect(listenerThread, &QThread::started, listener, &JournalctlListener::runCommand);
    QObject::connect(listener, &JournalctlListener::commandFinished, [&]() {
        listenerThread->quit();
        listenerThread->wait();
        delete listener;
        QCoreApplication::exit(0);
    });

    QObject::connect(&fpdInterface, &FPDInterface::identified, [sessionId](const QString &finger) {
        qDebug() << "Identified finger:" << finger;

        QProcess checkScreen;
        checkScreen.start("batman-helper", QStringList() << "wlrdisplay");
        checkScreen.waitForFinished();

        QString batmanOutput(checkScreen.readAllStandardOutput());

        if (batmanOutput.trimmed() == "yes") {
            QProcess *process = new QProcess();
            QString vibra_good = "fbcli -E bell-terminal";
            QString unlock = "loginctl unlock-session " + sessionId;
            QString command = vibra_good + " && " + unlock;
            process->start("bash", QStringList() << "-c" << command);

            QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [&](int exitCode, QProcess::ExitStatus exitStatus) {
                    QCoreApplication::exit(exitCode);
                });
        }
        else {
            QCoreApplication::exit(0);
        }
    });

    QObject::connect(&fpdInterface, &FPDInterface::errorInfo, [](const QString &info) {
        qDebug() << "Error info:" << info;

        QProcess checkScreen;
        checkScreen.start("batman-helper", QStringList() << "wlrdisplay");
        checkScreen.waitForFinished();

        QString batmanOutput(checkScreen.readAllStandardOutput());

        if (info.contains("FINGER_NOT_RECOGNIZED") && batmanOutput.trimmed() == "yes") {
            QProcess *process = new QProcess();
            QString vibra_bad = "for i in {0..2}; do fbcli -E button-pressed; done";
            process->start("bash", QStringList() << "-c" << vibra_bad);

            QCoreApplication::exit(1);
        } else if (info.contains("ERROR_CANCELED") && batmanOutput.trimmed() != "no") {
            QCoreApplication::exit(1);
        } else {
            QCoreApplication::exit(0);
        }
    });

    qDebug() << "Waiting for finger identification...";

    if (QFile::exists("/usr/lib/droidian/device/fpd-gkr-unlock")) {
        listenerThread->start();
    }

    fpdInterface.identify();

    return app.exec();
}

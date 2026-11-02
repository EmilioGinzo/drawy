/*
 * SPDX-FileCopyrightText: 2026 Emilio Ginzo
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "loadjobwayscribertest.hpp"
#include "jobs/loadjob.hpp"
#include "serializer/serializerutils.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
QTEST_MAIN(LoadJobWayScriberTest)
using namespace Qt::Literals::StringLiterals;

LoadJobWayScriberTest::LoadJobWayScriberTest(QObject *parent)
    : QObject{parent}
{
}

void LoadJobWayScriberTest::shouldLoadSessionAsPages()
{
    const QJsonObject red{{u"r"_s, 1.0}, {u"g"_s, 0.0}, {u"b"_s, 0.0}, {u"a"_s, 1.0}};
    const QJsonObject line{{u"Line"_s, QJsonObject{{u"x1"_s, 0}, {u"y1"_s, 0}, {u"x2"_s, 100}, {u"y2"_s, 0}, {u"color"_s, red}, {u"thick"_s, 2.0}}}};
    const QJsonObject blur{{u"BlurRect"_s, QJsonObject{{u"x"_s, 0}, {u"y"_s, 0}, {u"w"_s, 10}, {u"h"_s, 10}, {u"strength"_s, 4.0}}}};
    const QJsonArray shapes{QJsonObject{{u"id"_s, 1}, {u"shape"_s, line}}, QJsonObject{{u"id"_s, 2}, {u"shape"_s, blur}}};
    const QJsonObject board{{u"id"_s, u"whiteboard"_s}, {u"pages"_s, QJsonArray{QJsonObject{{u"shapes"_s, shapes}}}}, {u"active_page"_s, 0}};
    const QJsonObject session{{u"version"_s, 8}, {u"active_board_id"_s, u"whiteboard"_s}, {u"boards"_s, QJsonArray{board}}};

    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(QJsonDocument{session}.toJson());
    file.close();

    LoadJob loadJob(nullptr);
    loadJob.setFileName(file.fileName());
    QSignalSpy imported(&loadJob, &LoadJob::sessionImported);
    LoadJobUtil::DeserializeInfo loaded;
    connect(&loadJob, &LoadJob::loadDone, this, [&loaded](const LoadJobUtil::DeserializeInfo &info) {
        loaded = info;
    });
    loadJob.start();

    QCOMPARE(loaded.pages.count(), 1);
    QCOMPARE(loaded.pages.at(0).items.count(), 1);
    QCOMPARE(loaded.pages.at(0).items.at(0)->formType(), Item::FormType::Line);
    QCOMPARE(imported.count(), 1);
    QCOMPARE(imported.at(0).at(0).toStringList().count(), 1); // the blur region
}

void LoadJobWayScriberTest::shouldStillLoadDrawyFiles()
{
    const QJsonObject page{{u"page_name"_s, u"Page 1"_s},
                           {u"items"_s, QJsonArray{}},
                           {u"zoom_factor"_s, 1.0},
                           {u"offset_pos"_s, QJsonObject{{u"x"_s, 0}, {u"y"_s, 0}}}};
    const QJsonObject doc{{u"version"_s, SerializerUtils::pageVersion()}, {u"current_page"_s, 0}, {u"pages"_s, QJsonArray{page}}};

    QTemporaryFile file;
    QVERIFY(file.open());
    file.close();
    SerializerUtils::saveInFile(doc, file.fileName());

    LoadJob loadJob(nullptr);
    loadJob.setFileName(file.fileName());
    QSignalSpy imported(&loadJob, &LoadJob::sessionImported);
    QSignalSpy done(&loadJob, &LoadJob::loadDone);
    loadJob.start();

    QCOMPARE(done.count(), 1);
    QCOMPARE(imported.count(), 0);
}

#include "moc_loadjobwayscribertest.cpp"

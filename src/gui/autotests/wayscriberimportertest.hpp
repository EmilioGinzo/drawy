/*
 * SPDX-FileCopyrightText: 2026 Emilio Ginzo
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QObject>

class WayScriberImporterTest : public QObject
{
    Q_OBJECT
public:
    explicit WayScriberImporterTest(QObject *parent = nullptr);
    ~WayScriberImporterTest() override = default;

private Q_SLOTS:
    void shouldRecognizeSessions();
    void shouldReadGzipAndPlainSessions();
    void shouldMapBoardsToPages();
    void shouldConvertEveryShapeKind();
    void shouldReportLosses();
    void shouldReadLegacySessions();
    void shouldNotTakeOtherJsonForSessions();
    void shouldDeserializeIntoDrawyItems();
    void shouldKeepPressureStrokeWidths();
    void shouldReencodeNonPngImages();
    void shouldRejectBrokenSessions();
};

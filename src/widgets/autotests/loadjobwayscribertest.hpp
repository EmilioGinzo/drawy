/*
 * SPDX-FileCopyrightText: 2026 Emilio Ginzo
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QObject>

class LoadJobWayScriberTest : public QObject
{
    Q_OBJECT
public:
    explicit LoadJobWayScriberTest(QObject *parent = nullptr);
    ~LoadJobWayScriberTest() override = default;

private Q_SLOTS:
    void shouldLoadSessionAsPages();
    void shouldStillLoadDrawyFiles();
    void shouldFailOnOtherFiles_data();
    void shouldFailOnOtherFiles();
};

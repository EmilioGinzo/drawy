/*
 * SPDX-FileCopyrightText: 2026 Laurent Montel <montel@kde.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "libdrawywidgets_private_export.h"
#include "loadjobutil.hpp"
#include <QObject>
#include <QStringList>
class ApplicationContext;
class LIBDRAWYWIDGETS_TESTS_EXPORT LoadJob : public QObject
{
    Q_OBJECT
public:
    explicit LoadJob(ApplicationContext *context, QObject *parent = nullptr);
    ~LoadJob() override;

    [[nodiscard]] bool canStart() const;

    void start();

    [[nodiscard]] QString fileName() const;
    void setFileName(const QString &newFileName);
    void setIsAutoSave(bool value);

Q_SIGNALS:
    void loadDone(const LoadJobUtil::DeserializeInfo &info);
    void loadFailed();
    // Emitted after loadDone when the file was a WayScriber session: lists what the conversion lost.
    void sessionImported(const QStringList &warnings);

private:
    LIBDRAWYWIDGETS_NO_EXPORT void slotDeserializeDone(const LoadJobUtil::DeserializeInfo &info);
    LIBDRAWYWIDGETS_NO_EXPORT void slotDeserializeFailed();
    QString mFileName;
    QStringList mImportWarnings;
    bool mIsAutoSave{false};
    bool mIsImport{false};
    ApplicationContext *const mApplicationContext;
};

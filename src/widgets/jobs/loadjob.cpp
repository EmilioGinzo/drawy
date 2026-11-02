/*
 * SPDX-FileCopyrightText: 2026 Laurent Montel <montel@kde.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "loadjob.hpp"
#include "common/utils/compression.hpp"
#include "context/applicationcontext.hpp"
#include "drawy_debug.h"
#include "jobs/deserializejob.hpp"
#include "serializer/wayscriberimporter.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>
LoadJob::LoadJob(ApplicationContext *context, QObject *parent)
    : QObject{parent}
    , mApplicationContext(context)
{
}

LoadJob::~LoadJob() = default;

bool LoadJob::canStart() const
{
    return !mFileName.isEmpty();
}

void LoadJob::start()
{
    if (!canStart()) {
        qCWarning(DRAWY_LOG) << "File path is not defined";
        Q_EMIT loadFailed();
        deleteLater();
        return;
    }

    QFile file(mFileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(DRAWY_LOG) << "Failed to open file:" << file.errorString();
        Q_EMIT loadFailed();
        deleteLater();
        return;
    }

    const QByteArray fileData = file.readAll();
    file.close();

    QJsonObject docObj;
    if (WayScriberImporter::isSession(fileData)) {
        auto imported = WayScriberImporter::importSession(fileData);
        if (!imported.ok) {
            qCWarning(DRAWY_LOG) << "WayScriber import failed:" << imported.error;
            Q_EMIT loadFailed();
            deleteLater();
            return;
        }
        mIsImport = true;
        mImportWarnings = std::move(imported.warnings);
        docObj = std::move(imported.document);
    } else {
        QByteArray byteArray;
        try {
            byteArray = Common::Utils::Compression::decompressData(fileData);
        } catch (const std::runtime_error &error) {
            // Not a Drawy document (or a damaged one): report it instead of letting the exception abort the app.
            qCWarning(DRAWY_LOG) << "Failed to decompress file:" << error.what();
            Q_EMIT loadFailed();
            deleteLater();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(byteArray, &parseError);
        if (doc.isNull() || !doc.isObject()) {
            qCWarning(DRAWY_LOG) << "JSON parse failed:" << parseError.errorString() << "offset:" << parseError.offset;
            Q_EMIT loadFailed();
            deleteLater();
            return;
        }
        docObj = doc.object();
    }

    auto job = new DeserializeJob(this);
    job->setJsonObject(docObj);
    connect(job, &DeserializeJob::deserializeDone, this, &LoadJob::slotDeserializeDone);
    connect(job, &DeserializeJob::deserializeFailed, this, &LoadJob::slotDeserializeFailed);

    job->start();
}

void LoadJob::slotDeserializeFailed()
{
    Q_EMIT loadFailed();
    deleteLater();
}

void LoadJob::slotDeserializeDone(const LoadJobUtil::DeserializeInfo &info)
{
    Q_EMIT loadDone(info);

    if (mIsImport) {
        // An imported session is a new, unsaved drawing: saving must ask for a .drawy name, never overwrite the session.
        if (mApplicationContext) {
            mApplicationContext->setCurrentFileModified(true);
        }
        Q_EMIT sessionImported(mImportWarnings);
    } else if (!mIsAutoSave) {
        if (mApplicationContext) {
            mApplicationContext->setCurrentFileModified(false);
            mApplicationContext->setCurrentFileName(mFileName);
        }
    }

    deleteLater();
}

QString LoadJob::fileName() const
{
    return mFileName;
}

void LoadJob::setFileName(const QString &newFileName)
{
    mFileName = newFileName;
}

void LoadJob::setIsAutoSave(bool value)
{
    mIsAutoSave = value;
}

#include "moc_loadjob.cpp"

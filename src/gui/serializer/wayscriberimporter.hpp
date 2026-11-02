/*
 * SPDX-FileCopyrightText: 2026 Emilio Ginzo
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "libdrawygui_export.h"
#include <QByteArray>
#include <QJsonObject>
#include <QStringList>

/**
 * Reads a WayScriber session file (gzip-compressed or plain JSON, as written
 * to ~/.local/share/wayscriber/session-*.json or a *.wayscriber-session export)
 * and converts it into a Drawy document of the page format, ready for
 * DeserializeJob.
 *
 * Every WayScriber board page with shapes becomes a Drawy page. Shapes Drawy
 * cannot represent are simplified or skipped; each loss is listed in
 * ImportResult::warnings.
 */
namespace WayScriberImporter
{
struct ImportResult {
    bool ok{false};
    QString error;
    QJsonObject document;
    QStringList warnings;
};

/**
 * @brief Whether the file data is a WayScriber session (gzip or JSON with boards), not a Drawy file.
 */
[[nodiscard]] LIBDRAWYGUI_EXPORT bool isSession(const QByteArray &fileData);

[[nodiscard]] LIBDRAWYGUI_EXPORT ImportResult importSession(const QByteArray &fileData);

/**
 * @brief Inflates gzip data; returns an empty array on error or if it expands past maxSize.
 */
[[nodiscard]] LIBDRAWYGUI_EXPORT QByteArray gunzip(const QByteArray &data, qsizetype maxSize);
} // namespace WayScriberImporter

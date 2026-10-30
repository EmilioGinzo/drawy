/*
 * SPDX-FileCopyrightText: 2026 Emilio Ginzo
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "itemcommand.hpp"
#include <QRect>
#include <QRectF>

class ImageItem;

class CropImageCommand : public ItemCommand
{
public:
    // newSourceRect is in pixmap pixels
    CropImageCommand(const std::shared_ptr<ImageItem> &item, const QRect &newSourceRect);

    void redo(ApplicationContext *context) override;
    void undo(ApplicationContext *context) override;

    [[nodiscard]] QString text() const override;

private:
    void apply(ApplicationContext *context, const QRect &sourceRect, const QRectF &box);

    std::shared_ptr<ImageItem> m_image;
    QRect m_oldSourceRect;
    QRect m_newSourceRect;
    QRectF m_oldBox;
    QRectF m_newBox;
};

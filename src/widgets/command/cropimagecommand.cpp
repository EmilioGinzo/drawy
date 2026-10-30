/*
 * SPDX-FileCopyrightText: 2026 Emilio Ginzo
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cropimagecommand.hpp"

#include "context/applicationcontext.hpp"
#include "context/coordinatetransformer.hpp"
#include "context/renderingcontext.hpp"
#include "context/spatialcontext.hpp"
#include "data-structures/cachegrid.hpp"
#include "drawy_command_debug.h"
#include "item/image.hpp"
#include <KLocalizedString>

CropImageCommand::CropImageCommand(const std::shared_ptr<ImageItem> &item, const QRect &newSourceRect)
    : ItemCommand{{item}}
    , m_image{item}
    , m_oldSourceRect{item->sourceRect()}
    , m_newSourceRect{newSourceRect}
    , m_oldBox{item->normalizedBoundingBox()}
{
    qCDebug(DRAWY_COMMAND_LOG) << "CropImageCommand" << m_oldSourceRect << "->" << m_newSourceRect;

    // keep the visible part of the image at the same place and scale on the canvas
    const qreal scaleX{m_oldBox.width() / m_oldSourceRect.width()};
    const qreal scaleY{m_oldBox.height() / m_oldSourceRect.height()};
    m_newBox = QRectF{m_oldBox.left() + (m_newSourceRect.left() - m_oldSourceRect.left()) * scaleX,
                      m_oldBox.top() + (m_newSourceRect.top() - m_oldSourceRect.top()) * scaleY,
                      m_newSourceRect.width() * scaleX,
                      m_newSourceRect.height() * scaleY};
}

void CropImageCommand::redo(ApplicationContext *context)
{
    apply(context, m_newSourceRect, m_newBox);
}

void CropImageCommand::undo(ApplicationContext *context)
{
    apply(context, m_oldSourceRect, m_oldBox);
}

void CropImageCommand::apply(ApplicationContext *context, const QRect &sourceRect, const QRectF &box)
{
    auto &transformer{context->spatialContext()->coordinateTransformer()};
    auto &cacheGrid{context->renderingContext()->cacheGrid()};

    QRectF dirtyRegion{m_image->boundingBox()};

    m_image->setSourceRect(sourceRect);
    m_image->setBox(box);

    dirtyRegion |= m_image->boundingBox();
    cacheGrid.markDirty(transformer.worldToGrid(dirtyRegion).toAlignedRect());
}

QString CropImageCommand::text() const
{
    return i18n("Crop Image");
}

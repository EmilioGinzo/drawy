// SPDX-FileCopyrightText: 2025 Prayag Jain <prayagjain2@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "libdrawywidgets_private_export.h"
#include "tools/tool.hpp"
#include "transformhandler/transformhandler.hpp"
class Item;
class ImageItem;

class LIBDRAWYWIDGETS_TESTS_EXPORT SelectionTool : public Tool
{
public:
    explicit SelectionTool(ApplicationContext *context);
    ~SelectionTool() override = default;

    [[nodiscard]] QString icon() const override;

    void mousePressed(ApplicationContext *context) override;
    void mouseMoved(ApplicationContext *context) override;
    void mouseReleased(ApplicationContext *context) override;
    void keyPressed(ApplicationContext *context) override;
    void mouseDoubleClick(ApplicationContext *context) override;
    void cleanup() override;

    void renderHighlights(ApplicationContext *context);

    [[nodiscard]] QList<Property::Type> properties() const override;

    [[nodiscard]] Tool::Type type() const override;

    [[nodiscard]] QList<std::shared_ptr<Item>> getItemsUnderCursor(ApplicationContext *context) const;

    // Crop mode: the image shows crop handles on the canvas until the crop is confirmed (Enter, double click
    // or a click outside the image) or cancelled (Escape)
    void beginCrop(ApplicationContext *context, const std::shared_ptr<ImageItem> &image);
    [[nodiscard]] bool isCropping() const;

private:
    LIBDRAWYWIDGETS_NO_EXPORT void endCrop(bool apply);
    LIBDRAWYWIDGETS_NO_EXPORT void paintCrop();
    [[nodiscard]] LIBDRAWYWIDGETS_NO_EXPORT bool cropStillValid() const;
    LIBDRAWYWIDGETS_NO_EXPORT int cropHandleAt(const QPointF &viewPos);
    LIBDRAWYWIDGETS_NO_EXPORT QPointF cropLocalPos(const QPointF &viewPos);
    LIBDRAWYWIDGETS_NO_EXPORT void cropMousePressed();
    LIBDRAWYWIDGETS_NO_EXPORT void cropMouseMoved();
    LIBDRAWYWIDGETS_NO_EXPORT void cropMouseReleased();

    std::shared_ptr<ImageItem> m_cropImage;
    QRectF m_cropRect; // in the local coordinates of the image
    QRectF m_cropFullRect; // where the whole uncropped image would be
    int m_cropHandle{-1};
    QMetaObject::Connection m_cropToolChanged;
    QMetaObject::Connection m_cropRendered;
    // how the image looked when the crop started, to notice it changing under us (undo, redo, delete, page change)
    QRectF m_cropBoxSnapshot;
    QRect m_cropSourceSnapshot;
    QTransform m_cropTransformSnapshot;
    LIBDRAWYWIDGETS_NO_EXPORT void updateCurrentHandler(ApplicationContext *context);

    TransformHandler *m_curHandler{};
    TransformHandler::State m_curHandlerState;

    QPointF m_lastPos{};
    bool m_isSelecting{false};
    bool m_highlightDrawn{false};

    qint64 m_lastMouseMoveTime{};
};

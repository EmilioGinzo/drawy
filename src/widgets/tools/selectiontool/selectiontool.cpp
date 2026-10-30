// SPDX-FileCopyrightText: 2025 Prayag Jain <prayagjain2@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "selectiontool.hpp"
#include <KLocalizedString>

#include <QDateTime>
#include <set>

#include "canvas/canvas.hpp"
#include "command/commandhistory.hpp"
#include "command/cropimagecommand.hpp"
#include "command/deselectcommand.hpp"
#include "command/moveitemcommand.hpp"
#include "command/selectcommand.hpp"
#include "common/constants.hpp"
#include "components/toolbar.hpp"
#include "context/applicationcontext.hpp"
#include "context/coordinatetransformer.hpp"
#include "context/renderingcontext.hpp"
#include "context/selectioncontext.hpp"
#include "context/spatialcontext.hpp"
#include "context/uicontext.hpp"
#include "data-structures/quadtree.hpp"
#include "drawyglobalconfig.h"
#include "event/event.hpp"
#include "item/arrow/arrow.hpp"
#include "item/image.hpp"
#include "item/item.hpp"
#include <utility>

using namespace Qt::Literals::StringLiterals;
SelectionTool::SelectionTool(ApplicationContext *context)
    : Tool(context)
    , m_curHandlerState{TransformHandler::State::Unlocked}
{
    m_cursor = QCursor(Qt::ArrowCursor);
}

void SelectionTool::mousePressed(ApplicationContext *context)
{
    if (m_cropImage) {
        cropMousePressed();
        return;
    }

    updateCurrentHandler(context);

    if (m_curHandler) {
        const auto &selectedItems{context->selectionContext()->selectedItems()};
        const bool singleTextItem = selectedItems.size() == 1 && (*selectedItems.begin())->formType() == Item::FormType::Text;

        context->selectionContext()->setShouldRenderHandles(singleTextItem);
        m_curHandlerState = m_curHandler->mousePressed(context);
        return;
    }

    auto uiContext{context->uiContext()};
    auto event{uiContext->appEvent()};

    if (event->button() == Qt::LeftButton) {
        m_lastPos = uiContext->appEvent()->pos();

        auto spatialContext{context->spatialContext()};
        auto selectionContext{context->selectionContext()};
        auto renderingContext{context->renderingContext()};
        auto commandHistory{spatialContext->commandHistory()};
        auto transformer{spatialContext->coordinateTransformer()};

        auto lockState{TransformHandler::State::Locked};

        const auto &selectedItems{selectionContext->selectedItems()};

        if (!(event->modifiers() & Qt::ShiftModifier)) {
            QList<std::shared_ptr<Item>> items{selectedItems.begin(), selectedItems.end()};

            if (!items.empty()) {
                commandHistory->push(std::make_shared<DeselectCommand>(std::move(items)));
            }
        }

        const auto intersectingItems{getItemsUnderCursor(context)};

        if (intersectingItems.empty()) {
            m_isSelecting = true;
        } else {
            auto &item{intersectingItems.back()};

            if (!item->locked()) {
                if (!(event->modifiers() & Qt::ShiftModifier) || !selectedItems.contains(item)) {
                    commandHistory->push(std::make_shared<SelectCommand>(QList<std::shared_ptr<Item>>{item}));
                }
            }

            m_isSelecting = false;
            lockState = TransformHandler::State::Unlocked;
        }

        renderingContext->markForRender();
        renderingContext->markForUpdate();

        m_curHandlerState = lockState;
        return;
    }

    m_curHandlerState = TransformHandler::State::Locked;
}

void SelectionTool::mouseMoved(ApplicationContext *context)
{
    if (m_cropImage) {
        cropMouseMoved();
        return;
    }

    const auto curTime{QDateTime::currentMSecsSinceEpoch()};
    const auto minimumTime{1000 / context->renderingContext()->fps()};

    if (curTime - m_lastMouseMoveTime < minimumTime) {
        return;
    }

    m_lastMouseMoveTime = curTime;

    updateCurrentHandler(context);

    auto uiContext{context->uiContext()};

    renderHighlights(context);

    if (m_curHandler) {
        m_curHandlerState = m_curHandler->mouseMoved(context);
        return;
    }

    auto renderingContext{context->renderingContext()};
    renderingContext->canvas()->setCursor(Qt::ArrowCursor);

    if (!m_isSelecting) {
        return;
    }

    auto spatialContext{context->spatialContext()};
    auto selectionContext{context->selectionContext()};
    auto &transformer{spatialContext->coordinateTransformer()};

    renderingContext->canvas()->setOverlayBg(Qt::transparent);

    const QPointF curPos{uiContext->appEvent()->pos()};

    const QRectF selectionBox{m_lastPos, curPos};
    const QRectF worldSelectionBox{transformer.viewToWorld(selectionBox)};

    const bool selectOnOverlap = DrawyGlobalConfig::self()->selectionMode() == DrawyGlobalConfig::EnumSelectionMode::Overlap;

    QList<std::shared_ptr<Item>> intersectingItems{
        spatialContext->quadtree().queryItems(worldSelectionBox, [selectOnOverlap](const std::shared_ptr<Item> &item, const QRectF &rect) {
            if (item->locked()) {
                return false;
            }

            if (selectOnOverlap) {
                return item->intersects(rect);
            } else {
                return rect.contains(item->boundingBox());
            }
        })};

    selectionContext->setSelectedItems(intersectingItems.begin(), intersectingItems.end());

    // TODO: Remove magic numbers
    renderingContext->canvas()->paintOverlay([&](QPainter &painter) -> void {
        const QPen pen{Common::selectionBorderColor};
        painter.setPen(pen);

        painter.drawRect(selectionBox);
        painter.fillRect(selectionBox, Common::selectionBackgroundColor);
    });

    renderingContext->markForRender();
    renderingContext->markForUpdate();
}

void SelectionTool::mouseReleased(ApplicationContext *context)
{
    if (m_cropImage) {
        cropMouseReleased();
        return;
    }

    updateCurrentHandler(context);

    context->selectionContext()->setShouldRenderHandles(true);
    context->renderingContext()->markForRender();
    context->renderingContext()->markForUpdate();

    if (m_curHandler) {
        m_curHandlerState = m_curHandler->mouseReleased(context);
        return;
    }

    if (m_isSelecting) {
        auto renderingContext{context->renderingContext()};
        auto &selectedItems{context->selectionContext()->selectedItems()};

        if (!selectedItems.empty()) {
            QList<std::shared_ptr<Item>> items;

            for (const auto &item : selectedItems) {
                if (!item->locked()) {
                    items.push_back(item);
                }
            }

            context->selectionContext()->reset();

            auto commandHistory{context->spatialContext()->commandHistory()};
            commandHistory->push(std::make_shared<SelectCommand>(std::move(items)));
        }

        renderingContext->canvas()->setOverlayBg(Qt::transparent);
        renderingContext->markForUpdate();

        m_isSelecting = false;
    }

    m_curHandlerState = TransformHandler::State::Unlocked;
}

void SelectionTool::renderHighlights(ApplicationContext *context)
{
    auto canvas{context->renderingContext()->canvas()};

    if (m_highlightDrawn) {
        canvas->setOverlayBg(canvas->overlayBg());
        m_highlightDrawn = false;

        context->renderingContext()->markForUpdate();
    }

    if (!m_isSelecting && context->selectionContext()->shouldRenderHandles()) {
        const auto intersectingItems{getItemsUnderCursor(context)};
        auto transformer{context->spatialContext()->coordinateTransformer()};

        if (!intersectingItems.empty()) {
            canvas->paintOverlay([&intersectingItems, &transformer](QPainter &painter) {
                QPen pen{Common::selectionBorderColor};
                pen.setStyle(Qt::DotLine);

                painter.setPen(pen);
                painter.drawPolygon(transformer.worldToView(intersectingItems.back()->displayBoundingBox()));
            });

            m_highlightDrawn = true;
        }

        context->renderingContext()->markForUpdate();
    }
}

void SelectionTool::cleanup()
{
    // the context menu calls this right after the action that starts the crop ran
    if (m_cropImage) {
        return;
    }

    mouseReleased(m_context);

    auto canvas{m_context->renderingContext()->canvas()};
    canvas->setOverlayBg(canvas->overlayBg());
}

void SelectionTool::updateCurrentHandler(ApplicationContext *context)
{
    if (m_curHandlerState == TransformHandler::State::Locked) {
        return;
    }

    auto selectionContext{context->selectionContext()};
    auto transformer{context->spatialContext()->coordinateTransformer()};

    const auto &selectedItems{selectionContext->selectedItems()};

    auto handlerTypes{selectionContext->defaultHandlerTypesForMultiSelection()};
    if (selectedItems.size() == 1) {
        handlerTypes = (*selectedItems.begin())->transformHandlers();
    }

    for (const auto &handlerType : std::as_const(handlerTypes)) {
        auto handler{TransformHandler::getHandler(handlerType)};

        if (handler->shouldActivate(context)) {
            m_curHandler = handler;
            return;
        }
    }

    m_curHandler = nullptr;
}

void SelectionTool::keyPressed(ApplicationContext *context)
{
    if (m_cropImage) {
        switch (context->uiContext()->appEvent()->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            endCrop(true);
            break;
        case Qt::Key_Escape:
            endCrop(false);
            break;
        default:
            break;
        }
        return;
    }

    const auto &selectedItems{context->selectionContext()->selectedItems()};
    if (selectedItems.empty()) {
        return;
    }

    auto event{context->uiContext()->appEvent()};
    auto commandHistory{context->spatialContext()->commandHistory()};
    QList<std::shared_ptr<Item>> items{selectedItems.begin(), selectedItems.end()};

    int delta{Common::translationDelta};
    if (event->modifiers() & Qt::ShiftModifier) {
        delta = Common::shiftTranslationDelta;
    }

    const QPointF worldOriginalPos{context->selectionContext()->selectionBox().boundingRect().topLeft()};

    bool updated{true};
    QPointF worldFinalPos{worldOriginalPos};

    switch (event->key()) {
    case Qt::Key_Left:
        worldFinalPos.setX(worldFinalPos.x() - delta);
        break;
    case Qt::Key_Right:
        worldFinalPos.setX(worldFinalPos.x() + delta);
        break;
    case Qt::Key_Up:
        worldFinalPos.setY(worldFinalPos.y() - delta);
        break;
    case Qt::Key_Down:
        worldFinalPos.setY(worldFinalPos.y() + delta);
        break;
    default:
        updated = false;
    }

    if (updated) {
        commandHistory->push(std::make_shared<MoveItemCommand>(std::move(items), worldOriginalPos, worldFinalPos));
        context->renderingContext()->markForRender();
        context->renderingContext()->markForUpdate();
    }
}

QList<Property::Type> SelectionTool::properties() const
{
    if (m_context) {
        const auto &selectedItems{m_context->selectionContext()->selectedItems()};

        std::set<Property::Type> result;
        bool hasArrow = false;
        for (const auto &item : selectedItems) {
            const auto &propertyTypes{item->propertyTypes()};
            for (const auto &property : std::as_const(propertyTypes)) {
                result.insert(property);
                if (!hasArrow && dynamic_cast<const ArrowItem *>(item.get())) {
                    hasArrow = true;
                }
            }
        }

        QList<Property::Type> output(result.begin(), result.end());
        if (selectedItems.size() > 1) {
            output += QList<Property::Type>{Property::Type::Alignment};
        }

        if (!selectedItems.empty()) {
            output += QList<Property::Type>{Property::Type::ZOrder, Property::Type::Actions};
        }

        return output;
    } else {
        return {};
    }
}

void SelectionTool::mouseDoubleClick(ApplicationContext *context)
{
    if (m_cropImage) {
        endCrop(true);
        return;
    }

    const auto &items{getItemsUnderCursor(context)};

    if (!items.empty() && items.back()->formType() == Item::FormType::Text && !items.back()->locked()) {
        auto toolBar{context->uiContext()->toolBar()};

        toolBar->changeTool(Tool::Type::Text);
        toolBar->curTool().mousePressed(context);
    }
}

QList<std::shared_ptr<Item>> SelectionTool::getItemsUnderCursor(ApplicationContext *context) const
{
    auto spatialContext{context->spatialContext()};
    auto transformer{spatialContext->coordinateTransformer()};

    constexpr int cursorHitSize{Common::selectionCursorHitSize};
    const QPointF worldPos{transformer.viewToWorld(context->uiContext()->appEvent()->pos())};
    const QRectF cursorRegion{worldPos.x() - cursorHitSize / 2.0, worldPos.y() - cursorHitSize / 2.0, cursorHitSize, cursorHitSize};

    return spatialContext->quadtree().queryItems(cursorRegion, [](const std::shared_ptr<Item> &item, auto &region) {
        return item->intersects(region);
    });
}

Tool::Type SelectionTool::type() const
{
    return Tool::Type::Selection;
}

QString SelectionTool::icon() const
{
    return u"edit-select"_s;
}

void SelectionTool::beginCrop(ApplicationContext *context, const std::shared_ptr<ImageItem> &image)
{
    endCrop(false);

    const QRectF box{image->normalizedBoundingBox()};
    const QRect source{image->sourceRect()};
    const qreal scaleX{box.width() / source.width()};
    const qreal scaleY{box.height() / source.height()};

    m_cropImage = image;
    m_cropRect = box;
    m_cropFullRect = QRectF{box.left() - source.left() * scaleX, box.top() - source.top() * scaleY, image->pixmap().width() * scaleX, image->pixmap().height() * scaleY};
    m_cropHandle = -1;

    // leaving the selection tool cancels the crop
    auto toolBar{context->uiContext()->toolBar()};
    m_cropToolChanged = QObject::connect(toolBar, &ToolBar::toolChanged, toolBar, [this](Tool &tool) {
        if (tool.type() != Tool::Type::Selection) {
            endCrop(false);
        }
    });

    context->selectionContext()->setShouldRenderHandles(false);
    context->renderingContext()->markForRender();
    paintCrop();
}

bool SelectionTool::isCropping() const
{
    return m_cropImage != nullptr;
}

void SelectionTool::endCrop(bool apply)
{
    if (!m_cropImage) {
        return;
    }

    QObject::disconnect(m_cropToolChanged);
    const auto image{std::move(m_cropImage)};
    m_cropImage = nullptr;
    m_cropHandle = -1;

    const QRect oldSource{image->sourceRect()};
    const QRectF box{image->normalizedBoundingBox()};
    const qreal scaleX{box.width() / oldSource.width()};
    const qreal scaleY{box.height() / oldSource.height()};

    auto canvas{m_context->renderingContext()->canvas()};
    canvas->setOverlayBg(canvas->overlayBg());
    canvas->setCursor(Qt::ArrowCursor);
    m_context->selectionContext()->setShouldRenderHandles(true);

    if (apply) {
        const QPoint topLeft{qRound((m_cropRect.left() - m_cropFullRect.left()) / scaleX), qRound((m_cropRect.top() - m_cropFullRect.top()) / scaleY)};
        const QSize size{std::max(1, qRound(m_cropRect.width() / scaleX)), std::max(1, qRound(m_cropRect.height() / scaleY))};
        const QRect newSource{QRect{topLeft, size}.intersected(image->pixmap().rect())};

        if (!newSource.isEmpty() && newSource != oldSource) {
            m_context->spatialContext()->commandHistory()->push(std::make_shared<CropImageCommand>(image, newSource));
        }
    }

    m_context->renderingContext()->markForRender();
    m_context->renderingContext()->markForUpdate();
}

// handles go clockwise from the top left corner: corners at even indexes, edge middles at odd ones
static QPointF cropHandlePoint(const QRectF &rect, int index)
{
    switch (index) {
    case 0:
        return rect.topLeft();
    case 1:
        return {rect.center().x(), rect.top()};
    case 2:
        return rect.topRight();
    case 3:
        return {rect.right(), rect.center().y()};
    case 4:
        return rect.bottomRight();
    case 5:
        return {rect.center().x(), rect.bottom()};
    case 6:
        return rect.bottomLeft();
    default:
        return {rect.left(), rect.center().y()};
    }
}

static QPolygonF cropQuad(const QRectF &rect)
{
    return QPolygonF{{rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()}};
}

void SelectionTool::paintCrop()
{
    auto canvas{m_context->renderingContext()->canvas()};
    auto transformer{m_context->spatialContext()->coordinateTransformer()};

    const auto toView{[this, &transformer](const QRectF &rect) {
        return transformer.worldToView(m_cropImage->transformObj().map(cropQuad(rect)));
    }};
    const QPolygonF fullView{toView(m_cropFullRect)};
    const QPolygonF cropView{toView(m_cropRect)};

    QTransform localToView;
    QTransform::quadToQuad(cropQuad(m_cropFullRect), fullView, localToView);

    canvas->setOverlayBg(canvas->overlayBg());
    canvas->paintOverlay([&](QPainter &painter) {
        QPainterPath outside;
        outside.addPolygon(fullView);
        outside.closeSubpath();
        QPainterPath hole;
        hole.addPolygon(cropView);
        hole.closeSubpath();
        const QPainterPath dimmed{outside.subtracted(hole)};

        // the whole image, dimmed, where it is cut away
        painter.save();
        painter.setClipPath(dimmed);
        painter.setTransform(localToView);
        painter.drawPixmap(m_cropFullRect, m_cropImage->pixmap(), m_cropImage->pixmap().rect());
        painter.resetTransform();
        painter.fillPath(dimmed, QColor(0, 0, 0, 140));
        painter.restore();

        painter.setPen(Common::selectionBorderPen());
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(cropView);

        painter.setBrush(Qt::white);
        painter.setPen(QPen{Common::selectionBorderColor, 1});
        constexpr qreal size{Common::selectionHandleSize};
        for (int i = 0; i < 8; i++) {
            const QPointF point{transformer.worldToView(m_cropImage->transformObj().map(cropHandlePoint(m_cropRect, i)))};
            painter.drawRect(QRectF{point.x() - size / 2, point.y() - size / 2, size, size});
        }
    });

    m_context->renderingContext()->markForUpdate();
}

int SelectionTool::cropHandleAt(const QPointF &viewPos)
{
    auto transformer{m_context->spatialContext()->coordinateTransformer()};

    for (int i = 0; i < 8; i++) {
        const QPointF point{transformer.worldToView(m_cropImage->transformObj().map(cropHandlePoint(m_cropRect, i)))};
        if (QLineF{point, viewPos}.length() <= Common::selectionHandleSize) {
            return i;
        }
    }
    return -1;
}

QPointF SelectionTool::cropLocalPos(const QPointF &viewPos)
{
    auto transformer{m_context->spatialContext()->coordinateTransformer()};
    return m_cropImage->transformObj().inverted().map(transformer.viewToWorld(viewPos));
}

void SelectionTool::cropMousePressed()
{
    auto event{m_context->uiContext()->appEvent()};
    if (event->button() != Qt::LeftButton) {
        return;
    }

    m_cropHandle = cropHandleAt(event->pos());
    if (m_cropHandle < 0 && !m_cropFullRect.contains(cropLocalPos(event->pos()))) {
        endCrop(true);
    }
}

void SelectionTool::cropMouseMoved()
{
    auto event{m_context->uiContext()->appEvent()};
    auto canvas{m_context->renderingContext()->canvas()};

    if (m_cropHandle < 0) {
        static constexpr Qt::CursorShape cursors[]{Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
        const int hovered{cropHandleAt(event->pos())};
        canvas->setCursor(hovered < 0 ? Qt::ArrowCursor : cursors[hovered % 4]);
        return;
    }

    constexpr qreal minSize{8};
    const QPointF pos{cropLocalPos(event->pos())};
    QRectF rect{m_cropRect};

    if (m_cropHandle == 0 || m_cropHandle == 6 || m_cropHandle == 7) {
        rect.setLeft(std::clamp(pos.x(), m_cropFullRect.left(), rect.right() - minSize));
    } else if (m_cropHandle >= 2 && m_cropHandle <= 4) {
        rect.setRight(std::clamp(pos.x(), rect.left() + minSize, m_cropFullRect.right()));
    }
    if (m_cropHandle <= 2) {
        rect.setTop(std::clamp(pos.y(), m_cropFullRect.top(), rect.bottom() - minSize));
    } else if (m_cropHandle >= 4 && m_cropHandle <= 6) {
        rect.setBottom(std::clamp(pos.y(), rect.top() + minSize, m_cropFullRect.bottom()));
    }

    m_cropRect = rect;
    paintCrop();
}

void SelectionTool::cropMouseReleased()
{
    m_cropHandle = -1;
}

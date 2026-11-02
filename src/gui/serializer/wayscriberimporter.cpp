/*
 * SPDX-FileCopyrightText: 2026 Emilio Ginzo
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "wayscriberimporter.hpp"
#include "drawyglobalconfig.h"
#include "serializer/serializerutils.hpp"

#include <KLocalizedString>
#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <zlib.h>

using namespace Qt::Literals::StringLiterals;

namespace
{
// WayScriber refuses sessions that expand past this size; so does the importer.
constexpr qsizetype maxSessionSize{128 * 1024 * 1024};

// Margin QTextDocument puts around a Drawy text box's contents.
constexpr qreal textDocumentMargin{4.0};

// Pressure at which a Drawy freeform stroke is exactly StrokeWidth wide, whatever the thinning setting.
constexpr qreal fullWidthPressure{0.5};

enum class Loss : uint8_t {
    EraserStroke,
    BlurRect,
    Spotlight,
    CurvedArrow,
    ArrowLabel,
    TextBackground,
    PolygonShape,
    UnreadableImage,
    UnknownShape,
};

class Converter
{
public:
    QJsonObject convert(const QJsonObject &session);
    [[nodiscard]] QStringList warnings() const;

private:
    QJsonArray convertShape(const QJsonObject &shape);

    QJsonObject freeform(const QJsonArray &points, qreal width, const QList<qreal> &pressures, const QJsonObject &color, int opacity);
    QJsonObject freehand(const QJsonObject &s);
    QJsonObject freehandPressure(const QJsonObject &s);
    QJsonObject marker(const QJsonObject &s);
    QJsonObject line(const QJsonObject &s);
    QJsonObject arrow(const QJsonObject &s);
    QJsonObject rectangle(const QJsonObject &s);
    QJsonObject ellipse(const QJsonObject &s);
    QJsonObject polygon(const QJsonObject &s);
    QJsonObject text(const QJsonObject &s);
    QJsonObject stickyNote(const QJsonObject &s);
    QJsonObject stepMarker(const QJsonObject &s);
    QJsonObject image(const QJsonObject &s);

    void lose(Loss loss);

    QMap<Loss, int> mLosses;
    QStringList mUnknownKinds;
};

QJsonObject point(qreal x, qreal y)
{
    return SerializerUtils::toJson(QPointF{x, y});
}

QJsonObject rect(const QRectF &r)
{
    return {{u"x"_s, r.x()}, {u"y"_s, r.y()}, {u"width"_s, r.width()}, {u"height"_s, r.height()}};
}

QJsonObject identityTransform()
{
    return {
        {u"m11"_s, 1.0},
        {u"m12"_s, 0.0},
        {u"m13"_s, 0.0},
        {u"m21"_s, 0.0},
        {u"m22"_s, 1.0},
        {u"m23"_s, 0.0},
        {u"m31"_s, 0.0},
        {u"m32"_s, 0.0},
        {u"m33"_s, 1.0},
    };
}

QJsonObject property(const QString &type, const QJsonValue &value)
{
    return {{u"type"_s, type}, {u"value"_s, value}};
}

QColor toColor(const QJsonObject &c)
{
    QColor color;
    color.setRgbF(std::clamp(c[u"r"_s].toDouble(), 0.0, 1.0),
                  std::clamp(c[u"g"_s].toDouble(), 0.0, 1.0),
                  std::clamp(c[u"b"_s].toDouble(), 0.0, 1.0),
                  std::clamp(c[u"a"_s].toDouble(1.0), 0.0, 1.0));
    return color;
}

// Drawy keeps colors opaque and puts transparency in the Opacity property.
QString rgbName(const QJsonObject &c)
{
    return toColor(c).name(QColor::HexRgb);
}

int opacity(const QJsonObject &c)
{
    return toColor(c).alpha();
}

int strokeWidth(qreal thick)
{
    return std::max(1, static_cast<int>(std::lround(thick)));
}

QJsonObject item(const QString &type, const QJsonArray &properties)
{
    return {
        {u"type"_s, type},
        {u"id"_s, QString::fromLatin1(QUuid::createUuid().toRfc4122().toHex())},
        {u"properties"_s, properties},
        {u"transform"_s, identityTransform()},
    };
}

QJsonArray strokeProperties(const QJsonObject &color, qreal thick, int opacityValue)
{
    return {
        property(u"StrokeColor"_s, rgbName(color)),
        property(u"StrokeWidth"_s, strokeWidth(thick)),
        property(u"Opacity"_s, opacityValue),
        property(u"StrokeStyle"_s, u"Solid"_s),
    };
}

QJsonArray strokeProperties(const QJsonObject &color, qreal thick)
{
    return strokeProperties(color, thick, opacity(color));
}

// WayScriber fills with the border color unless the shape has its own fill_color.
void appendFill(QJsonArray &properties, const QJsonObject &s)
{
    if (!s[u"fill"_s].toBool()) {
        properties.append(property(u"BackgroundStyle"_s, u"None"_s));
        return;
    }
    const QJsonObject fill{s[u"fill_color"_s].isObject() ? s[u"fill_color"_s].toObject() : s[u"color"_s].toObject()};
    properties.append(property(u"BackgroundStyle"_s, u"Solid"_s));
    properties.append(property(u"BackgroundColor"_s, rgbName(fill)));
}

QJsonObject boxItem(const QString &type, const QRectF &box, QJsonArray properties)
{
    QJsonObject result{item(type, properties)};
    result[u"start"_s] = point(box.left(), box.top());
    result[u"end"_s] = point(box.right(), box.bottom());
    return result;
}

QJsonObject groupItem(const QJsonArray &children)
{
    QJsonObject result{item(u"GROUP"_s, {})};
    result[u"items"_s] = children;
    return result;
}

QString fontStyle(const QJsonObject &descriptor)
{
    QStringList parts;
    const QString weight{descriptor[u"weight"_s].toString().toLower()};
    bool numeric{false};
    const int weightValue{weight.toInt(&numeric)};
    if ((numeric && weightValue >= 600) || weight.contains(u"bold"_s) || weight == u"heavy"_s || weight == u"black"_s) {
        parts << u"Bold"_s;
    }
    const QString style{descriptor[u"style"_s].toString().toLower()};
    if (style == u"italic"_s || style == u"oblique"_s) {
        parts << u"Italic"_s;
    }
    return parts.isEmpty() ? u"Normal"_s : parts.join(u',');
}

QFont qtFont(const QJsonObject &descriptor, qreal size)
{
    QFont font{descriptor[u"family"_s].toString(u"Sans"_s)};
    font.setPointSizeF(std::max(1.0, size));
    const QString style{fontStyle(descriptor)};
    font.setBold(style.contains(u"Bold"_s));
    font.setItalic(style.contains(u"Italic"_s));
    return font;
}

// WayScriber anchors text at the first line's baseline; Drawy at the text box's top-left corner.
QJsonObject textItem(qreal x, qreal baseline, const QString &text, const QColor &color, qreal size, const QJsonObject &descriptor, const QJsonValue &wrapWidth)
{
    const QFontMetricsF metrics{qtFont(descriptor, size)};
    QJsonObject result{item(u"TEXT"_s,
                            {
                                property(u"StrokeColor"_s, color.name(QColor::HexRgb)),
                                property(u"Opacity"_s, color.alpha()),
                                property(u"FontSize"_s, std::max(1, static_cast<int>(std::lround(size)))),
                                property(u"FontFamily"_s, descriptor[u"family"_s].toString(u"Sans"_s)),
                                property(u"FontStyle"_s, fontStyle(descriptor)),
                                property(u"TextAlignment"_s, u"AlignLeft"_s),
                                property(u"ListStyle"_s, u"None"_s),
                            })};
    result[u"text"_s] = text;
    result[u"top_left"_s] = point(x - textDocumentMargin, baseline - metrics.ascent() - textDocumentMargin);
    if (wrapWidth.isDouble() && wrapWidth.toDouble() > 0) {
        result[u"wrap_width"_s] = wrapWidth.toDouble() + 2 * textDocumentMargin;
    }
    return result;
}

// Text that stays readable on a filled marker or note, as WayScriber picks it.
QColor contrastingText(const QColor &background)
{
    const qreal luminance{0.299 * background.redF() + 0.587 * background.greenF() + 0.114 * background.blueF()};
    return luminance > 0.6 ? QColor::fromRgbF(0.12f, 0.12f, 0.12f) : QColor{Qt::white};
}

QList<qreal> constantPressures(qsizetype count)
{
    return QList<qreal>(count, fullWidthPressure);
}

QJsonObject Converter::freeform(const QJsonArray &points, qreal width, const QList<qreal> &pressures, const QJsonObject &color, int opacityValue)
{
    QJsonObject result{item(u"FREEFORM"_s, strokeProperties(color, width, opacityValue))};
    QJsonArray drawyPoints;
    for (const auto &p : points) {
        const QJsonArray xy = p.toArray();
        drawyPoints.append(point(xy.at(0).toDouble(), xy.at(1).toDouble()));
    }
    result[u"points"_s] = drawyPoints;
    result[u"pressures"_s] = QJsonArray::fromVariantList(QVariantList{pressures.cbegin(), pressures.cend()});
    result[u"simulatePressure"_s] = false;
    return result;
}

QJsonObject Converter::freehand(const QJsonObject &s)
{
    const QJsonArray points = s[u"points"_s].toArray();
    const QJsonObject color{s[u"color"_s].toObject()};
    return freeform(points, s[u"thick"_s].toDouble(), constantPressures(points.size()), color, opacity(color));
}

// WayScriber stores a width per sample; Drawy a stroke width scaled by a pressure per sample.
// Drawy's stroke radius is width * (0.5 - thinning * (0.5 - pressure)), so solve that for the pressure.
QJsonObject Converter::freehandPressure(const QJsonObject &s)
{
    const QJsonArray samples = s[u"points"_s].toArray();
    qreal maxWidth{0};
    for (const auto &sample : samples) {
        maxWidth = std::max(maxWidth, sample.toArray().at(2).toDouble());
    }
    const qreal width{static_cast<qreal>(strokeWidth(maxWidth))};
    const qreal thinning{DrawyGlobalConfig::self()->thinning()};
    QList<qreal> pressures;
    pressures.reserve(samples.size());
    for (const auto &sample : samples) {
        const qreal sampleWidth{sample.toArray().at(2).toDouble()};
        const qreal pressure{thinning > 0 ? fullWidthPressure - (0.5 - sampleWidth / (2 * width)) / thinning : fullWidthPressure};
        pressures.append(std::clamp(pressure, 0.0, 1.0));
    }
    const QJsonObject color{s[u"color"_s].toObject()};
    return freeform(samples, width, pressures, color, opacity(color));
}

// WayScriber draws highlighter ink wider than its nominal size and at about a third of its alpha.
QJsonObject Converter::marker(const QJsonObject &s)
{
    const QJsonArray points = s[u"points"_s].toArray();
    const QJsonObject color{s[u"color"_s].toObject()};
    const qreal thick{s[u"thick"_s].toDouble()};
    const qreal alpha{std::clamp(toColor(color).alphaF() * 0.32, 0.05, 0.85)};
    return freeform(points, std::max(thick * 1.25, thick + 1.0), constantPressures(points.size()), color, static_cast<int>(std::lround(alpha * 255)));
}

QJsonObject Converter::line(const QJsonObject &s)
{
    QJsonObject result{item(u"LINE"_s, strokeProperties(s[u"color"_s].toObject(), s[u"thick"_s].toDouble()))};
    result[u"start"_s] = point(s[u"x1"_s].toDouble(), s[u"y1"_s].toDouble());
    result[u"end"_s] = point(s[u"x2"_s].toDouble(), s[u"y2"_s].toDouble());
    return result;
}

QJsonObject Converter::arrow(const QJsonObject &s)
{
    const QString style{s[u"style"_s].toString(u"standard"_s)};
    const bool headAtEnd{s[u"head_at_end"_s].toBool(true)};
    const bool doubleHead{style == u"double"_s};
    if (style == u"curved"_s && s[u"bend"_s].toDouble() != 0.0) {
        lose(Loss::CurvedArrow);
    }
    if (s[u"label"_s].isObject()) {
        lose(Loss::ArrowLabel);
    }

    QJsonArray properties = strokeProperties(s[u"color"_s].toObject(), s[u"thick"_s].toDouble());
    properties.append(property(u"StartArrowType"_s, doubleHead || !headAtEnd ? u"Filled"_s : u"None"_s));
    properties.append(property(u"EndArrowType"_s, doubleHead || headAtEnd ? u"Filled"_s : u"None"_s));
    QJsonObject result{item(u"ARROW"_s, properties)};
    result[u"start"_s] = point(s[u"x1"_s].toDouble(), s[u"y1"_s].toDouble());
    result[u"end"_s] = point(s[u"x2"_s].toDouble(), s[u"y2"_s].toDouble());
    return result;
}

QJsonObject Converter::rectangle(const QJsonObject &s)
{
    QJsonArray properties = strokeProperties(s[u"color"_s].toObject(), s[u"thick"_s].toDouble());
    appendFill(properties, s);
    properties.append(property(u"CornerRectangleType"_s, u"Pointed"_s));
    const QRectF box{s[u"x"_s].toDouble(), s[u"y"_s].toDouble(), s[u"w"_s].toDouble(), s[u"h"_s].toDouble()};
    return boxItem(u"RECTANGLE"_s, box, properties);
}

QJsonObject Converter::ellipse(const QJsonObject &s)
{
    QJsonArray properties = strokeProperties(s[u"color"_s].toObject(), s[u"thick"_s].toDouble());
    appendFill(properties, s);
    const qreal rx{s[u"rx"_s].toDouble()};
    const qreal ry{s[u"ry"_s].toDouble()};
    const QRectF box{s[u"cx"_s].toDouble() - rx, s[u"cy"_s].toDouble() - ry, 2 * rx, 2 * ry};
    return boxItem(u"ELLIPSE"_s, box, properties);
}

// A rhombus is Drawy's diamond; any other polygon becomes its closed outline.
QJsonObject Converter::polygon(const QJsonObject &s)
{
    const QJsonArray points = s[u"points"_s].toArray();
    if (points.isEmpty()) {
        return {};
    }
    if (s[u"kind"_s].toString() == u"Rhombus"_s) {
        QPolygonF corners;
        for (const auto &p : points) {
            const QJsonArray xy = p.toArray();
            corners << QPointF{xy.at(0).toDouble(), xy.at(1).toDouble()};
        }
        const QRectF box{corners.boundingRect()};
        QJsonArray properties = strokeProperties(s[u"color"_s].toObject(), s[u"thick"_s].toDouble());
        appendFill(properties, s);
        return boxItem(u"DIAMOND"_s, box, properties);
    }

    lose(Loss::PolygonShape);
    QJsonArray closed = points;
    closed.append(points.first());
    const QJsonObject color{s[u"color"_s].toObject()};
    return freeform(closed, s[u"thick"_s].toDouble(), constantPressures(closed.size()), color, opacity(color));
}

QJsonObject Converter::text(const QJsonObject &s)
{
    if (s[u"background_enabled"_s].toBool()) {
        lose(Loss::TextBackground);
    }
    return textItem(s[u"x"_s].toDouble(),
                    s[u"y"_s].toDouble(),
                    s[u"text"_s].toString(),
                    toColor(s[u"color"_s].toObject()),
                    s[u"size"_s].toDouble(),
                    s[u"font_descriptor"_s].toObject(),
                    s[u"wrap_width"_s]);
}

// A filled note rectangle behind the text, grouped, padded as WayScriber pads it.
QJsonObject Converter::stickyNote(const QJsonObject &s)
{
    const qreal x{s[u"x"_s].toDouble()};
    const qreal y{s[u"y"_s].toDouble()};
    const qreal size{s[u"size"_s].toDouble()};
    const QString noteText{s[u"text"_s].toString()};
    const QJsonObject descriptor{s[u"font_descriptor"_s].toObject()};
    const QJsonObject background{s[u"background"_s].toObject()};

    const QFontMetricsF metrics{qtFont(descriptor, size)};
    const qreal wrap{s[u"wrap_width"_s].toDouble()};
    const QRectF textBounds{metrics.boundingRect(QRectF{0, 0, wrap > 0 ? wrap : 1e6, 1e6}, Qt::TextWordWrap, noteText)};
    const qreal padX{std::max(size * 0.55, 6.0)};
    const qreal padY{std::max(size * 0.4, 4.0)};
    const QRectF box{x - padX, y - metrics.ascent() - padY, textBounds.width() + 2 * padX, textBounds.height() + 2 * padY};

    QJsonArray noteProperties = strokeProperties(background, 1);
    noteProperties.append(property(u"BackgroundStyle"_s, u"Solid"_s));
    noteProperties.append(property(u"BackgroundColor"_s, rgbName(background)));
    noteProperties.append(property(u"CornerRectangleType"_s, u"Rounded"_s));

    return groupItem({
        boxItem(u"RECTANGLE"_s, box, noteProperties),
        textItem(x, y, noteText, contrastingText(toColor(background)), size, descriptor, s[u"wrap_width"_s]),
    });
}

// A filled circle with its number centered on it, grouped, sized as WayScriber sizes it.
QJsonObject Converter::stepMarker(const QJsonObject &s)
{
    const QJsonObject label{s[u"label"_s].toObject()};
    const qreal size{label[u"size"_s].toDouble()};
    const QJsonObject descriptor{label[u"font_descriptor"_s].toObject()};
    const QString number{QString::number(label[u"value"_s].toInteger())};
    const QPointF center{s[u"x"_s].toDouble(), s[u"y"_s].toDouble()};

    const QFontMetricsF metrics{qtFont(descriptor, size)};
    const QRectF numberBounds{metrics.tightBoundingRect(number)};
    const qreal padding{std::max(size * 0.45, 6.0)};
    const qreal radius{std::max(std::max(numberBounds.width(), numberBounds.height()) / 2 + padding, 10.0)};

    const QJsonObject color{s[u"color"_s].toObject()};
    const QColor textColor{contrastingText(toColor(color))};
    QJsonArray circleProperties = strokeProperties(color, std::max(size * 0.12, 1.5));
    circleProperties.append(property(u"BackgroundStyle"_s, u"Solid"_s));
    circleProperties.append(property(u"BackgroundColor"_s, rgbName(color)));
    const QRectF circle{center.x() - radius, center.y() - radius, 2 * radius, 2 * radius};

    // Center the digits' ink box on the marker, then turn that into the baseline origin.
    const qreal originX{center.x() - numberBounds.center().x()};
    const qreal baseline{center.y() - numberBounds.center().y()};
    return groupItem({
        boxItem(u"ELLIPSE"_s, circle, circleProperties),
        textItem(originX, baseline, number, textColor, size, descriptor, QJsonValue{}),
    });
}

// Drawy items keep their pixels as PNG, so other formats are re-encoded.
QJsonObject Converter::image(const QJsonObject &s)
{
    const QJsonObject data{s[u"data"_s].toObject()};
    const QByteArray bytes{QByteArray::fromBase64(data[u"bytes"_s].toString().toLatin1())};
    QByteArray png{bytes};
    if (data[u"mime_type"_s].toString() != u"image/png"_s) {
        const QImage decoded{QImage::fromData(bytes)};
        png.clear();
        QBuffer buffer{&png};
        if (decoded.isNull() || !buffer.open(QIODevice::WriteOnly) || !decoded.save(&buffer, "PNG")) {
            lose(Loss::UnreadableImage);
            return {};
        }
    }
    QJsonObject result{item(u"IMAGE"_s, {property(u"Opacity"_s, 255)})};
    result[u"image_data"_s] = QString::fromLatin1(png.toBase64());
    result[u"bounding_box"_s] = rect(QRectF{s[u"x"_s].toDouble(), s[u"y"_s].toDouble(), s[u"w"_s].toDouble(), s[u"h"_s].toDouble()});
    return result;
}

QJsonArray Converter::convertShape(const QJsonObject &shape)
{
    if (shape.size() != 1) {
        lose(Loss::UnknownShape);
        return {};
    }
    const QString kind{shape.constBegin().key()};
    const QJsonObject s{shape.constBegin().value().toObject()};

    QJsonObject result;
    if (kind == u"Freehand"_s) {
        result = freehand(s);
    } else if (kind == u"FreehandPressure"_s) {
        result = freehandPressure(s);
    } else if (kind == u"MarkerStroke"_s) {
        result = marker(s);
    } else if (kind == u"Line"_s) {
        result = line(s);
    } else if (kind == u"Arrow"_s) {
        result = arrow(s);
    } else if (kind == u"Rect"_s) {
        result = rectangle(s);
    } else if (kind == u"Ellipse"_s) {
        result = ellipse(s);
    } else if (kind == u"Polygon"_s) {
        result = polygon(s);
    } else if (kind == u"Text"_s) {
        result = text(s);
    } else if (kind == u"StickyNote"_s) {
        result = stickyNote(s);
    } else if (kind == u"StepMarker"_s) {
        result = stepMarker(s);
    } else if (kind == u"Image"_s) {
        result = image(s);
    } else if (kind == u"EraserStroke"_s) {
        lose(Loss::EraserStroke);
    } else if (kind == u"BlurRect"_s) {
        lose(Loss::BlurRect);
    } else if (kind == u"Spotlight"_s) {
        lose(Loss::Spotlight);
    } else {
        lose(Loss::UnknownShape);
        if (!mUnknownKinds.contains(kind)) {
            mUnknownKinds.append(kind);
        }
    }
    return result.isEmpty() ? QJsonArray{} : QJsonArray{result};
}

QJsonObject Converter::convert(const QJsonObject &session)
{
    const QJsonArray boards = session[u"boards"_s].toArray();
    const QString activeBoard{session[u"active_board_id"_s].toString()};
    const bool severalBoards{boards.size() > 1};

    QJsonArray pages;
    int currentPage{0};
    for (const auto &boardValue : boards) {
        const QJsonObject board{boardValue.toObject()};
        const QString boardId{board[u"id"_s].toString()};
        const QJsonArray boardPages = board[u"pages"_s].toArray();
        const bool boardHasShapes{std::any_of(boardPages.cbegin(), boardPages.cend(), [](const QJsonValue &page) {
            return !page.toObject()[u"shapes"_s].toArray().isEmpty();
        })};
        // Unused boards (WayScriber always keeps the transparent and whiteboard ones) would only add blank pages.
        if (!boardHasShapes) {
            continue;
        }

        for (qsizetype index = 0; index < boardPages.size(); ++index) {
            const QJsonObject page{boardPages.at(index).toObject()};
            if (boardId == activeBoard && index == board[u"active_page"_s].toInt()) {
                currentPage = static_cast<int>(pages.size());
            }

            QJsonArray items;
            for (const auto &entry : page[u"shapes"_s].toArray()) {
                const QJsonObject entryObj{entry.toObject()};
                for (const auto &converted : convertShape(entryObj[u"shape"_s].toObject())) {
                    QJsonObject drawyItem{converted.toObject()};
                    drawyItem[u"zorder"_s] = QString::number(items.size());
                    if (entryObj[u"locked"_s].toBool()) {
                        drawyItem[u"locked"_s] = true;
                    }
                    items.append(drawyItem);
                }
            }

            QString name{page[u"page_name"_s].toString()};
            if (name.isEmpty() && (severalBoards || boardPages.size() > 1)) {
                name = boardPages.size() > 1 ? i18nc("@item page name: board name, page number", "%1 %2", boardId, index + 1) : boardId;
            }
            const QJsonArray viewOffset = page[u"view_offset"_s].toArray();
            pages.append(QJsonObject{
                {u"page_name"_s, name},
                {u"offset_pos"_s, point(viewOffset.at(0).toDouble(), viewOffset.at(1).toDouble())},
                {u"zoom_factor"_s, 1.0},
                {u"items"_s, items},
            });
        }
    }

    if (pages.isEmpty()) {
        pages.append(QJsonObject{
            {u"page_name"_s, QString{}},
            {u"offset_pos"_s, point(0, 0)},
            {u"zoom_factor"_s, 1.0},
            {u"items"_s, QJsonArray{}},
        });
    }

    return {
        {u"version"_s, SerializerUtils::pageVersion()},
        {u"current_page"_s, currentPage},
        {u"pages"_s, pages},
    };
}

void Converter::lose(Loss loss)
{
    ++mLosses[loss];
}

QStringList Converter::warnings() const
{
    QStringList result;
    for (auto it = mLosses.cbegin(); it != mLosses.cend(); ++it) {
        const int n{it.value()};
        switch (it.key()) {
        case Loss::EraserStroke:
            result << i18np("1 eraser stroke was skipped, so ink it erased is visible again.",
                            "%1 eraser strokes were skipped, so ink they erased is visible again.",
                            n);
            break;
        case Loss::BlurRect:
            result << i18np("1 blur region was skipped.", "%1 blur regions were skipped.", n);
            break;
        case Loss::Spotlight:
            result << i18np("1 spotlight was skipped.", "%1 spotlights were skipped.", n);
            break;
        case Loss::CurvedArrow:
            result << i18np("1 curved arrow became straight.", "%1 curved arrows became straight.", n);
            break;
        case Loss::ArrowLabel:
            result << i18np("1 arrow number label was dropped.", "%1 arrow number labels were dropped.", n);
            break;
        case Loss::TextBackground:
            result << i18np("1 text lost its background box.", "%1 texts lost their background boxes.", n);
            break;
        case Loss::PolygonShape:
            result << i18np("1 polygon became an unfilled outline.", "%1 polygons became unfilled outlines.", n);
            break;
        case Loss::UnreadableImage:
            result << i18np("1 image could not be read and was skipped.", "%1 images could not be read and were skipped.", n);
            break;
        case Loss::UnknownShape:
            result << i18np("1 shape of an unknown kind was skipped (%2).", "%1 shapes of unknown kinds were skipped (%2).", n, mUnknownKinds.join(u", "_s));
            break;
        }
    }
    return result;
}
} // namespace

namespace WayScriberImporter
{
QByteArray gunzip(const QByteArray &data, qsizetype maxSize)
{
    z_stream stream{};
    // 16 + MAX_WBITS: expect a gzip header and trailer.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        return {};
    }
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = static_cast<uInt>(data.size());

    QByteArray output;
    constexpr qsizetype chunk{256 * 1024};
    int status{Z_OK};
    while (status == Z_OK) {
        if (output.size() >= maxSize) {
            inflateEnd(&stream);
            return {};
        }
        const qsizetype used{output.size()};
        output.resize(std::min(used + chunk, maxSize));
        stream.next_out = reinterpret_cast<Bytef *>(output.data() + used);
        stream.avail_out = static_cast<uInt>(output.size() - used);
        status = inflate(&stream, Z_NO_FLUSH);
        output.resize(output.size() - stream.avail_out);
    }
    inflateEnd(&stream);
    return status == Z_STREAM_END ? output : QByteArray{};
}

bool isSession(const QByteArray &fileData)
{
    if (fileData.startsWith("\x1f\x8b")) {
        return true;
    }
    const QJsonDocument doc{QJsonDocument::fromJson(fileData)};
    return doc.isObject() && doc.object()[u"boards"_s].isArray();
}

ImportResult importSession(const QByteArray &fileData)
{
    ImportResult result;
    QByteArray json{fileData};
    if (fileData.startsWith("\x1f\x8b")) {
        json = gunzip(fileData, maxSessionSize);
        if (json.isEmpty()) {
            result.error = i18n("The WayScriber session is damaged or too large to decompress.");
            return result;
        }
    }

    QJsonParseError parseError;
    const QJsonDocument doc{QJsonDocument::fromJson(json, &parseError)};
    if (!doc.isObject() || !doc.object()[u"boards"_s].isArray()) {
        result.error = parseError.error != QJsonParseError::NoError ? i18n("The WayScriber session is not valid JSON: %1", parseError.errorString())
                                                                    : i18n("The file is not a WayScriber session.");
        return result;
    }

    Converter converter;
    result.document = converter.convert(doc.object());
    result.warnings = converter.warnings();
    result.ok = true;
    return result;
}
} // namespace WayScriberImporter

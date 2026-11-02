/*
 * SPDX-FileCopyrightText: 2026 Emilio Ginzo
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "wayscriberimportertest.hpp"
#include "common/utils/compression.hpp"
#include "drawyglobalconfig.h"
#include "item/freeform.hpp"
#include "serializer/deserializeutils.hpp"
#include "serializer/serializerutils.hpp"
#include "serializer/wayscriberimporter.hpp"
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
QTEST_MAIN(WayScriberImporterTest)
using namespace Qt::Literals::StringLiterals;

namespace
{
QByteArray readFixture(const QString &name)
{
    QFile file{QLatin1StringView(DRAWY_GUI_DATA_DIR) + u"/wayscriber/"_s + name};
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "missing fixture" << file.fileName();
        return {};
    }
    return file.readAll();
}

QJsonObject importFixture()
{
    return WayScriberImporter::importSession(readFixture(u"allshapes.json"_s)).document;
}

QJsonArray pageItems(const QJsonObject &doc, int page)
{
    return doc[u"pages"_s].toArray().at(page).toObject()[u"items"_s].toArray();
}

QJsonValue propertyValue(const QJsonObject &item, const QString &type)
{
    for (const auto &p : item[u"properties"_s].toArray()) {
        if (p.toObject()[u"type"_s].toString() == type) {
            return p.toObject()[u"value"_s];
        }
    }
    return {};
}

QByteArray sessionWith(const QJsonObject &shape)
{
    const QJsonObject entry{{u"id"_s, 1}, {u"shape"_s, shape}, {u"locked"_s, false}};
    const QJsonObject page{{u"shapes"_s, QJsonArray{entry}}};
    const QJsonObject board{{u"id"_s, u"transparent"_s}, {u"pages"_s, QJsonArray{page}}, {u"active_page"_s, 0}};
    const QJsonObject session{{u"version"_s, 8}, {u"active_board_id"_s, u"transparent"_s}, {u"boards"_s, QJsonArray{board}}};
    return QJsonDocument{session}.toJson();
}
} // namespace

WayScriberImporterTest::WayScriberImporterTest(QObject *parent)
    : QObject{parent}
{
}

void WayScriberImporterTest::shouldRecognizeSessions()
{
    QVERIFY(WayScriberImporter::isSession(readFixture(u"allshapes.json"_s)));
    QVERIFY(WayScriberImporter::isSession(readFixture(u"allshapes.json.gz"_s)));

    // A saved Drawy document is zstd data, never a session.
    const QJsonObject drawyDoc{{u"version"_s, SerializerUtils::pageVersion()}, {u"pages"_s, QJsonArray{}}};
    QVERIFY(!WayScriberImporter::isSession(SerializerUtils::compressData(drawyDoc)));
    QVERIFY(!WayScriberImporter::isSession(QJsonDocument{drawyDoc}.toJson()));
    QVERIFY(!WayScriberImporter::isSession(QByteArray{}));
}

void WayScriberImporterTest::shouldReadGzipAndPlainSessions()
{
    const auto plain = WayScriberImporter::importSession(readFixture(u"allshapes.json"_s));
    const auto gzipped = WayScriberImporter::importSession(readFixture(u"allshapes.json.gz"_s));
    QVERIFY(plain.ok);
    QVERIFY(gzipped.ok);
    QCOMPARE(gzipped.warnings, plain.warnings);
    QCOMPARE(pageItems(gzipped.document, 0).size(), pageItems(plain.document, 0).size());
}

void WayScriberImporterTest::shouldMapBoardsToPages()
{
    const QJsonObject doc{importFixture()};
    QCOMPARE(doc[u"version"_s].toInt(), SerializerUtils::pageVersion());

    // The empty whiteboard board is left out; the blackboard keeps its empty second page.
    const QJsonArray pages = doc[u"pages"_s].toArray();
    QCOMPARE(pages.size(), 3);
    QCOMPARE(pages.at(0).toObject()[u"page_name"_s].toString(), u"transparent"_s);
    QCOMPARE(pages.at(1).toObject()[u"page_name"_s].toString(), u"blackboard 1"_s);
    QCOMPARE(pages.at(2).toObject()[u"page_name"_s].toString(), u"Plan"_s);

    // The session's active board page opens first, at the session's pan position.
    QCOMPARE(doc[u"current_page"_s].toInt(), 2);
    const QJsonObject offset{pages.at(1).toObject()[u"offset_pos"_s].toObject()};
    QCOMPARE(offset[u"x"_s].toDouble(), 100.0);
    QCOMPARE(offset[u"y"_s].toDouble(), -50.0);
}

void WayScriberImporterTest::shouldConvertEveryShapeKind()
{
    const QJsonArray items = pageItems(importFixture(), 0);
    QStringList types;
    for (const auto &item : items) {
        types << item.toObject()[u"type"_s].toString();
    }
    const QStringList expected{
        u"FREEFORM"_s, // Freehand
        u"FREEFORM"_s, // FreehandPressure
        u"FREEFORM"_s, // MarkerStroke
        u"LINE"_s,
        u"ARROW"_s,
        u"ARROW"_s,
        u"ARROW"_s,
        u"RECTANGLE"_s,
        u"ELLIPSE"_s,
        u"DIAMOND"_s, // Rhombus polygon
        u"FREEFORM"_s, // other polygon, as its outline
        u"TEXT"_s,
        u"GROUP"_s, // sticky note
        u"GROUP"_s, // step marker
        u"IMAGE"_s,
    };
    QCOMPARE(types, expected);

    for (int i = 0; i < items.size(); ++i) {
        QCOMPARE(items.at(i).toObject()[u"zorder"_s].toString(), QString::number(i));
    }
    QVERIFY(items.at(3).toObject()[u"locked"_s].toBool());

    const QJsonObject freehand{items.at(0).toObject()};
    QCOMPARE(propertyValue(freehand, u"StrokeColor"_s).toString(), u"#ff0000"_s);
    QCOMPARE(propertyValue(freehand, u"StrokeWidth"_s).toInt(), 3);
    QCOMPARE(freehand[u"points"_s].toArray().at(0).toObject()[u"x"_s].toDouble(), 10.5);
    QVERIFY(!freehand[u"simulatePressure"_s].toBool());

    // Highlighter ink: wider than its nominal size, about a third of its alpha.
    const QJsonObject marker{items.at(2).toObject()};
    QCOMPARE(propertyValue(marker, u"StrokeWidth"_s).toInt(), 15);
    QCOMPARE(propertyValue(marker, u"Opacity"_s).toInt(), 82);

    // WayScriber draws the head at (x2, y2) when head_at_end, else at (x1, y1); double has both.
    const QJsonObject headAtEnd{items.at(4).toObject()};
    QCOMPARE(propertyValue(headAtEnd, u"StartArrowType"_s).toString(), u"None"_s);
    QCOMPARE(propertyValue(headAtEnd, u"EndArrowType"_s).toString(), u"Filled"_s);
    const QJsonObject headAtStart{items.at(5).toObject()};
    QCOMPARE(propertyValue(headAtStart, u"StartArrowType"_s).toString(), u"Filled"_s);
    QCOMPARE(propertyValue(headAtStart, u"EndArrowType"_s).toString(), u"None"_s);
    const QJsonObject doubleHead{items.at(6).toObject()};
    QCOMPARE(propertyValue(doubleHead, u"StartArrowType"_s).toString(), u"Filled"_s);
    QCOMPARE(propertyValue(doubleHead, u"EndArrowType"_s).toString(), u"Filled"_s);

    // Fill uses fill_color; the border's alpha becomes the item opacity.
    const QJsonObject rectangle{items.at(7).toObject()};
    QCOMPARE(propertyValue(rectangle, u"BackgroundStyle"_s).toString(), u"Solid"_s);
    QCOMPARE(propertyValue(rectangle, u"BackgroundColor"_s).toString(), u"#ffe633"_s);
    QCOMPARE(propertyValue(rectangle, u"StrokeColor"_s).toString(), u"#0000ff"_s);
    QCOMPARE(propertyValue(rectangle, u"Opacity"_s).toInt(), 128);
    QCOMPARE(rectangle[u"start"_s].toObject()[u"x"_s].toDouble(), 10.0);
    QCOMPARE(rectangle[u"end"_s].toObject()[u"y"_s].toDouble(), 60.0);

    const QJsonObject ellipse{items.at(8).toObject()};
    QCOMPARE(propertyValue(ellipse, u"BackgroundStyle"_s).toString(), u"None"_s);
    QCOMPARE(ellipse[u"start"_s].toObject()[u"x"_s].toDouble(), 80.0);
    QCOMPARE(ellipse[u"end"_s].toObject()[u"y"_s].toDouble(), 110.0);

    const QJsonObject diamond{items.at(9).toObject()};
    QCOMPARE(diamond[u"start"_s].toObject()[u"x"_s].toDouble(), 0.0);
    QCOMPARE(diamond[u"end"_s].toObject()[u"x"_s].toDouble(), 100.0);

    // The polygon outline is closed.
    const QJsonArray outline = items.at(10).toObject()[u"points"_s].toArray();
    QCOMPARE(outline.size(), 6);
    QCOMPARE(outline.first(), outline.last());

    // Text sits above its WayScriber baseline; the wrap width gains the text box margins.
    const QJsonObject text{items.at(11).toObject()};
    QCOMPARE(text[u"text"_s].toString(), u"Hello"_s);
    QCOMPARE(propertyValue(text, u"FontStyle"_s).toString(), u"Bold,Italic"_s);
    QCOMPARE(propertyValue(text, u"FontSize"_s).toInt(), 18);
    QVERIFY(text[u"top_left"_s].toObject()[u"y"_s].toDouble() < 200.0 - 10.0);
    QCOMPARE(text[u"wrap_width"_s].toDouble(), 128.0);

    const QJsonArray note = items.at(12).toObject()[u"items"_s].toArray();
    QCOMPARE(note.size(), 2);
    QCOMPARE(note.at(0).toObject()[u"type"_s].toString(), u"RECTANGLE"_s);
    QCOMPARE(propertyValue(note.at(0).toObject(), u"BackgroundColor"_s).toString(), u"#ffe633"_s);
    QCOMPARE(note.at(1).toObject()[u"text"_s].toString(), u"Note"_s);

    const QJsonArray step = items.at(13).toObject()[u"items"_s].toArray();
    QCOMPARE(step.at(0).toObject()[u"type"_s].toString(), u"ELLIPSE"_s);
    QCOMPARE(step.at(1).toObject()[u"text"_s].toString(), u"3"_s);
    // The circle is centred on the marker position.
    const QJsonObject circle{step.at(0).toObject()};
    QCOMPARE((circle[u"start"_s].toObject()[u"x"_s].toDouble() + circle[u"end"_s].toObject()[u"x"_s].toDouble()) / 2, 400.0);

    const QJsonObject box{items.at(14).toObject()[u"bounding_box"_s].toObject()};
    QCOMPARE(box[u"width"_s].toDouble(), 34.0);
    QCOMPARE(box[u"height"_s].toDouble(), 14.0);
}

void WayScriberImporterTest::shouldReportLosses()
{
    const auto result = WayScriberImporter::importSession(readFixture(u"allshapes.json"_s));
    // Eraser, blur, spotlight, curved arrow, arrow label, text background, polygon outline, blackboard background, unknown kind.
    QCOMPARE(result.warnings.size(), 9);
    QVERIFY(result.warnings.last().contains(u"Hologram"_s));
}

void WayScriberImporterTest::shouldReadLegacySessions()
{
    // Before boards existed, sessions kept one frame or a page list per mode, and bare shapes.
    const QByteArray legacy{readFixture(u"legacy.json"_s)};
    QVERIFY(WayScriberImporter::isSession(legacy));
    const auto result = WayScriberImporter::importSession(legacy);
    QVERIFY(result.ok);
    const QJsonArray pages = result.document[u"pages"_s].toArray();
    QCOMPARE(pages.size(), 3);
    QCOMPARE(pageItems(result.document, 0).size(), 1);
    QCOMPARE(pageItems(result.document, 1).size(), 1);
    QCOMPARE(pageItems(result.document, 1).at(0).toObject()[u"type"_s].toString(), u"LINE"_s);
    QCOMPARE(result.document[u"current_page"_s].toInt(), 2);
    // The entry that is no shape at all is reported, not listed as an unknown kind.
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY(!result.warnings.first().contains(u"()"_s));
}

void WayScriberImporterTest::shouldNotTakeOtherJsonForSessions()
{
    // WayScriber keeps a sessions.json catalog next to the sessions.
    QVERIFY(!WayScriberImporter::isSession(R"({"version":1,"sessions":[]})"));
    QVERIFY(!WayScriberImporter::importSession(R"({"version":1,"sessions":[]})").ok);
}

void WayScriberImporterTest::shouldDeserializeIntoDrawyItems()
{
    const QJsonObject doc{importFixture()};
    for (int page = 0; page < 3; ++page) {
        const QJsonArray items = pageItems(doc, page);
        const auto drawyItems = DeserializeUtils::deserializeItems(items);
        QCOMPARE(drawyItems.size(), items.size());
        for (const auto &item : drawyItems) {
            QVERIFY(item);
            QVERIFY(!item->boundingBox().isNull());
        }
    }
}

void WayScriberImporterTest::shouldKeepPressureStrokeWidths()
{
    // Samples 2, 4 and 8 px wide: the widest one is the stroke width, at the full-width pressure.
    const QJsonObject stroke{pageItems(importFixture(), 0).at(1).toObject()};
    QCOMPARE(propertyValue(stroke, u"StrokeWidth"_s).toInt(), 8);
    const QJsonArray pressures = stroke[u"pressures"_s].toArray();
    QCOMPARE(pressures.size(), 3);
    QCOMPARE(pressures.at(2).toDouble(), 0.5);

    // Drawy's radius formula gives back each sample's width.
    const qreal thinning{DrawyGlobalConfig::self()->thinning()};
    const QList<qreal> widths{2.0, 4.0, 8.0};
    for (int i = 0; i < 3; ++i) {
        const qreal radius{8.0 * (0.5 - thinning * (0.5 - pressures.at(i).toDouble()))};
        if (pressures.at(i).toDouble() > 0.0) {
            QCOMPARE(2 * radius, widths.at(i));
        }
    }
}

void WayScriberImporterTest::shouldReencodeNonPngImages()
{
    QImage image{4, 3, QImage::Format_RGB32};
    image.fill(Qt::green);
    QByteArray jpeg;
    QBuffer buffer{&jpeg};
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "JPEG"));

    const QJsonObject data{{u"mime_type"_s, u"image/jpeg"_s}, {u"width"_s, 4}, {u"height"_s, 3}, {u"bytes"_s, QString::fromLatin1(jpeg.toBase64())}};
    const QJsonObject shape{{u"Image"_s, QJsonObject{{u"x"_s, 0}, {u"y"_s, 0}, {u"w"_s, 4}, {u"h"_s, 3}, {u"data"_s, data}}}};
    const auto result = WayScriberImporter::importSession(sessionWith(shape));
    QVERIFY(result.ok);
    const QByteArray stored{QByteArray::fromBase64(pageItems(result.document, 0).at(0).toObject()[u"image_data"_s].toString().toLatin1())};
    QVERIFY(stored.startsWith("\x89PNG"));
    QCOMPARE(QImage::fromData(stored, "PNG").size(), QSize(4, 3));

    const QJsonObject broken{{u"mime_type"_s, u"image/jpeg"_s}, {u"bytes"_s, u"bm90IGFuIGltYWdl"_s}};
    const QJsonObject brokenShape{{u"Image"_s, QJsonObject{{u"x"_s, 0}, {u"y"_s, 0}, {u"w"_s, 4}, {u"h"_s, 3}, {u"data"_s, broken}}}};
    const auto skipped = WayScriberImporter::importSession(sessionWith(brokenShape));
    QVERIFY(skipped.ok);
    QVERIFY(pageItems(skipped.document, 0).isEmpty());
    QCOMPARE(skipped.warnings.size(), 1);
}

void WayScriberImporterTest::shouldRejectBrokenSessions()
{
    QVERIFY(!WayScriberImporter::importSession("{\"boards\": [").ok);
    QVERIFY(!WayScriberImporter::importSession("{\"pages\": []}").ok);

    const QByteArray gzipped{readFixture(u"allshapes.json.gz"_s)};
    QVERIFY(!WayScriberImporter::importSession(gzipped.left(gzipped.size() / 2)).ok);
    QVERIFY(WayScriberImporter::gunzip(gzipped, 100).isEmpty());
    QVERIFY(!WayScriberImporter::gunzip(gzipped, 1024 * 1024).isEmpty());
}

#include "moc_wayscriberimportertest.cpp"

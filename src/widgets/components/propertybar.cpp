// SPDX-FileCopyrightText: 2025 Prayag Jain <prayagjain2@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "propertybar.hpp"

#include <KLocalizedString>
#include <QIcon>
#include <QLabel>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>

#include "context/applicationcontext.hpp"
#include "context/selectioncontext.hpp"
#include "context/uicontext.hpp"
#include "item/item.hpp"
#include "properties/widgets/propertymanager.hpp"
#include "properties/widgets/propertywidget.hpp"
#include "toolbar.hpp"
#include "tools/tool.hpp"
using namespace Qt::Literals::StringLiterals;

PropertyBar::PropertyBar(ApplicationContext *context, QWidget *parent)
    : Frame{parent}
    , m_contentWidget{new QWidget(this)}
    , m_layout{new QVBoxLayout(m_contentWidget)}
    , m_context{context}
{
    const int margins{style()->pixelMetric(QStyle::PM_ToolBarItemMargin) * 2};
    m_layout->setContentsMargins(margins, margins, margins, margins);

    m_layout->setAlignment(Qt::AlignTop);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidget(m_contentWidget);

    m_scrollArea->setWidgetResizable(true);

    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_scrollArea->setFrameShape(QFrame::NoFrame);

    const int iconWidth{style()->pixelMetric(QStyle::PM_ToolBarIconSize)};
    m_collapseButton = new QToolButton(this);
    m_collapseButton->setAutoRaise(true);
    m_collapseButton->setIconSize(QSize{iconWidth, iconWidth});
    connect(m_collapseButton, &QToolButton::clicked, this, &PropertyBar::toggleCollapsed);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_collapseButton, 0, Qt::AlignLeft);
    mainLayout->addWidget(m_scrollArea);

    updateCollapseButton();
}

QSize PropertyBar::sizeHint() const
{
    if (m_collapsed) {
        return m_collapseButton->sizeHint();
    }

    QSize size = m_contentWidget->sizeHint();
    size.setWidth(size.width() + style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    size.setHeight(size.height() + m_collapseButton->sizeHint().height());
    return size;
}

bool PropertyBar::isCollapsed() const
{
    return m_collapsed;
}

void PropertyBar::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed) {
        return;
    }

    m_collapsed = collapsed;
    m_scrollArea->setVisible(!m_collapsed);
    updateCollapseButton();
    updateGeometry();
}

void PropertyBar::toggleCollapsed()
{
    setCollapsed(!m_collapsed);
}

void PropertyBar::setInterfaceHidden(bool hidden)
{
    m_interfaceHidden = hidden;
    if (hidden) {
        hide();
    } else {
        updateToolProperties();
    }
}

void PropertyBar::updateCollapseButton()
{
    if (m_collapsed) {
        m_collapseButton->setIcon(QIcon::fromTheme(u"sidebar-expand-left"_s, QIcon::fromTheme(u"go-next"_s)));
        m_collapseButton->setToolTip(i18nc("@info:tooltip", "Show tool options"));
    } else {
        m_collapseButton->setIcon(QIcon::fromTheme(u"sidebar-collapse-left"_s, QIcon::fromTheme(u"go-previous"_s)));
        m_collapseButton->setToolTip(i18nc("@info:tooltip", "Hide tool options"));
    }
}

void PropertyBar::setPropertyManager(PropertyManager *manager)
{
    m_propertyManager = manager;
}

void PropertyBar::updateToolProperties()
{
    updateProperties(m_context->uiContext()->toolBar()->curTool());
}

void PropertyBar::updateProperties(Tool &tool)
{
    if (m_updateInProgress) {
        return;
    }

    m_updateInProgress = true;

    hide();

    // remove existing widgets
    const QLayoutItem *curItem = nullptr;
    while ((curItem = m_layout->takeAt(0)) != nullptr) {
        QWidget *widget = curItem->widget();
        if (widget) {
            widget->hide();
            widget->setParent(nullptr);

            // this is the property label
            if (qobject_cast<QLabel *>(widget) != nullptr) {
                widget->deleteLater();
            }
        }
        delete curItem;
    }

    QList<Property::Type> properties{tool.properties()};

    std::sort(properties.begin(), properties.end(), [](const auto first, const auto second) -> bool {
        return Property::propertyPriority(first) < Property::propertyPriority(second);
    });

    for (const Property::Type property : properties) {
        try {
            PropertyWidget *const widget{m_propertyManager->widget(property)};

            auto widgetLabel{new QLabel{widget->name(), m_contentWidget}};

            m_layout->addWidget(widgetLabel);
            m_layout->addWidget(widget->widget());

            widget->widget()->show();
            assignPropertyValue(property, widget);
        } catch (const std::logic_error &) { // NOLINT(bugprone-empty-catch)
            // ignore since the item does not support this property
        }
    }

    if (properties.empty() || m_interfaceHidden) {
        hide();
    } else {
        show();
    }

    update();

    m_updateInProgress = false;
}

void PropertyBar::assignPropertyValue(Property::Type property, PropertyWidget *widget)
{
    const auto &selectedItems{m_context->selectionContext()->selectedItems()};
    QVariant propertyValue;
    for (const auto &item : selectedItems) {
        if (item->hasProperty(property)) {
            if (propertyValue.isValid() && (propertyValue != item->property(property).variant())) {
                return;
            }
            propertyValue = item->property(property).variant();
        }
    }

    if (propertyValue.isValid()) {
        widget->setValue(propertyValue);
    }
}

#include "moc_propertybar.cpp"

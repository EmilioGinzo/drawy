// SPDX-FileCopyrightText: 2025 Prayag Jain <prayagjain2@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "frame.hpp"
#include "properties/property.hpp"
#include <QVBoxLayout>

class QScrollArea;
class QToolButton;
class Tool;
class PropertyManager;
class PropertyWidget;
class ApplicationContext;

class PropertyBar : public Frame
{
    Q_OBJECT
public:
    explicit PropertyBar(ApplicationContext *context, QWidget *parent = nullptr);

    void setPropertyManager(PropertyManager *manager);
    void updateToolProperties();
    void updateProperties(Tool &);

    [[nodiscard]] QSize sizeHint() const override;

    [[nodiscard]] bool isCollapsed() const;
    void setCollapsed(bool collapsed);
    void toggleCollapsed();

    // Hides the whole bar (including the collapse button) regardless of the selected tool
    void setInterfaceHidden(bool hidden);

private:
    void assignPropertyValue(Property::Type property, PropertyWidget *widget);
    void updateCollapseButton();

    QWidget *m_contentWidget = nullptr; // Added container widget
    QScrollArea *m_scrollArea = nullptr;
    QToolButton *m_collapseButton = nullptr;
    bool m_collapsed{false};
    bool m_interfaceHidden{false};
    QVBoxLayout *const m_layout;
    PropertyManager *m_propertyManager = nullptr;
    ApplicationContext *const m_context;

    bool m_updateInProgress{false};
};

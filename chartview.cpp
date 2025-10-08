/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Charts module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 or (at your option) any later version
** approved by the KDE Free Qt Foundation. The licenses are as published by
** the Free Software Foundation and appearing in the file LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "chartview.h"
#include <QtGui/QMouseEvent>
#include <QCoreApplication>
#include <iostream>

ChartView::ChartView(QChart *chart, QWidget *parent) :
    QChartView(chart, parent),
    m_isTouching(false)
{
    m_isRunning = true;
    m_mouseIndex = 0;
    setRubberBand(QChartView::RectangleRubberBand);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    viewport()->setMouseTracking(true);

    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents, true);
}

bool ChartView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::TouchBegin) {

        std::cout << "viewportEvent: " << event->type() << std::endl;
        // By default touch events are converted to mouse events. So
        // after this event we will get a mouse event also but we want
        // to handle touch events as gestures only. So we need this safeguard
        // to block mouse events that are actually generated from touch.
        m_isTouching = true;

        // Turn off animations when handling gestures they
        // will only slow us down.
        chart()->setAnimationOptions(QChart::NoAnimation);
        return true;
    }
    if (event->type() == QEvent::MouseMove) {
        auto e = static_cast<QMouseEvent*>(event);
        if(!chart()->plotArea().contains(e->pos()) || chart()->series().length() != 2) {
            QToolTip::hideText();
            return true;
        }

        displayTooltip(e);
    }
    return QChartView::viewportEvent(event);
}

void ChartView::mousePressEvent(QMouseEvent *event)
{
    m_isRunning = false;
    if (m_isTouching)
        return;

    QChartView::mousePressEvent(event);
}

void ChartView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isTouching)
        return;

    auto e = static_cast<QMouseEvent*>(event);
    if(!chart()->plotArea().contains(e->pos()) || chart()->series().length() != 2) {
        QToolTip::hideText();
        return;
    }

    displayTooltip(e);
    QChartView::mouseMoveEvent(event);
}

void ChartView::mouseReleaseEvent(QMouseEvent *event)
{
    std::cout << "mouseReleaseEvent: " << event->type() << std::endl;
    if (m_isTouching)
        m_isTouching = false;

    // Because we disabled animations when touch event was detected
    // we must put them back on.
    chart()->setAnimationOptions(QChart::SeriesAnimations);

    QChartView::mouseReleaseEvent(event);
}

//![1]
void ChartView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Plus:
        chart()->zoomIn();
        break;
    case Qt::Key_Minus:
        chart()->zoomOut();
        break;
//![1]
    case Qt::Key_Left:
        chart()->scroll(-10, 0);
        break;
    case Qt::Key_Right:
        chart()->scroll(10, 0);
        break;
    case Qt::Key_Up:
        chart()->scroll(0, 10);
        break;
    case Qt::Key_Down:
        chart()->scroll(0, -10);
        break;
    default:
        QGraphicsView::keyPressEvent(event);
        break;
    }
}

void ChartView::displayTooltip(QMouseEvent* e)
{
    auto series = chart()->series();
    auto xSeries = qobject_cast<QLineSeries*>(series.at(0));
    auto ySeries = qobject_cast<QLineSeries*>(series.at(1));
    int index = chart()->mapToValue(e->pos(), xSeries).x();
    QString text = QString::asprintf("Time: %d ms\nX: %.3f um | Y: %.3f um", index, xSeries->at(index).y(), ySeries->at(index).y());
    m_globalPos = e->globalPos();
    m_mouseIndex = index;
    m_tooltipData[0] = index;
    m_tooltipData[1] = xSeries->at(index).y();
    m_tooltipData[2] = ySeries->at(index).y();
    QToolTip::showText(e->globalPos(), text, this, this->rect(), 20000);
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "linechart.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

const qreal LineWidth = 2;
// Room at the top and bottom so 2 px lines at 0 and the maximum aren't cut.
const qreal Inset = 2;

} // namespace

LineChart::LineChart(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_referenceColor(Qt::gray)
    , m_gridColor(QColor(128, 128, 128, 60))
    , m_surfaceColor(Qt::transparent)
{
    setAntialiasing(true);
    connect(this, &LineChart::appearanceChanged, this, [this]() {
        updateRange();
        update();
    });
}

RrdHistory *LineChart::history() const
{
    return m_history;
}

void LineChart::setHistory(RrdHistory *history)
{
    if (history == m_history)
        return;
    if (m_history)
        m_history->disconnect(this);
    m_history = history;
    if (history) {
        connect(history, &RrdHistory::historyChanged, this, [this]() {
            if (m_cursorIndex >= (m_history ? m_history->count() : 0))
                setCursorIndex(-1);
            updateRange();
            update();
        });
    }
    updateRange();
    update();
    emit historyChanged();
}

QStringList LineChart::keys() const
{
    return m_keys;
}

void LineChart::setKeys(const QStringList &keys)
{
    if (keys == m_keys)
        return;
    m_keys = keys;
    emit appearanceChanged();
}

QVariantList LineChart::colors() const
{
    return m_colors;
}

void LineChart::setColors(const QVariantList &colors)
{
    if (colors == m_colors)
        return;
    m_colors = colors;
    emit appearanceChanged();
}

QString LineChart::referenceKey() const
{
    return m_referenceKey;
}

void LineChart::setReferenceKey(const QString &key)
{
    if (key == m_referenceKey)
        return;
    m_referenceKey = key;
    emit appearanceChanged();
}

QColor LineChart::referenceColor() const
{
    return m_referenceColor;
}

void LineChart::setReferenceColor(const QColor &color)
{
    if (color == m_referenceColor)
        return;
    m_referenceColor = color;
    emit appearanceChanged();
}

QColor LineChart::gridColor() const
{
    return m_gridColor;
}

void LineChart::setGridColor(const QColor &color)
{
    if (color == m_gridColor)
        return;
    m_gridColor = color;
    emit appearanceChanged();
}

QColor LineChart::surfaceColor() const
{
    return m_surfaceColor;
}

void LineChart::setSurfaceColor(const QColor &color)
{
    if (color == m_surfaceColor)
        return;
    m_surfaceColor = color;
    emit appearanceChanged();
}

qreal LineChart::maximum() const
{
    return m_maximum;
}

void LineChart::setMaximum(qreal maximum)
{
    if (qFuzzyCompare(maximum, m_maximum))
        return;
    m_maximum = maximum;
    emit appearanceChanged();
}

bool LineChart::bytes() const
{
    return m_bytes;
}

void LineChart::setBytes(bool bytes)
{
    if (bytes == m_bytes)
        return;
    m_bytes = bytes;
    emit appearanceChanged();
}

qreal LineChart::yMaximum() const
{
    return m_yMaximum;
}

int LineChart::cursorIndex() const
{
    return m_cursorIndex;
}

void LineChart::setCursorIndex(int index)
{
    if (index == m_cursorIndex)
        return;
    m_cursorIndex = index;
    update();
    emit cursorIndexChanged();
}

int LineChart::indexAt(qreal x) const
{
    const int n = m_history ? m_history->count() : 0;
    if (n == 0 || width() <= 0)
        return -1;
    if (n == 1)
        return 0;
    const int index = qRound(x / width() * (n - 1));
    return qBound(0, index, n - 1);
}

qreal LineChart::niceCeiling(qreal value)
{
    if (value <= 0 || std::isnan(value))
        return 1;
    const qreal magnitude = std::pow(10.0, std::floor(std::log10(value)));
    const qreal steps[] = { 1, 2, 5, 10 };
    for (qreal step : steps) {
        if (step * magnitude >= value * (1 - 1e-9))
            return step * magnitude;
    }
    return 10 * magnitude;
}

qreal LineChart::niceByteCeiling(qreal value)
{
    if (value <= 0 || std::isnan(value))
        return 1;
    qreal unit = 1;
    while (value / unit >= 1024)
        unit *= 1024;
    const qreal steps[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500 };
    for (qreal step : steps) {
        if (step * unit >= value * (1 - 1e-9))
            return step * unit;
    }
    return 1024 * unit;
}

void LineChart::paint(QPainter *painter)
{
    const QRectF area = boundingRect();
    if (m_surfaceColor.alpha() > 0)
        painter->fillRect(area, m_surfaceColor);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Gridlines at 0, half and the top: recessive, 1 px.
    QPen grid(m_gridColor, 1);
    painter->setPen(grid);
    for (qreal f : { 0.0, 0.5, 1.0 }) {
        const qreal y = yFor(m_yMaximum * f);
        painter->drawLine(QPointF(0, y), QPointF(area.width(), y));
    }

    if (!m_history || m_history->count() == 0)
        return;
    const int n = m_history->count();

    // One path per series; a gap in the data starts a new subpath.
    auto pathFor = [this, n](const QVector<double> &values) {
        QPainterPath path;
        bool drawing = false;
        for (int i = 0; i < n; ++i) {
            if (std::isnan(values.at(i))) {
                drawing = false;
                continue;
            }
            const QPointF p(xFor(i), yFor(values.at(i)));
            if (drawing)
                path.lineTo(p);
            else
                path.moveTo(p);
            drawing = true;
        }
        return path;
    };

    if (!m_referenceKey.isEmpty()) {
        QPen pen(m_referenceColor, 1, Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(pathFor(m_history->values(m_referenceKey)));
    }

    for (int k = 0; k < m_keys.size(); ++k) {
        const QColor color = m_colors.value(k).value<QColor>();
        const QVector<double> values = m_history->values(m_keys.at(k));

        if (m_keys.size() == 1) {
            // A light fill under a lone series, closed down to the baseline
            // for each run of samples.
            QColor fill = color;
            fill.setAlphaF(0.18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            int i = 0;
            while (i < n) {
                while (i < n && std::isnan(values.at(i)))
                    ++i;
                if (i >= n)
                    break;
                QPainterPath run;
                const int first = i;
                run.moveTo(xFor(first), yFor(0));
                while (i < n && !std::isnan(values.at(i))) {
                    run.lineTo(xFor(i), yFor(values.at(i)));
                    ++i;
                }
                run.lineTo(xFor(i - 1), yFor(0));
                run.closeSubpath();
                painter->drawPath(run);
            }
        }

        QPen pen(color, LineWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(pathFor(values));
    }

    // Crosshair and a dot on each series, ringed in the surface colour.
    if (m_cursorIndex >= 0 && m_cursorIndex < n) {
        const qreal x = xFor(m_cursorIndex);
        QPen cross(m_referenceColor, 1);
        painter->setPen(cross);
        painter->drawLine(QPointF(x, 0), QPointF(x, area.height()));
        const qreal radius = 4;
        for (int k = 0; k < m_keys.size(); ++k) {
            const double value = m_history->valueAt(m_keys.at(k), m_cursorIndex);
            if (std::isnan(value))
                continue;
            QPen ring(m_surfaceColor.alpha() > 0 ? m_surfaceColor : QColor(Qt::black), 2);
            painter->setPen(ring);
            painter->setBrush(m_colors.value(k).value<QColor>());
            painter->drawEllipse(QPointF(x, yFor(value)), radius, radius);
        }
    }
}

void LineChart::updateRange()
{
    qreal top = m_maximum;
    if (top <= 0) {
        double highest = 0;
        double reference = 0;
        if (m_history) {
            foreach (const QString &key, m_keys) {
                foreach (double v, m_history->values(key)) {
                    if (!std::isnan(v) && v > highest)
                        highest = v;
                }
            }
            if (!m_referenceKey.isEmpty()) {
                foreach (double v, m_history->values(m_referenceKey)) {
                    if (!std::isnan(v) && v > reference)
                        reference = v;
                }
            }
        }
        // A reference such as the memory size is the natural top.
        if (reference > 0 && highest <= reference)
            top = reference;
        else
            top = m_bytes ? niceByteCeiling(qMax(highest, reference)) : niceCeiling(qMax(highest, reference));
    }
    if (qFuzzyCompare(top, m_yMaximum))
        return;
    m_yMaximum = top;
    emit rangeChanged();
}

qreal LineChart::xFor(int index) const
{
    const int n = m_history ? m_history->count() : 0;
    if (n <= 1)
        return width() / 2;
    return width() * index / (n - 1);
}

qreal LineChart::yFor(double value) const
{
    const qreal h = height() - 2 * Inset;
    const qreal f = m_yMaximum > 0 ? qBound<qreal>(0, value / m_yMaximum, 1) : 0;
    return Inset + h * (1 - f);
}

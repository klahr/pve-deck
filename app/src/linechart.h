// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_LINECHART_H
#define PVEDECK_LINECHART_H

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QStringList>
#include <QVariantList>

#include "rrdhistory.h"

// A line chart of one or more RrdHistory series sharing one y-axis from 0.
// Thin lines, a light fill under a lone series, faint gridlines at 0, half
// and the top, gaps where samples are missing, and an optional dashed
// reference series (such as the memory total). Touching sets cursorIndex;
// the chart draws a crosshair and dots there, and QML shows the values.
class LineChart : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(RrdHistory *history READ history WRITE setHistory NOTIFY historyChanged)
    Q_PROPERTY(QStringList keys READ keys WRITE setKeys NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantList colors READ colors WRITE setColors NOTIFY appearanceChanged)
    // Drawn dashed in referenceColor, and included in the y range.
    Q_PROPERTY(QString referenceKey READ referenceKey WRITE setReferenceKey NOTIFY appearanceChanged)
    Q_PROPERTY(QColor referenceColor READ referenceColor WRITE setReferenceColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor surfaceColor READ surfaceColor WRITE setSurfaceColor NOTIFY appearanceChanged)
    // Fixed top of the y-axis; 0 picks a round number above the data.
    Q_PROPERTY(qreal maximum READ maximum WRITE setMaximum NOTIFY appearanceChanged)
    // The top of the y-axis actually used.
    Q_PROPERTY(qreal yMaximum READ yMaximum NOTIFY rangeChanged)
    Q_PROPERTY(int cursorIndex READ cursorIndex WRITE setCursorIndex NOTIFY cursorIndexChanged)

public:
    explicit LineChart(QQuickItem *parent = nullptr);

    RrdHistory *history() const;
    void setHistory(RrdHistory *history);
    QStringList keys() const;
    void setKeys(const QStringList &keys);
    QVariantList colors() const;
    void setColors(const QVariantList &colors);
    QString referenceKey() const;
    void setReferenceKey(const QString &key);
    QColor referenceColor() const;
    void setReferenceColor(const QColor &color);
    QColor gridColor() const;
    void setGridColor(const QColor &color);
    QColor surfaceColor() const;
    void setSurfaceColor(const QColor &color);
    qreal maximum() const;
    void setMaximum(qreal maximum);
    qreal yMaximum() const;
    int cursorIndex() const;
    void setCursorIndex(int index);

    // The sample nearest to x, or -1 when there is no data.
    Q_INVOKABLE int indexAt(qreal x) const;

    void paint(QPainter *painter) override;

    // 1, 2 or 5 times a power of ten, at or above value.
    static qreal niceCeiling(qreal value);

signals:
    void historyChanged();
    void appearanceChanged();
    void rangeChanged();
    void cursorIndexChanged();

private:
    void updateRange();
    qreal xFor(int index) const;
    qreal yFor(double value) const;

    QPointer<RrdHistory> m_history;
    QStringList m_keys;
    QVariantList m_colors;
    QString m_referenceKey;
    QColor m_referenceColor;
    QColor m_gridColor;
    QColor m_surfaceColor;
    qreal m_maximum = 0;
    qreal m_yMaximum = 1;
    int m_cursorIndex = -1;
};

#endif // PVEDECK_LINECHART_H

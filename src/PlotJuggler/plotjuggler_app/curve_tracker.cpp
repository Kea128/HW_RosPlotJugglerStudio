/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "curve_tracker.h"

#include "qwt_plot.h"
#include "qwt_plot_curve.h"
#include "qwt_plot_marker.h"
#include "qwt_scale_map.h"
#include "qwt_series_data.h"
#include "qwt_symbol.h"
#include "qwt_text.h"
#include "tracker_label_layout.h"

#include <QFontDatabase>
#include <QFontMetricsF>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
struct CompareX
{
  bool operator()(double x, const QPointF& point) const
  {
    return x < point.x();
  }
};
}

CurveTracker::CurveTracker(QwtPlot* plot, QColor color, QString label, bool prefer_right,
                           std::vector<QRectF>* occupied_labels)
  : QObject(plot)
  , _line_marker(new QwtPlotMarker())
  , _plot(plot)
  , _color(std::move(color))
  , _label(std::move(label))
  , _prefer_right(prefer_right)
  , _occupied_labels(occupied_labels ? occupied_labels : &_local_occupied_labels)
  , _param(VALUE)
  , _precision(3)
  , _visible(true)
{
  _line_marker->setLinePen(QPen(_color));
  _line_marker->setLineStyle(QwtPlotMarker::VLine);
  _line_marker->setValue(0, 0);
  if (!_label.isEmpty())
  {
    QwtText text(_label);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setBold(true);
    font.setPointSize(10);
    text.setColor(_color);
    text.setFont(font);
    _line_marker->setLabel(text);
    _line_marker->setLabelAlignment(Qt::AlignTop |
                                    (_prefer_right ? Qt::AlignRight : Qt::AlignLeft));
  }
  _line_marker->attach(plot);
}

CurveTracker::~CurveTracker()
{
  for (auto* marker : _point_markers)
  {
    marker->detach();
    delete marker;
  }
  for (auto* marker : _value_markers)
  {
    marker->detach();
    delete marker;
  }
  _line_marker->detach();
  delete _line_marker;
}

QPointF CurveTracker::actualPosition() const
{
  return _prev_trackerpoint;
}

void CurveTracker::setParameter(Parameter parameter)
{
  _param = parameter;
  redraw();
}

void CurveTracker::setPrecision(int precision)
{
  _precision = std::clamp(precision, 0, 16);
}

void CurveTracker::setEnabled(bool enable)
{
  _visible = enable;
  _line_marker->setVisible(enable);
  for (auto* marker : _point_markers)
  {
    marker->setVisible(false);
  }
  for (auto* marker : _value_markers)
  {
    marker->setVisible(false);
  }
  if (enable)
  {
    redraw();
  }
}

bool CurveTracker::isEnabled() const
{
  return _visible;
}

void CurveTracker::setPosition(const QPointF& tracker_position)
{
  const QwtPlotItemList curves = _plot->itemList(QwtPlotItem::Rtti_PlotCurve);
  _line_marker->setValue(tracker_position);

  while (_point_markers.size() > static_cast<size_t>(curves.size()))
  {
    _point_markers.back()->detach();
    delete _point_markers.back();
    _point_markers.pop_back();
    _value_markers.back()->detach();
    delete _value_markers.back();
    _value_markers.pop_back();
  }
  while (_point_markers.size() < static_cast<size_t>(curves.size()))
  {
    auto* point_marker = new QwtPlotMarker();
    point_marker->setZ(40.0);
    point_marker->attach(_plot);
    _point_markers.push_back(point_marker);

    auto* value_marker = new QwtPlotMarker();
    value_marker->setZ(50.0);
    value_marker->attach(_plot);
    _value_markers.push_back(value_marker);
  }

  QFont label_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  label_font.setPointSize(9);
  const QFontMetricsF metrics(label_font);
  const QRectF canvas = _plot->canvas()->contentsRect();
  const QwtScaleMap x_map = _plot->canvasMap(QwtPlot::xBottom);
  const QwtScaleMap y_map = _plot->canvasMap(QwtPlot::yLeft);
  const QRectF visible_rect(std::min(x_map.s1(), x_map.s2()), std::min(y_map.s1(), y_map.s2()),
                            std::abs(x_map.s2() - x_map.s1()),
                            std::abs(y_map.s2() - y_map.s1()));

  for (int index = 0; index < curves.size(); ++index)
  {
    auto* curve = static_cast<QwtPlotCurve*>(curves[index]);
    auto* point_marker = _point_markers[static_cast<size_t>(index)];
    auto* value_marker = _value_markers[static_cast<size_t>(index)];
    point_marker->setVisible(false);
    value_marker->setVisible(false);

    if (!_visible || !curve->isVisible())
    {
      continue;
    }
    const auto point = curvePointAt(curve, tracker_position.x());
    if (!point || !visible_rect.contains(*point))
    {
      continue;
    }

    if (!point_marker->symbol() || point_marker->symbol()->brush().color() != _color)
    {
      point_marker->setSymbol(
          new QwtSymbol(QwtSymbol::Ellipse, _color, QPen(Qt::black), QSize(6, 6)));
    }
    point_marker->setValue(*point);
    point_marker->setVisible(true);

    if (_param == LINE_ONLY)
    {
      continue;
    }

    const QString value_text =
        QString("%1 %2")
            .arg(_label, QString::number(point->y(), 'g', std::max(_precision + 3, 6)));
    QwtText text(value_text);
    text.setColor(_color);
    text.setFont(label_font);
    QColor background = _plot->palette().window().color();
    background.setAlpha(220);
    text.setBackgroundBrush(background);
    QColor border = _color;
    border.setAlpha(150);
    text.setBorderPen(QPen(border));
    text.setBorderRadius(3.0);

    const QSizeF requested(metrics.horizontalAdvance(value_text) + 10.0, metrics.height() + 6.0);
    const QPointF anchor(x_map.transform(point->x()), y_map.transform(point->y()));
    const QRectF placement =
        PlaceTrackerLabel(anchor, requested, canvas, *_occupied_labels, _prefer_right);
    if (!placement.isValid())
    {
      continue;
    }
    _occupied_labels->push_back(placement);
    value_marker->setLabel(text);
    value_marker->setLabelAlignment(Qt::AlignCenter);
    value_marker->setValue(x_map.invTransform(placement.center().x()),
                           y_map.invTransform(placement.center().y()));
    value_marker->setVisible(true);
  }
  _prev_trackerpoint = tracker_position;
}

void CurveTracker::setReferencePosition(std::optional<QPointF> reference_pos)
{
  _reference_pos = reference_pos;
}

std::optional<QPointF> curvePointAt(const QwtPlotCurve* curve, double x)
{
  if (!curve || curve->dataSize() == 0)
  {
    return std::nullopt;
  }
  const QPointF first = curve->sample(0);
  const QPointF last = curve->sample(curve->dataSize() - 1);
  const double tolerance = std::max(1e-9, std::abs(last.x() - first.x()) * 1e-12);
  if (x < first.x() - tolerance || x > last.x() + tolerance)
  {
    return std::nullopt;
  }
  if (curve->dataSize() == 1)
  {
    return first;
  }

  const int index = qwtUpperSampleIndex<QPointF>(*curve->data(), x, CompareX());
  if (index <= 0)
  {
    return first;
  }
  if (index >= static_cast<int>(curve->dataSize()))
  {
    return last;
  }
  const QPointF left = curve->sample(index - 1);
  const QPointF right = curve->sample(index);
  return (x < (left.x() + right.x()) * 0.5) ? left : right;
}

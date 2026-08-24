/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <QColor>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <optional>
#include <vector>

class QwtPlot;
class QwtPlotCurve;
class QwtPlotMarker;

std::optional<QPointF> curvePointAt(const QwtPlotCurve* curve, double x);

class CurveTracker : public QObject
{
  Q_OBJECT
public:
  explicit CurveTracker(QwtPlot*, QColor color, QString label = {}, bool prefer_right = true,
                        std::vector<QRectF>* occupied_labels = nullptr);

  ~CurveTracker() override;

  QPointF actualPosition() const;

  typedef enum
  {
    LINE_ONLY,
    VALUE
  } Parameter;

public slots:

  void setPosition(const QPointF& pos);

  void setReferencePosition(std::optional<QPointF> reference_pos);

  void setParameter(Parameter par);

  void setPrecision(int precision);

  void setEnabled(bool enable);

  bool isEnabled() const;

  void redraw()
  {
    setPosition(_prev_trackerpoint);
  }

private:
  QPointF _prev_trackerpoint;
  std::optional<QPointF> _reference_pos;
  std::vector<QwtPlotMarker*> _point_markers;
  std::vector<QwtPlotMarker*> _value_markers;
  QwtPlotMarker* _line_marker;
  QwtPlot* _plot;
  QColor _color;
  QString _label;
  bool _prefer_right;
  std::vector<QRectF>* _occupied_labels;
  std::vector<QRectF> _local_occupied_labels;
  Parameter _param;
  int _precision;
  bool _visible;
};

#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "curve_tracker.h"
#include "linked_zoom_policy.h"
#include "ruler_metrics.h"
#include "tracker_label_layout.h"
#include "qwt_plot_curve.h"

TEST(RulerMetrics, CalculatesBMinusAFromRosbagSamples)
{
  PJ::PlotData series("speed", {});
  series.pushBack({ 1.0, 10.0 });
  series.pushBack({ 2.0, 14.5 });
  series.pushBack({ 3.0, 20.0 });

  const auto metrics = CalculateRulerMetrics(series, 1.1, 2.9);
  ASSERT_TRUE(metrics.has_value());
  EXPECT_EQ(metrics->a.frame, 0);
  EXPECT_EQ(metrics->b.frame, 2);
  EXPECT_EQ(metrics->delta_frames, 2);
  EXPECT_DOUBLE_EQ(metrics->a.value, 10.0);
  EXPECT_DOUBLE_EQ(metrics->b.value, 20.0);
  EXPECT_DOUBLE_EQ(metrics->delta_value, 10.0);
  EXPECT_DOUBLE_EQ(metrics->delta_time, 1.8);
}

TEST(RulerMetrics, PreservesSignedReverseDifferences)
{
  PJ::PlotData series("speed", {});
  series.pushBack({ 1.0, 10.0 });
  series.pushBack({ 2.0, 15.0 });

  const auto metrics = CalculateRulerMetrics(series, 2.0, 1.0);
  ASSERT_TRUE(metrics.has_value());
  EXPECT_DOUBLE_EQ(metrics->delta_time, -1.0);
  EXPECT_DOUBLE_EQ(metrics->delta_value, -5.0);
  EXPECT_EQ(metrics->delta_frames, -1);
}

TEST(RulerMetrics, RejectsEmptySeries)
{
  PJ::PlotData empty("empty", {});
  EXPECT_FALSE(CalculateRulerMetrics(empty, 0.0, 1.0).has_value());
  empty.pushBack({ 0.0, 1.0 });
  EXPECT_FALSE(
      CalculateRulerMetrics(empty, std::numeric_limits<double>::quiet_NaN(), 1.0).has_value());
}

TEST(TrackerLabelLayout, KeepsLabelsInsideCanvas)
{
  const QRectF canvas(0.0, 0.0, 100.0, 80.0);
  const QRectF label =
      PlaceTrackerLabel(QPointF(98.0, 2.0), QSizeF(40.0, 20.0), canvas, {}, true);
  EXPECT_TRUE(canvas.contains(label));
}

TEST(TrackerLabelLayout, AvoidsExistingLabels)
{
  const QRectF canvas(0.0, 0.0, 300.0, 200.0);
  const QRectF occupied(106.0, 90.0, 60.0, 20.0);
  const QRectF label = PlaceTrackerLabel(QPointF(100.0, 100.0), QSizeF(60.0, 20.0),
                                        canvas, { occupied }, true);
  EXPECT_FALSE(label.intersects(occupied));
}

TEST(TrackerLabelLayout, SeparatesDenseIntersectionLabels)
{
  const QRectF canvas(0.0, 0.0, 400.0, 300.0);
  const QPointF anchor(200.0, 150.0);
  std::vector<QRectF> labels;
  for (int index = 0; index < 6; ++index)
  {
    const QRectF next =
        PlaceTrackerLabel(anchor, QSizeF(80.0, 24.0), canvas, labels, true);
    for (const QRectF& previous : labels)
    {
      EXPECT_FALSE(next.intersects(previous));
    }
    labels.push_back(next);
  }
}

TEST(TrackerLabelLayout, UsesOppositePreferredSidesForRulers)
{
  const QRectF canvas(0.0, 0.0, 400.0, 300.0);
  const QPointF anchor(200.0, 150.0);
  const QRectF right = PlaceTrackerLabel(anchor, QSizeF(80.0, 24.0), canvas, {}, true);
  const QRectF left = PlaceTrackerLabel(anchor, QSizeF(80.0, 24.0), canvas, {}, false);
  EXPECT_GT(right.left(), anchor.x());
  EXPECT_LT(left.right(), anchor.x());
}

TEST(LinkedZoomPolicy, KeepsCompatibleTimeRangesLinked)
{
  EXPECT_FALSE(ShouldFitLinkedPlotsIndependently({ { 0.0, 10.0 }, { 1.0, 9.0 } }));
}

TEST(LinkedZoomPolicy, SeparatesExtremelyDistantTimeRanges)
{
  EXPECT_TRUE(ShouldFitLinkedPlotsIndependently({ { 0.0, 1.0 }, { 1000.0, 1001.0 } }));
}

TEST(LinkedZoomPolicy, IgnoresInvalidRanges)
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(ShouldFitLinkedPlotsIndependently({ { 0.0, 1.0 }, { nan, 10.0 } }));
}

TEST(CurveTrackerSampling, RejectsPointsOutsideCurveTimeDomain)
{
  QwtPlotCurve curve;
  curve.setSamples(QVector<QPointF>{ { 1.0, 10.0 }, { 2.0, 20.0 }, { 3.0, 30.0 } });
  EXPECT_FALSE(curvePointAt(&curve, 0.9).has_value());
  EXPECT_FALSE(curvePointAt(&curve, 3.1).has_value());
  ASSERT_TRUE(curvePointAt(&curve, 1.0).has_value());
  EXPECT_DOUBLE_EQ(curvePointAt(&curve, 1.0)->y(), 10.0);
}

TEST(CurveTrackerSampling, UsesNearestSampleAndSupportsSinglePoint)
{
  QwtPlotCurve curve;
  curve.setSamples(QVector<QPointF>{ { 1.0, 10.0 }, { 2.0, 20.0 } });
  ASSERT_TRUE(curvePointAt(&curve, 1.6).has_value());
  EXPECT_DOUBLE_EQ(curvePointAt(&curve, 1.6)->y(), 20.0);

  curve.setSamples(QVector<QPointF>{ { 4.0, 42.0 } });
  ASSERT_TRUE(curvePointAt(&curve, 4.0).has_value());
  EXPECT_DOUBLE_EQ(curvePointAt(&curve, 4.0)->y(), 42.0);
}

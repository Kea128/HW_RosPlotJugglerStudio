#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

#include "PlotJuggler/plotdata.h"

struct RulerSample
{
  double ruler_time = 0.0;
  double sample_time = 0.0;
  double value = 0.0;
  int64_t frame = 0;
};

struct RulerMetrics
{
  RulerSample a;
  RulerSample b;
  double delta_time = 0.0;
  double delta_value = 0.0;
  int64_t delta_frames = 0;
};

inline std::optional<RulerMetrics> CalculateRulerMetrics(const PJ::PlotData& series,
                                                         double time_a, double time_b)
{
  if (series.size() == 0 || !std::isfinite(time_a) || !std::isfinite(time_b))
  {
    return std::nullopt;
  }

  const int index_a = series.getIndexFromX(time_a);
  const int index_b = series.getIndexFromX(time_b);
  if (index_a < 0 || index_b < 0)
  {
    return std::nullopt;
  }

  const auto& sample_a = series.at(static_cast<size_t>(index_a));
  const auto& sample_b = series.at(static_cast<size_t>(index_b));
  if (!std::isfinite(sample_a.x) || !std::isfinite(sample_a.y) ||
      !std::isfinite(sample_b.x) || !std::isfinite(sample_b.y))
  {
    return std::nullopt;
  }

  RulerMetrics metrics;
  metrics.a = { time_a, sample_a.x, sample_a.y, index_a };
  metrics.b = { time_b, sample_b.x, sample_b.y, index_b };
  metrics.delta_time = time_b - time_a;
  metrics.delta_value = sample_b.y - sample_a.y;
  metrics.delta_frames = static_cast<int64_t>(index_b) - index_a;
  return metrics;
}

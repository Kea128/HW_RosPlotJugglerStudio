#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

struct LinkedZoomRange
{
  double min = 0.0;
  double max = 0.0;
};

inline bool ShouldFitLinkedPlotsIndependently(const std::vector<LinkedZoomRange>& ranges)
{
  if (ranges.size() < 2)
  {
    return false;
  }

  double union_min = std::numeric_limits<double>::max();
  double union_max = std::numeric_limits<double>::lowest();
  double smallest_span = std::numeric_limits<double>::max();
  int valid_ranges = 0;

  for (const auto& range : ranges)
  {
    if (!std::isfinite(range.min) || !std::isfinite(range.max))
    {
      continue;
    }
    const double left = std::min(range.min, range.max);
    const double right = std::max(range.min, range.max);
    union_min = std::min(union_min, left);
    union_max = std::max(union_max, right);
    const double span = right - left;
    if (span > std::numeric_limits<double>::epsilon())
    {
      smallest_span = std::min(smallest_span, span);
    }
    ++valid_ranges;
  }

  if (valid_ranges < 2 || union_max < union_min)
  {
    return false;
  }

  const double union_span = union_max - union_min;
  if (smallest_span == std::numeric_limits<double>::max())
  {
    return union_span > 1.0;
  }

  // A common range more than two orders of magnitude wider than the shortest
  // plotted signal makes that signal effectively invisible on screen.
  const double maximum_useful_union = std::max(1.0, smallest_span * 100.0);
  return union_span > maximum_useful_union;
}

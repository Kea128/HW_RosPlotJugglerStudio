#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <algorithm>
#include <limits>
#include <vector>

inline QRectF PlaceTrackerLabel(const QPointF& anchor, const QSizeF& requested_size,
                                const QRectF& canvas, const std::vector<QRectF>& occupied,
                                bool prefer_right)
{
  if (!canvas.isValid())
  {
    return {};
  }

  const QSizeF size(std::clamp(requested_size.width(), 0.0, canvas.width()),
                    std::clamp(requested_size.height(), 0.0, canvas.height()));
  constexpr double gap = 6.0;

  const auto clampToCanvas = [&](QRectF rect) {
    rect.moveLeft(std::clamp(rect.left(), canvas.left(), canvas.right() - rect.width()));
    rect.moveTop(std::clamp(rect.top(), canvas.top(), canvas.bottom() - rect.height()));
    return rect;
  };

  const auto overlapArea = [&](const QRectF& candidate) {
    double area = 0.0;
    for (const QRectF& other : occupied)
    {
      const QRectF intersection = candidate.intersected(other);
      area += std::max(0.0, intersection.width()) * std::max(0.0, intersection.height());
    }
    return area;
  };

  QRectF best;
  double best_overlap = std::numeric_limits<double>::max();
  double best_distance = std::numeric_limits<double>::max();
  const double vertical_step = std::max(gap, size.height() + gap);

  for (int side_index = 0; side_index < 2; ++side_index)
  {
    const bool right = (side_index == 0) ? prefer_right : !prefer_right;
    const double x = right ? anchor.x() + gap : anchor.x() - gap - size.width();

    for (int offset_index = 0; offset_index < 33; ++offset_index)
    {
      const int level = (offset_index + 1) / 2;
      const double direction = (offset_index == 0 || offset_index % 2 == 1) ? -1.0 : 1.0;
      const double offset = (offset_index == 0) ? 0.0 : direction * level * vertical_step;
      QRectF candidate(QPointF(x, anchor.y() - size.height() / 2.0 + offset), size);
      candidate = clampToCanvas(candidate);

      const double overlap = overlapArea(candidate);
      const double distance =
          std::abs(candidate.center().x() - anchor.x()) +
          std::abs(candidate.center().y() - anchor.y()) + side_index * gap;
      if (overlap < best_overlap || (overlap == best_overlap && distance < best_distance))
      {
        best = candidate;
        best_overlap = overlap;
        best_distance = distance;
      }
      if (overlap == 0.0)
      {
        return candidate;
      }
    }
  }
  return best;
}

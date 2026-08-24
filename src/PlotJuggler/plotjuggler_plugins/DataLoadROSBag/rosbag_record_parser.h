#pragma once

#include <QByteArray>

#include <cstddef>
#include <string>
#include <unordered_map>

#include "PlotJuggler/plotdata.h"

namespace PJ
{
namespace ROSBag
{

// Parses one data record emitted by runtime/rosbag_python/extract_rosbag.py.
// Metadata records (INFO/PROGRESS/DONE/ERROR/WARN) are intentionally handled
// by the process owner rather than this class.
class RecordParser
{
public:
  explicit RecordParser(PlotDataMapRef& destination);

  bool parse(const QByteArray& record);
  size_t recordCount() const;

private:
  PlotDataMapRef& _destination;
  std::unordered_map<std::string, PlotData*> _numeric_cache;
  std::unordered_map<std::string, StringSeries*> _string_cache;
  size_t _record_count = 0;
};

}  // namespace ROSBag
}  // namespace PJ
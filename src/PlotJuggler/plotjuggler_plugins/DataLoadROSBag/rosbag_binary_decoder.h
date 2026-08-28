#pragma once

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PlotJuggler/plotdata.h"

namespace PJ
{
namespace ROSBag
{

class RosbagBinaryDecoder
{
public:
  explicit RosbagBinaryDecoder(PlotDataMapRef& destination);

  bool append(const void* data, size_t size);
  bool append(const QByteArray& data);
  bool finish();

  size_t recordCount() const;
  uint64_t messages() const;
  bool done() const;

private:
  struct Series
  {
    uint8_t kind = 0;
    PlotData* numeric = nullptr;
    StringSeries* string = nullptr;
  };

  bool process();
  bool parseFrame(uint8_t type, const uint8_t* payload, size_t size);
  bool parseSeries(const uint8_t* payload, size_t size);
  bool parseNumeric(const uint8_t* payload, size_t size);
  bool parseStrings(const uint8_t* payload, size_t size);
  bool parseDone(const uint8_t* payload, size_t size);
  bool fail();

  PlotDataMapRef& _destination;
  std::vector<uint8_t> _buffer;
  size_t _offset = 0;
  std::unordered_map<uint32_t, Series> _series;
  std::unordered_set<std::string> _series_names;
  size_t _record_count = 0;
  uint64_t _messages = 0;
  bool _header_read = false;
  bool _done = false;
  bool _failed = false;
};

}  // namespace ROSBag
}  // namespace PJ

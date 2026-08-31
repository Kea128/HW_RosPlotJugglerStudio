#include "rosbag_binary_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace PJ
{
namespace ROSBag
{

namespace
{
constexpr size_t HEADER_SIZE = 12;
constexpr size_t FRAME_HEADER_SIZE = 5;
constexpr uint32_t MAX_FRAME_SIZE = 64U * 1024U * 1024U;
constexpr uint32_t MAX_NAME_SIZE = 1024U * 1024U;

uint32_t readU32(const uint8_t* data)
{
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) | (static_cast<uint32_t>(data[3]) << 24U);
}

uint64_t readU64(const uint8_t* data)
{
  uint64_t value = 0;
  for (unsigned index = 0; index < 8; ++index)
  {
    value |= static_cast<uint64_t>(data[index]) << (index * 8U);
  }
  return value;
}

int64_t readI64(const uint8_t* data)
{
  const uint64_t bits = readU64(data);
  int64_t value = 0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

double readDouble(const uint8_t* data)
{
  const uint64_t bits = readU64(data);
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool validUtf8(const uint8_t* data, size_t size)
{
  size_t pos = 0;
  while (pos < size)
  {
    const uint8_t first = data[pos++];
    if (first <= 0x7F)
    {
      continue;
    }

    size_t trailing = 0;
    uint32_t codepoint = 0;
    uint32_t minimum = 0;
    if (first >= 0xC2 && first <= 0xDF)
    {
      trailing = 1;
      codepoint = first & 0x1FU;
      minimum = 0x80;
    }
    else if (first >= 0xE0 && first <= 0xEF)
    {
      trailing = 2;
      codepoint = first & 0x0FU;
      minimum = 0x800;
    }
    else if (first >= 0xF0 && first <= 0xF4)
    {
      trailing = 3;
      codepoint = first & 0x07U;
      minimum = 0x10000;
    }
    else
    {
      return false;
    }

    if (trailing > size - pos)
    {
      return false;
    }
    for (size_t index = 0; index < trailing; ++index)
    {
      const uint8_t next = data[pos++];
      if ((next & 0xC0U) != 0x80U)
      {
        return false;
      }
      codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    if (codepoint < minimum || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
    {
      return false;
    }
  }
  return true;
}
}  // namespace

RosbagBinaryDecoder::RosbagBinaryDecoder(PlotDataMapRef& destination) : _destination(destination)
{
}

bool RosbagBinaryDecoder::append(const void* data, size_t size)
{
  if (_failed || (_done && size != 0) || (data == nullptr && size != 0))
  {
    return fail();
  }
  if (size == 0)
  {
    return true;
  }

  const auto* bytes = static_cast<const uint8_t*>(data);
  _buffer.insert(_buffer.end(), bytes, bytes + size);
  return process();
}

bool RosbagBinaryDecoder::append(const QByteArray& data)
{
  return append(data.constData(), static_cast<size_t>(data.size()));
}

bool RosbagBinaryDecoder::finish()
{
  if (_failed || !process())
  {
    return false;
  }
  if (!_header_read || !_done || _offset != _buffer.size())
  {
    return fail();
  }
  return true;
}

size_t RosbagBinaryDecoder::recordCount() const
{
  return _record_count;
}

uint64_t RosbagBinaryDecoder::messages() const
{
  return _messages;
}

bool RosbagBinaryDecoder::done() const
{
  return _done;
}

bool RosbagBinaryDecoder::process()
{
  if (_failed)
  {
    return false;
  }

  if (!_header_read)
  {
    if (_buffer.size() - _offset < HEADER_SIZE)
    {
      return true;
    }
    const uint8_t expected_magic[8] = { 'R', 'S', 'P', 'J', 'B', 'A', 'G', 0 };
    const uint8_t* header = _buffer.data() + _offset;
    if (!std::equal(std::begin(expected_magic), std::end(expected_magic), header) ||
        header[8] != 1 || header[9] != 0 || header[10] != 0 || header[11] != 0)
    {
      return fail();
    }
    _offset += HEADER_SIZE;
    _header_read = true;
  }

  while (_buffer.size() - _offset >= FRAME_HEADER_SIZE)
  {
    if (_done)
    {
      return fail();
    }

    const uint8_t* frame = _buffer.data() + _offset;
    const uint8_t type = frame[0];
    const uint32_t payload_size = readU32(frame + 1);
    if (payload_size > MAX_FRAME_SIZE)
    {
      return fail();
    }
    if (_buffer.size() - _offset - FRAME_HEADER_SIZE < payload_size)
    {
      break;
    }
    if (!parseFrame(type, frame + FRAME_HEADER_SIZE, payload_size))
    {
      return false;
    }
    _offset += FRAME_HEADER_SIZE + payload_size;
  }

  if (_done && _offset != _buffer.size())
  {
    return fail();
  }
  if (_offset != 0 && (_offset == _buffer.size() || _offset > 1024U * 1024U))
  {
    _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<std::ptrdiff_t>(_offset));
    _offset = 0;
  }
  return true;
}

bool RosbagBinaryDecoder::parseFrame(uint8_t type, const uint8_t* payload, size_t size)
{
  switch (type)
  {
    case 1:
      return parseSeries(payload, size);
    case 2:
      return parseNumeric(payload, size);
    case 3:
      return parseStrings(payload, size);
    case 4:
      return parseDone(payload, size);
    default:
      return fail();
  }
}

bool RosbagBinaryDecoder::parseSeries(const uint8_t* payload, size_t size)
{
  if (size < 9)
  {
    return fail();
  }
  const uint32_t id = readU32(payload);
  const uint8_t kind = payload[4];
  const uint32_t name_size = readU32(payload + 5);
  if ((kind != 1 && kind != 2) || name_size == 0 || name_size > MAX_NAME_SIZE ||
      size != 9ULL + name_size ||
      std::find(payload + 9, payload + 9 + name_size, uint8_t{ 0 }) !=
          payload + 9 + name_size ||
      !validUtf8(payload + 9, name_size))
  {
    return fail();
  }

  const std::string name(reinterpret_cast<const char*>(payload + 9), name_size);
  std::string typed_name(1, static_cast<char>(kind));
  typed_name += name;
  if (_series.count(id) != 0 || !_series_names.emplace(std::move(typed_name)).second)
  {
    return fail();
  }

  Series series;
  series.kind = kind;
  if (kind == 1)
  {
    series.numeric = &_destination.getOrCreateNumeric(name);
  }
  else
  {
    series.string = &_destination.getOrCreateStringSeries(name);
  }
  _series.emplace(id, series);
  return true;
}

bool RosbagBinaryDecoder::parseNumeric(const uint8_t* payload, size_t size)
{
  if (size < 8)
  {
    return fail();
  }
  const uint32_t id = readU32(payload);
  const uint32_t count = readU32(payload + 4);
  if (size != 8ULL + static_cast<uint64_t>(count) * 16ULL)
  {
    return fail();
  }
  const auto found = _series.find(id);
  if (found == _series.end() || found->second.kind != 1)
  {
    return fail();
  }
  if (count > std::numeric_limits<size_t>::max() - _record_count)
  {
    return fail();
  }

  const uint8_t* timestamps = payload + 8;
  const uint8_t* values = timestamps + static_cast<size_t>(count) * 8;
  for (uint32_t index = 0; index < count; ++index)
  {
    if (!std::isfinite(readDouble(values + static_cast<size_t>(index) * 8)))
    {
      return fail();
    }
  }
  for (uint32_t index = 0; index < count; ++index)
  {
    const double timestamp =
        static_cast<double>(readI64(timestamps + static_cast<size_t>(index) * 8)) * 1.0e-9;
    const double value = readDouble(values + static_cast<size_t>(index) * 8);
    found->second.numeric->pushBack({ timestamp, value });
  }
  _record_count += count;
  return true;
}

bool RosbagBinaryDecoder::parseStrings(const uint8_t* payload, size_t size)
{
  if (size < 8)
  {
    return fail();
  }
  const uint32_t id = readU32(payload);
  const uint32_t count = readU32(payload + 4);
  const auto found = _series.find(id);
  if (found == _series.end() || found->second.kind != 2 ||
      count > std::numeric_limits<size_t>::max() - _record_count)
  {
    return fail();
  }

  size_t pos = 8;
  for (uint32_t index = 0; index < count; ++index)
  {
    if (size - pos < 12)
    {
      return fail();
    }
    const uint32_t string_size = readU32(payload + pos + 8);
    pos += 12;
    if (string_size > size - pos || !validUtf8(payload + pos, string_size))
    {
      return fail();
    }
    pos += string_size;
  }
  if (pos != size)
  {
    return fail();
  }

  pos = 8;
  for (uint32_t index = 0; index < count; ++index)
  {
    const double timestamp = static_cast<double>(readI64(payload + pos)) * 1.0e-9;
    const uint32_t string_size = readU32(payload + pos + 8);
    pos += 12;
    found->second.string->pushBack(
        { timestamp, StringRef(reinterpret_cast<const char*>(payload + pos), string_size) });
    pos += string_size;
  }
  _record_count += count;
  return true;
}

bool RosbagBinaryDecoder::parseDone(const uint8_t* payload, size_t size)
{
  if (size != 16)
  {
    return fail();
  }
  const uint64_t emitted = readU64(payload + 8);
  if (emitted != _record_count)
  {
    return fail();
  }
  _messages = readU64(payload);
  _done = true;
  return true;
}

bool RosbagBinaryDecoder::fail()
{
  _failed = true;
  return false;
}

}  // namespace ROSBag
}  // namespace PJ

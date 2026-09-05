#include "rosbag_raw_decoder.h"

#include <algorithm>
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
constexpr uint32_t MAX_TEXT_SIZE = 8U * 1024U * 1024U;

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

bool readSizedString(const uint8_t* payload, size_t size, size_t& pos, std::string& out,
                     bool allow_empty)
{
  if (size - pos < 4)
  {
    return false;
  }
  const uint32_t length = readU32(payload + pos);
  pos += 4;
  if ((!allow_empty && length == 0) || length > MAX_TEXT_SIZE || size - pos < length)
  {
    return false;
  }
  out.assign(reinterpret_cast<const char*>(payload + pos), length);
  pos += length;
  return true;
}
}  // namespace

RosbagRawDecoder::RosbagRawDecoder(PlotDataMapRef& destination, ParserFactory factory,
                                   int max_array)
  : _destination(destination), _factory(std::move(factory)), _max_array(max_array)
{
}

bool RosbagRawDecoder::append(const void* data, size_t size)
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

bool RosbagRawDecoder::append(const QByteArray& data)
{
  return append(data.constData(), static_cast<size_t>(data.size()));
}

bool RosbagRawDecoder::finish()
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

uint64_t RosbagRawDecoder::messages() const
{
  return _messages;
}

uint64_t RosbagRawDecoder::payloadBytes() const
{
  return _payload_bytes;
}

bool RosbagRawDecoder::done() const
{
  return _done;
}

QStringList RosbagRawDecoder::failedTopics() const
{
  return _failed_topics;
}

QStringList RosbagRawDecoder::warnings() const
{
  return _warnings;
}

bool RosbagRawDecoder::process()
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
    const uint8_t expected_magic[8] = { 'R', 'S', 'P', 'J', 'R', 'A', 'W', 0 };
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

bool RosbagRawDecoder::parseFrame(uint8_t type, const uint8_t* payload, size_t size)
{
  switch (type)
  {
    case 1:
      return parseConnection(payload, size);
    case 2:
      return parseMessages(payload, size);
    case 3:
      return parseDone(payload, size);
    default:
      return fail();
  }
}

bool RosbagRawDecoder::parseConnection(const uint8_t* payload, size_t size)
{
  if (size < 16)
  {
    return fail();
  }
  const uint32_t id = readU32(payload);
  size_t pos = 4;
  std::string topic;
  std::string type;
  std::string schema;
  if (!readSizedString(payload, size, pos, topic, false) ||
      !readSizedString(payload, size, pos, type, false) ||
      !readSizedString(payload, size, pos, schema, true) || pos != size ||
      _connections.count(id) != 0)
  {
    return fail();
  }

  Connection connection;
  connection.topic = topic;
  connection.type = type;
  try
  {
    connection.parser = _factory(topic, type, schema);
    if (connection.parser)
    {
      connection.parser->setLargeArraysPolicy(true, static_cast<unsigned>(_max_array));
    }
  }
  catch (const std::exception& error)
  {
    connection.parser.reset();
    _warnings.push_back(
        QStringLiteral("WARN\t%1\tschema: %2")
            .arg(QString::fromStdString(topic), QString::fromUtf8(error.what())));
  }
  if (!connection.parser)
  {
    connection.failed = true;
    const QString topic_name = QString::fromStdString(topic);
    if (!_failed_topics.contains(topic_name))
    {
      _failed_topics.push_back(topic_name);
    }
  }
  _connections.emplace(id, std::move(connection));
  return true;
}

bool RosbagRawDecoder::parseMessages(const uint8_t* payload, size_t size)
{
  if (size < 4)
  {
    return fail();
  }
  const uint32_t count = readU32(payload);
  size_t pos = 4;
  for (uint32_t index = 0; index < count; ++index)
  {
    if (size - pos < 16)
    {
      return fail();
    }
    const uint32_t id = readU32(payload + pos);
    const int64_t timestamp_ns = readI64(payload + pos + 4);
    const uint32_t payload_size = readU32(payload + pos + 12);
    pos += 16;
    if (size - pos < payload_size)
    {
      return fail();
    }

    const auto found = _connections.find(id);
    if (found == _connections.end())
    {
      return fail();
    }
    auto& connection = found->second;
    if (!connection.failed && connection.parser)
    {
      try
      {
        double timestamp = static_cast<double>(timestamp_ns) * 1.0e-9;
        MessageRef message(payload + pos, payload_size);
        connection.parser->parseMessage(message, timestamp);
      }
      catch (const std::exception& error)
      {
        if (_warned_topics.insert(connection.topic).second)
        {
          _warnings.push_back(
              QStringLiteral("WARN\t%1\tparse: %2")
                  .arg(QString::fromStdString(connection.topic),
                       QString::fromUtf8(error.what())));
        }
      }
    }
    _messages += 1;
    _payload_bytes += payload_size;
    pos += payload_size;
  }
  if (pos != size)
  {
    return fail();
  }
  return true;
}

bool RosbagRawDecoder::parseDone(const uint8_t* payload, size_t size)
{
  if (size != 16)
  {
    return fail();
  }
  const uint64_t messages = readU64(payload);
  const uint64_t bytes = readU64(payload + 8);
  if (messages != _messages || bytes != _payload_bytes)
  {
    return fail();
  }
  _done = true;
  return true;
}

bool RosbagRawDecoder::fail()
{
  _failed = true;
  return false;
}

}  // namespace ROSBag
}  // namespace PJ

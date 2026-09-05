#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "PlotJuggler/messageparser_base.h"
#include "PlotJuggler/plotdata.h"

namespace PJ
{
namespace ROSBag
{

class RosbagRawDecoder
{
public:
  using ParserFactory = std::function<MessageParserPtr(
      const std::string& topic, const std::string& type, const std::string& schema)>;

  RosbagRawDecoder(PlotDataMapRef& destination, ParserFactory factory, int max_array);

  bool append(const void* data, size_t size);
  bool append(const QByteArray& data);
  bool finish();

  uint64_t messages() const;
  uint64_t payloadBytes() const;
  bool done() const;
  QStringList failedTopics() const;
  QStringList warnings() const;

private:
  struct Connection
  {
    std::string topic;
    std::string type;
    MessageParserPtr parser;
    bool failed = false;
  };

  bool process();
  bool parseFrame(uint8_t type, const uint8_t* payload, size_t size);
  bool parseConnection(const uint8_t* payload, size_t size);
  bool parseMessages(const uint8_t* payload, size_t size);
  bool parseDone(const uint8_t* payload, size_t size);
  bool fail();

  PlotDataMapRef& _destination;
  ParserFactory _factory;
  int _max_array = 100;
  std::vector<uint8_t> _buffer;
  size_t _offset = 0;
  std::unordered_map<uint32_t, Connection> _connections;
  std::unordered_set<std::string> _warned_topics;
  QStringList _failed_topics;
  QStringList _warnings;
  uint64_t _messages = 0;
  uint64_t _payload_bytes = 0;
  bool _header_read = false;
  bool _done = false;
  bool _failed = false;
};

}  // namespace ROSBag
}  // namespace PJ

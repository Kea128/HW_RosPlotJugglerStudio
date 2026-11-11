#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "ros_parser.h"
#include "rosbag_raw_decoder.h"

namespace
{
using Bytes = std::vector<uint8_t>;

void appendBytes(Bytes& dest, const void* data, size_t size)
{
  const auto* bytes = static_cast<const uint8_t*>(data);
  dest.insert(dest.end(), bytes, bytes + size);
}

void appendU16(Bytes& dest, uint16_t value)
{
  appendBytes(dest, &value, sizeof(value));
}

void appendU32(Bytes& dest, uint32_t value)
{
  appendBytes(dest, &value, sizeof(value));
}

void appendU64(Bytes& dest, uint64_t value)
{
  appendBytes(dest, &value, sizeof(value));
}

void appendI64(Bytes& dest, int64_t value)
{
  appendBytes(dest, &value, sizeof(value));
}

void appendF64(Bytes& dest, double value)
{
  appendBytes(dest, &value, sizeof(value));
}

void appendString(Bytes& dest, const std::string& value)
{
  appendU32(dest, static_cast<uint32_t>(value.size()));
  appendBytes(dest, value.data(), value.size());
}

Bytes header()
{
  Bytes stream = { 'R', 'S', 'P', 'J', 'R', 'A', 'W', 0 };
  appendU16(stream, 1);
  appendU16(stream, 0);
  return stream;
}

void frame(Bytes& stream, uint8_t type, const Bytes& payload)
{
  stream.push_back(type);
  appendU32(stream, static_cast<uint32_t>(payload.size()));
  stream.insert(stream.end(), payload.begin(), payload.end());
}

Bytes connection(uint32_t id, const std::string& topic, const std::string& type,
                 const std::string& schema)
{
  Bytes payload;
  appendU32(payload, id);
  appendString(payload, topic);
  appendString(payload, type);
  appendString(payload, schema);
  return payload;
}

Bytes oneMessage(uint32_t id, int64_t timestamp, const Bytes& raw)
{
  Bytes payload;
  appendU32(payload, 1);
  appendU32(payload, id);
  appendI64(payload, timestamp);
  appendU32(payload, static_cast<uint32_t>(raw.size()));
  payload.insert(payload.end(), raw.begin(), raw.end());
  return payload;
}

Bytes done(uint64_t messages, uint64_t bytes)
{
  Bytes payload;
  appendU64(payload, messages);
  appendU64(payload, bytes);
  return payload;
}

PJ::MessageParserPtr makeParser(PJ::PlotDataMapRef& data, const std::string& topic,
                                const std::string& type, const std::string& schema)
{
  auto parser = std::make_shared<ParserROS>(topic, type, schema,
                                            new RosMsgParser::ROS_Deserializer(), data);
  parser->enableTruncationCheck(false);
  return parser;
}

TEST(RosbagRawDecoder, DecodesNumericAndStringMessages)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [&data](const std::string& topic, const std::string& type, const std::string& schema) {
        return makeParser(data, topic, type, schema);
      },
      16);

  Bytes float_payload;
  appendF64(float_payload, 12.5);
  Bytes string_payload;
  const std::string text = "hello";
  appendU32(string_payload, static_cast<uint32_t>(text.size()));
  appendBytes(string_payload, text.data(), text.size());

  Bytes stream = header();
  frame(stream, 1,
        connection(1, "/speed", "std_msgs/Float64", "float64 data\n"));
  frame(stream, 1,
        connection(2, "/label", "std_msgs/String", "string data\n"));
  frame(stream, 2, oneMessage(1, 1500000000, float_payload));
  frame(stream, 2, oneMessage(2, 2500000000, string_payload));
  frame(stream, 3, done(2, float_payload.size() + string_payload.size()));

  ASSERT_TRUE(decoder.append(stream.data(), stream.size()));
  ASSERT_TRUE(decoder.finish());
  EXPECT_TRUE(decoder.done());
  EXPECT_EQ(decoder.messages(), 2u);
  ASSERT_EQ(data.numeric.count("/speed/data"), 1u);
  ASSERT_EQ(data.numeric.at("/speed/data").size(), 1u);
  EXPECT_DOUBLE_EQ(data.numeric.at("/speed/data").at(0).y, 12.5);
  ASSERT_EQ(data.strings.count("/label/data"), 1u);
  EXPECT_EQ(data.strings.at("/label/data").getString(data.strings.at("/label/data").at(0).y),
            "hello");
}

TEST(RosbagRawDecoder, RejectsTruncatedAndMismatchedDone)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [&data](const std::string& topic, const std::string& type, const std::string& schema) {
        return makeParser(data, topic, type, schema);
      },
      16);

  Bytes stream = header();
  EXPECT_FALSE(decoder.finish());

  PJ::PlotDataMapRef mismatch_data;
  PJ::ROSBag::RosbagRawDecoder mismatch(
      mismatch_data,
      [&mismatch_data](const std::string& topic, const std::string& type,
                       const std::string& schema) {
        return makeParser(mismatch_data, topic, type, schema);
      },
      16);
  stream = header();
  frame(stream, 3, done(1, 0));
  EXPECT_FALSE(mismatch.append(stream.data(), stream.size()));
}

TEST(RosbagRawDecoder, AcceptsEmptySchemaAndRecordsFailedTopic)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [](const std::string&, const std::string&, const std::string& schema) {
        if (schema.empty())
        {
          return PJ::MessageParserPtr{};
        }
        return PJ::MessageParserPtr{};
      },
      16);

  Bytes stream = header();
  frame(stream, 1, connection(1, "/custom", "pkg/Msg", ""));
  frame(stream, 3, done(0, 0));
  ASSERT_TRUE(decoder.append(stream.data(), stream.size()));
  ASSERT_TRUE(decoder.finish());
  ASSERT_EQ(decoder.failedTopics().size(), 1);
  EXPECT_EQ(decoder.failedTopics().front(), "/custom");
}

TEST(RosbagRawDecoder, RecordsFailedTopicsWhenFactoryReturnsNull)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [](const std::string&, const std::string&, const std::string&) {
        return PJ::MessageParserPtr{};
      },
      16);

  Bytes stream = header();
  frame(stream, 1, connection(1, "/custom", "pkg/Msg", "int32 data\n"));
  frame(stream, 3, done(0, 0));
  ASSERT_TRUE(decoder.append(stream.data(), stream.size()));
  ASSERT_TRUE(decoder.finish());
  ASSERT_EQ(decoder.failedTopics().size(), 1);
  EXPECT_EQ(decoder.failedTopics().front(), "/custom");
}

class ScriptedParser : public PJ::MessageParser
{
public:
  ScriptedParser(const std::string& topic, PJ::PlotDataMapRef& data, std::vector<bool> outcomes)
    : PJ::MessageParser(topic, data), _outcomes(std::move(outcomes))
  {
  }

  bool parseMessage(const PJ::MessageRef, double& timestamp) override
  {
    const bool ok = calls < _outcomes.size() && _outcomes[calls];
    calls++;
    getSeries(_topic_name + "/first_field").pushBack({ timestamp, 1.0 });
    if (!ok)
    {
      throw std::runtime_error("bad message");
    }
    getSeries(_topic_name + "/value").pushBack({ timestamp, 1.0 });
    return true;
  }

  size_t calls = 0;

private:
  std::vector<bool> _outcomes;
};

Bytes twoMessagesOn(uint32_t id)
{
  Bytes stream = header();
  frame(stream, 1, connection(id, "/scripted", "pkg/Msg", "int32 data\n"));
  frame(stream, 2, oneMessage(id, 1000000000, Bytes{ 1 }));
  frame(stream, 2, oneMessage(id, 2000000000, Bytes{ 2 }));
  frame(stream, 3, done(2, 2));
  return stream;
}

TEST(RosbagRawDecoder, HandsTopicToFallbackWhenItsFirstMessageFails)
{
  PJ::PlotDataMapRef data;
  std::shared_ptr<ScriptedParser> parser;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [&](const std::string& topic, const std::string&, const std::string&) {
        parser = std::make_shared<ScriptedParser>(topic, data, std::vector<bool>{ false, true });
        return parser;
      },
      16);

  const Bytes stream = twoMessagesOn(4);
  ASSERT_TRUE(decoder.append(stream.data(), stream.size()));
  ASSERT_TRUE(decoder.finish());
  EXPECT_EQ(decoder.messages(), 2u);
  ASSERT_EQ(decoder.failedTopics(), QStringList{ "/scripted" });
  ASSERT_EQ(decoder.failureReasons().size(), 1);
  EXPECT_TRUE(decoder.failureReasons().front().contains("bad message"));
  EXPECT_TRUE(decoder.warnings().isEmpty());
  EXPECT_EQ(parser->calls, 1u);
  EXPECT_EQ(data.numeric.count("/scripted/first_field"), 0u);
  EXPECT_EQ(data.numeric.count("/scripted/value"), 0u);
}

TEST(RosbagRawDecoder, KeepsTopicWhenAnotherConnectionAlreadyDecodedIt)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [&](const std::string& topic, const std::string&, const std::string& schema) {
        if (schema.empty())
        {
          return std::shared_ptr<ScriptedParser>{};
        }
        return std::make_shared<ScriptedParser>(
            topic, data, std::vector<bool>{ schema == "good\n" });
      },
      16);

  Bytes stream = header();
  frame(stream, 1, connection(1, "/scripted", "pkg/Msg", "good\n"));
  frame(stream, 1, connection(2, "/scripted", "pkg/Msg", "bad\n"));
  frame(stream, 2, oneMessage(1, 1000000000, Bytes{ 1 }));
  frame(stream, 2, oneMessage(2, 2000000000, Bytes{ 2 }));
  frame(stream, 1, connection(3, "/scripted", "pkg/Msg", ""));
  frame(stream, 2, oneMessage(3, 3000000000, Bytes{ 3 }));
  frame(stream, 3, done(3, 3));
  ASSERT_TRUE(decoder.append(stream.data(), stream.size()));
  ASSERT_TRUE(decoder.finish());
  EXPECT_TRUE(decoder.failedTopics().isEmpty());
  EXPECT_EQ(decoder.warnings().size(), 1);
  ASSERT_EQ(data.numeric.count("/scripted/value"), 1u);
  EXPECT_EQ(data.numeric.at("/scripted/value").size(), 1u);
}

TEST(RosbagRawDecoder, KeepsDecodedDataWhenALaterMessageFails)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [&](const std::string& topic, const std::string&, const std::string&) {
        return std::make_shared<ScriptedParser>(topic, data, std::vector<bool>{ true, false });
      },
      16);

  const Bytes stream = twoMessagesOn(5);
  ASSERT_TRUE(decoder.append(stream.data(), stream.size()));
  ASSERT_TRUE(decoder.finish());
  EXPECT_TRUE(decoder.failedTopics().isEmpty());
  ASSERT_EQ(decoder.warnings().size(), 1);
  EXPECT_TRUE(decoder.warnings().front().contains("bad message"));
  ASSERT_EQ(data.numeric.count("/scripted/value"), 1u);
  EXPECT_EQ(data.numeric.at("/scripted/value").size(), 1u);
  EXPECT_EQ(data.numeric.at("/scripted/first_field").size(), 2u);
}
}  // namespace

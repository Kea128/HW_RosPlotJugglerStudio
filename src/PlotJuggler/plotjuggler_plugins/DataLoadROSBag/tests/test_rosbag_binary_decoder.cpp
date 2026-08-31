#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "rosbag_binary_decoder.h"

namespace
{
using Bytes = std::vector<uint8_t>;

void u32(Bytes& out, uint32_t value)
{
  for (unsigned index = 0; index < 4; ++index)
  {
    out.push_back(static_cast<uint8_t>(value >> (index * 8U)));
  }
}

void u64(Bytes& out, uint64_t value)
{
  for (unsigned index = 0; index < 8; ++index)
  {
    out.push_back(static_cast<uint8_t>(value >> (index * 8U)));
  }
}

void f64(Bytes& out, double value)
{
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  u64(out, bits);
}

Bytes header(uint16_t version = 1)
{
  return { 'R', 'S', 'P', 'J', 'B', 'A', 'G', 0, static_cast<uint8_t>(version),
           static_cast<uint8_t>(version >> 8U), 0, 0 };
}

void frame(Bytes& stream, uint8_t type, const Bytes& payload)
{
  stream.push_back(type);
  u32(stream, static_cast<uint32_t>(payload.size()));
  stream.insert(stream.end(), payload.begin(), payload.end());
}

Bytes series(uint32_t id, uint8_t kind, const std::string& name)
{
  Bytes payload;
  u32(payload, id);
  payload.push_back(kind);
  u32(payload, static_cast<uint32_t>(name.size()));
  payload.insert(payload.end(), name.begin(), name.end());
  return payload;
}

Bytes numeric(uint32_t id, const std::vector<int64_t>& timestamps,
              const std::vector<double>& values)
{
  Bytes payload;
  u32(payload, id);
  u32(payload, static_cast<uint32_t>(timestamps.size()));
  for (const int64_t timestamp : timestamps)
  {
    u64(payload, static_cast<uint64_t>(timestamp));
  }
  for (const double value : values)
  {
    f64(payload, value);
  }
  return payload;
}

Bytes strings(uint32_t id, const std::vector<std::pair<int64_t, std::string>>& records)
{
  Bytes payload;
  u32(payload, id);
  u32(payload, static_cast<uint32_t>(records.size()));
  for (const auto& record : records)
  {
    u64(payload, static_cast<uint64_t>(record.first));
    u32(payload, static_cast<uint32_t>(record.second.size()));
    payload.insert(payload.end(), record.second.begin(), record.second.end());
  }
  return payload;
}

Bytes done(uint64_t messages, uint64_t emitted)
{
  Bytes payload;
  u64(payload, messages);
  u64(payload, emitted);
  return payload;
}

TEST(RosbagBinaryDecoder, DecodesNumericAndStringFramesAcrossEveryBoundary)
{
  Bytes stream = header();
  frame(stream, 1, series(7, 1, "/速度"));
  frame(stream, 1, series(9, 2, "/label"));
  frame(stream, 2, numeric(7, { 1500000000, 2500000000 }, { 12.5, -3.0 }));
  frame(stream, 3, strings(9, { { 42, "hello" }, { 43, "" }, { 44, "世界" } }));
  frame(stream, 4, done(11, 5));

  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagBinaryDecoder decoder(data);
  for (const uint8_t byte : stream)
  {
    ASSERT_TRUE(decoder.append(&byte, 1));
  }
  ASSERT_TRUE(decoder.finish());

  EXPECT_TRUE(decoder.done());
  EXPECT_EQ(decoder.messages(), 11u);
  EXPECT_EQ(decoder.recordCount(), 5u);
  const auto& numbers = data.numeric.at("/速度");
  ASSERT_EQ(numbers.size(), 2u);
  EXPECT_DOUBLE_EQ(numbers.at(0).x, 1.5);
  EXPECT_DOUBLE_EQ(numbers.at(0).y, 12.5);
  EXPECT_DOUBLE_EQ(numbers.at(1).x, 2.5);
  EXPECT_DOUBLE_EQ(numbers.at(1).y, -3.0);
  const auto& text = data.strings.at("/label");
  ASSERT_EQ(text.size(), 3u);
  EXPECT_EQ(text.getString(text.at(0).y), "hello");
  EXPECT_EQ(text.getString(text.at(1).y), "");
  EXPECT_EQ(text.getString(text.at(2).y), "世界");
}

TEST(RosbagBinaryDecoder, FinishRejectsTruncatedHeaderFrameAndMissingDone)
{
  PJ::PlotDataMapRef data1;
  PJ::ROSBag::RosbagBinaryDecoder short_header(data1);
  const Bytes partial_header = { 'R', 'S', 'P' };
  EXPECT_TRUE(short_header.append(partial_header.data(), partial_header.size()));
  EXPECT_FALSE(short_header.finish());

  PJ::PlotDataMapRef data2;
  PJ::ROSBag::RosbagBinaryDecoder short_frame(data2);
  Bytes partial_frame = header();
  partial_frame.push_back(1);
  u32(partial_frame, 20);
  partial_frame.push_back(0);
  EXPECT_TRUE(short_frame.append(partial_frame.data(), partial_frame.size()));
  EXPECT_FALSE(short_frame.finish());

  PJ::PlotDataMapRef data3;
  PJ::ROSBag::RosbagBinaryDecoder missing_done(data3);
  const Bytes complete_header = header();
  EXPECT_TRUE(missing_done.append(complete_header.data(), complete_header.size()));
  EXPECT_FALSE(missing_done.finish());
}

TEST(RosbagBinaryDecoder, RejectsUnknownVersionAndOversizeFrame)
{
  PJ::PlotDataMapRef data1;
  PJ::ROSBag::RosbagBinaryDecoder bad_version(data1);
  const Bytes version = header(2);
  EXPECT_FALSE(bad_version.append(version.data(), version.size()));

  PJ::PlotDataMapRef data2;
  PJ::ROSBag::RosbagBinaryDecoder oversize(data2);
  Bytes stream = header();
  stream.push_back(1);
  u32(stream, 64U * 1024U * 1024U + 1U);
  EXPECT_FALSE(oversize.append(stream.data(), stream.size()));
}

TEST(RosbagBinaryDecoder, RejectsUnknownAndDuplicateSeries)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagBinaryDecoder unknown(data);
  Bytes stream = header();
  frame(stream, 2, numeric(99, {}, {}));
  EXPECT_FALSE(unknown.append(stream.data(), stream.size()));

  PJ::PlotDataMapRef duplicate_data;
  PJ::ROSBag::RosbagBinaryDecoder duplicate(duplicate_data);
  stream = header();
  frame(stream, 1, series(1, 1, "/one"));
  frame(stream, 1, series(1, 2, "/two"));
  EXPECT_FALSE(duplicate.append(stream.data(), stream.size()));
}

TEST(RosbagBinaryDecoder, AllowsSameNameForDifferentSeriesKinds)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagBinaryDecoder decoder(data);
  Bytes stream = header();
  frame(stream, 1, series(1, 1, "/changing/data"));
  frame(stream, 1, series(2, 2, "/changing/data"));
  frame(stream, 4, done(0, 0));
  ASSERT_TRUE(decoder.append(stream.data(), stream.size()));
  ASSERT_TRUE(decoder.finish());
  EXPECT_EQ(data.numeric.count("/changing/data"), 1u);
  EXPECT_EQ(data.strings.count("/changing/data"), 1u);

  PJ::PlotDataMapRef duplicate_data;
  PJ::ROSBag::RosbagBinaryDecoder duplicate(duplicate_data);
  stream = header();
  frame(stream, 1, series(1, 1, "/duplicate"));
  frame(stream, 1, series(2, 1, "/duplicate"));
  EXPECT_FALSE(duplicate.append(stream.data(), stream.size()));
}

TEST(RosbagBinaryDecoder, RejectsUnknownFrameAndSeriesKinds)
{
  PJ::PlotDataMapRef data1;
  PJ::ROSBag::RosbagBinaryDecoder unknown_type(data1);
  Bytes stream = header();
  frame(stream, 27, {});
  EXPECT_FALSE(unknown_type.append(stream.data(), stream.size()));

  PJ::PlotDataMapRef data2;
  PJ::ROSBag::RosbagBinaryDecoder unknown_kind(data2);
  stream = header();
  frame(stream, 1, series(1, 3, "/bad"));
  EXPECT_FALSE(unknown_kind.append(stream.data(), stream.size()));
}

TEST(RosbagBinaryDecoder, RejectsEmptyOversizeAndInvalidUtf8Names)
{
  PJ::PlotDataMapRef empty_data;
  PJ::ROSBag::RosbagBinaryDecoder empty(empty_data);
  Bytes stream = header();
  frame(stream, 1, series(1, 1, ""));
  EXPECT_FALSE(empty.append(stream.data(), stream.size()));

  PJ::PlotDataMapRef oversize_data;
  PJ::ROSBag::RosbagBinaryDecoder oversize(oversize_data);
  stream = header();
  Bytes payload;
  u32(payload, 1);
  payload.push_back(1);
  u32(payload, 1024U * 1024U + 1U);
  frame(stream, 1, payload);
  EXPECT_FALSE(oversize.append(stream.data(), stream.size()));

  PJ::PlotDataMapRef utf8_data;
  PJ::ROSBag::RosbagBinaryDecoder utf8(utf8_data);
  stream = header();
  const std::string invalid_name("\xC0\xAF", 2);
  frame(stream, 1, series(1, 1, invalid_name));
  EXPECT_FALSE(utf8.append(stream.data(), stream.size()));

  PJ::PlotDataMapRef null_data;
  PJ::ROSBag::RosbagBinaryDecoder null_name(null_data);
  stream = header();
  frame(stream, 1, series(1, 1, std::string("/bad\0name", 9)));
  EXPECT_FALSE(null_name.append(stream.data(), stream.size()));
}

TEST(RosbagBinaryDecoder, RejectsNonFiniteNumbersWithoutWritingFrame)
{
  for (const double invalid :
       { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
         -std::numeric_limits<double>::infinity() })
  {
    PJ::PlotDataMapRef data;
    PJ::ROSBag::RosbagBinaryDecoder decoder(data);
    Bytes stream = header();
    frame(stream, 1, series(1, 1, "/number"));
    frame(stream, 2, numeric(1, { 1, 2 }, { 3.0, invalid }));
    EXPECT_FALSE(decoder.append(stream.data(), stream.size()));
    EXPECT_EQ(data.numeric.at("/number").size(), 0u);
    EXPECT_EQ(decoder.recordCount(), 0u);
  }
}

TEST(RosbagBinaryDecoder, RejectsInvalidUtf8StringsAndCountMismatch)
{
  PJ::PlotDataMapRef utf8_data;
  PJ::ROSBag::RosbagBinaryDecoder utf8(utf8_data);
  Bytes stream = header();
  frame(stream, 1, series(1, 2, "/text"));
  const std::string invalid_value("\xED\xA0\x80", 3);
  frame(stream, 3, strings(1, { { 1, "valid" }, { 2, invalid_value } }));
  EXPECT_FALSE(utf8.append(stream.data(), stream.size()));
  EXPECT_EQ(utf8_data.strings.at("/text").size(), 0u);

  PJ::PlotDataMapRef mismatch_data;
  PJ::ROSBag::RosbagBinaryDecoder mismatch(mismatch_data);
  stream = header();
  frame(stream, 1, series(2, 1, "/number"));
  Bytes bad_numeric = numeric(2, { 1 }, { 2.0 });
  bad_numeric.pop_back();
  frame(stream, 2, bad_numeric);
  EXPECT_FALSE(mismatch.append(stream.data(), stream.size()));
}

TEST(RosbagBinaryDecoder, RequiresDoneCountMatchAndRejectsTrailingData)
{
  PJ::PlotDataMapRef count_data;
  PJ::ROSBag::RosbagBinaryDecoder count_decoder(count_data);
  Bytes stream = header();
  frame(stream, 4, done(5, 1));
  EXPECT_FALSE(count_decoder.append(stream.data(), stream.size()));

  PJ::PlotDataMapRef trailing_data;
  PJ::ROSBag::RosbagBinaryDecoder trailing(trailing_data);
  stream = header();
  frame(stream, 4, done(5, 0));
  stream.push_back(0);
  EXPECT_FALSE(trailing.append(stream.data(), stream.size()));
}

}  // namespace

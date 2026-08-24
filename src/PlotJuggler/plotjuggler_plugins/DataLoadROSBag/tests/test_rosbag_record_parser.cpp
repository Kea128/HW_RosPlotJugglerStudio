#include <gtest/gtest.h>

#include "rosbag_record_parser.h"

namespace
{

TEST(RosbagRecordParser, ParsesAndCachesNumericSeries)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RecordParser parser(data);

  EXPECT_TRUE(parser.parse("N\t1500000000\t/vehicle/speed\t12.5"));
  EXPECT_TRUE(parser.parse("N\t2500000000\t/vehicle/speed\t13.75"));

  ASSERT_EQ(parser.recordCount(), 2u);
  ASSERT_EQ(data.numeric.size(), 1u);
  const auto& series = data.numeric.at("/vehicle/speed");
  ASSERT_EQ(series.size(), 2u);
  EXPECT_DOUBLE_EQ(series.at(0).x, 1.5);
  EXPECT_DOUBLE_EQ(series.at(0).y, 12.5);
  EXPECT_DOUBLE_EQ(series.at(1).x, 2.5);
  EXPECT_DOUBLE_EQ(series.at(1).y, 13.75);
}

TEST(RosbagRecordParser, DecodesNamesAndStringValues)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RecordParser parser(data);

  ASSERT_TRUE(parser.parse("S\t42\t/topic/label%20text\taGVsbG8="));
  ASSERT_EQ(data.strings.count("/topic/label text"), 1u);
  const auto& series = data.strings.at("/topic/label text");
  ASSERT_EQ(series.size(), 1u);
  EXPECT_EQ(series.getString(series.at(0).y), "hello");
}

TEST(RosbagRecordParser, IgnoresMetadataRecords)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RecordParser parser(data);

  EXPECT_FALSE(parser.parse("INFO\t2\t100"));
  EXPECT_FALSE(parser.parse("PROGRESS\t50\t100\t400"));
  EXPECT_FALSE(parser.parse("DONE\t100\t800"));
  EXPECT_FALSE(parser.parse("unrecognized output"));
  EXPECT_TRUE(data.numeric.empty());
  EXPECT_TRUE(data.strings.empty());
}

TEST(RosbagRecordParser, RejectsInvalidRecordValuesWithoutCreatingSeries)
{
  PJ::PlotDataMapRef data;
  PJ::ROSBag::RecordParser parser(data);

  EXPECT_FALSE(parser.parse("N\tnot-a-time\t/topic/value\t1"));
  EXPECT_FALSE(parser.parse("N\t100\t/topic/value\tnot-a-number"));
  EXPECT_FALSE(parser.parse("N\t100\t/topic/value\tnan"));
  EXPECT_FALSE(parser.parse("S\t100\t/topic/value\t%%%"));
  EXPECT_EQ(parser.recordCount(), 0u);
  EXPECT_TRUE(data.numeric.empty());
  EXPECT_TRUE(data.strings.empty());
}

}  // namespace
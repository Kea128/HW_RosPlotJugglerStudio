#include <gtest/gtest.h>

#include <QApplication>
#include <QHeaderView>
#include <QSettings>
#include <QTableWidget>

#include <mcap/types.hpp>

#include "dialog_mcap.h"

namespace
{
mcap::SchemaPtr schema(mcap::SchemaId id, const std::string& name)
{
  auto value = std::make_shared<mcap::Schema>(name, "ros2msg", "");
  value->id = id;
  return value;
}

mcap::ChannelPtr channel(mcap::ChannelId id, mcap::SchemaId schema_id,
                         const std::string& topic)
{
  auto value = std::make_shared<mcap::Channel>(topic, "cdr", schema_id);
  value->id = id;
  return value;
}

TEST(DialogMCAP, KeepsTopicsSelectableWhenStatisticsAreMissing)
{
  const std::unordered_map<int, mcap::SchemaPtr> schemas = {
    { 1, schema(1, "pkg/msg/One") }, { 2, schema(2, "pkg/msg/Two") }
  };
  const std::unordered_map<int, mcap::ChannelPtr> channels = {
    { 1, channel(1, 1, "/one") }, { 2, channel(2, 2, "/two") }
  };

  DialogMCAP dialog(channels, schemas, {}, std::nullopt);
  auto* table = dialog.findChild<QTableWidget*>();
  ASSERT_NE(table, nullptr);
  EXPECT_TRUE(table->horizontalHeader()->isSectionHidden(3));
  ASSERT_EQ(table->rowCount(), 2);
  EXPECT_EQ(table->selectionModel()->selectedRows().size(), 2);
  for (int row = 0; row < table->rowCount(); ++row)
  {
    EXPECT_TRUE(table->item(row, 0)->flags().testFlag(Qt::ItemIsSelectable));
    EXPECT_EQ(table->item(row, 3)->text(), "Unknown");
  }
}

TEST(DialogMCAP, PreservesRowMetadataWhileSortingAndHandlesLargeCounts)
{
  const std::unordered_map<int, mcap::SchemaPtr> schemas = {
    { 1, schema(1, "pkg/msg/Alpha") }, { 2, schema(2, "pkg/msg/Beta") }
  };
  const std::unordered_map<int, mcap::ChannelPtr> channels = {
    { 2, channel(2, 2, "/beta") }, { 1, channel(1, 1, "/alpha") }
  };
  const std::unordered_map<uint16_t, uint64_t> counts = {
    { 1, 5'000'000'000ULL }, { 2, 7ULL }
  };

  DialogMCAP dialog(channels, schemas, counts, std::nullopt);
  auto* table = dialog.findChild<QTableWidget*>();
  ASSERT_NE(table, nullptr);
  ASSERT_EQ(table->rowCount(), 2);

  EXPECT_EQ(table->item(0, 0)->text(), "/alpha");
  EXPECT_EQ(table->item(0, 1)->text(), "pkg/msg/Alpha");
  EXPECT_EQ(table->item(0, 3)->text(), "5000000000");
  EXPECT_EQ(table->item(1, 0)->text(), "/beta");
  EXPECT_EQ(table->item(1, 1)->text(), "pkg/msg/Beta");
  EXPECT_EQ(table->item(1, 3)->text(), "7");

  table->sortItems(3, Qt::AscendingOrder);
  EXPECT_EQ(table->item(0, 0)->text(), "/beta");
  EXPECT_EQ(table->item(1, 0)->text(), "/alpha");
  table->sortItems(3, Qt::DescendingOrder);
  EXPECT_EQ(table->item(0, 0)->text(), "/alpha");
  EXPECT_EQ(table->item(1, 0)->text(), "/beta");
}
}  // namespace

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("RosPlotJugglerTests"));
  QCoreApplication::setApplicationName(QStringLiteral("DialogMCAP"));
  QSettings().clear();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

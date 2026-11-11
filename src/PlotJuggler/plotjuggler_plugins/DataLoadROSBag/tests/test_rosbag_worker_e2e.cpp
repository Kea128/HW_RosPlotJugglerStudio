#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "ros_parser.h"
#include "rosbag_binary_decoder.h"
#include "rosbag_raw_decoder.h"
#include "rosbag_record_parser.h"

namespace
{

bool pythonImportsNumpy(const QString& python)
{
  if (python.isEmpty() || !QFileInfo::exists(python))
  {
    return false;
  }
  QProcess process;
  process.start(python, { QStringLiteral("-c"), QStringLiteral("import numpy") });
  if (!process.waitForStarted(10000) || !process.waitForFinished(30000))
  {
    process.kill();
    process.waitForFinished(3000);
    return false;
  }
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QString pythonExecutable()
{
  QStringList candidates;
#ifdef ROSBAG_E2E_PYTHON
  candidates << QString::fromUtf8(ROSBAG_E2E_PYTHON);
#endif
  const QString configured = qEnvironmentVariable("RSPJ_PYTHON");
  if (!configured.isEmpty())
  {
    candidates << configured;
  }
  for (const QString& name : { QStringLiteral("python3"), QStringLiteral("python") })
  {
    const QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty())
    {
      candidates << found;
    }
  }

  QString fallback;
  QStringList seen;
  for (const QString& candidate : candidates)
  {
    const QString resolved = QFileInfo(candidate).absoluteFilePath();
    if (resolved.isEmpty() || seen.contains(resolved) || !QFileInfo::exists(resolved))
    {
      continue;
    }
    seen << resolved;
    if (fallback.isEmpty())
    {
      fallback = resolved;
    }
    if (pythonImportsNumpy(resolved))
    {
      return resolved;
    }
  }
  return fallback;
}

QProcessEnvironment workerEnvironment()
{
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  const QString runtime = QFileInfo(QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE)).absolutePath();
  const QString existing = environment.value(QStringLiteral("PYTHONPATH"));
  environment.insert(QStringLiteral("PYTHONPATH"),
                     existing.isEmpty() ? runtime
                                        : runtime + QDir::listSeparator() + existing);
  environment.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
  return environment;
}

bool runPython(const QString& python, const QStringList& arguments, QByteArray& output,
               QByteArray& diagnostics)
{
  QProcess process;
  process.setProcessEnvironment(workerEnvironment());
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(python, arguments);
  if (!process.waitForStarted(10000) || !process.waitForFinished(120000))
  {
    process.kill();
    process.waitForFinished(3000);
    diagnostics = process.errorString().toUtf8();
    return false;
  }
  output = process.readAllStandardOutput();
  diagnostics = process.readAllStandardError();
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

TEST(RosbagWorkerE2E, InspectsFiltersAndStreamsBinary)
{
  const QString python = pythonExecutable();
  if (python.isEmpty())
  {
    GTEST_SKIP() << "Python is not available";
  }

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString bag = temporary.filePath(QStringLiteral("sample.bag"));
  QByteArray output;
  QByteArray diagnostics;
  ASSERT_TRUE(runPython(
      python,
      { QString::fromUtf8(ROSBAG_GENERATOR_SOURCE), QStringLiteral("--output"), bag,
        QStringLiteral("--messages"), QStringLiteral("50"),
        QStringLiteral("--numeric-topics"), QStringLiteral("4"),
        QStringLiteral("--string-every"), QStringLiteral("10"),
        QStringLiteral("--array-width"), QStringLiteral("8") },
      output, diagnostics))
      << diagnostics.constData();

  EXPECT_FALSE(runPython(
      python, { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), bag,
                QStringLiteral("--max-array"), QStringLiteral("-1") },
      output, diagnostics));
  EXPECT_TRUE(diagnostics.contains("must be between 0 and 100000"));
  EXPECT_FALSE(runPython(
      python, { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), bag,
                QStringLiteral("--max-array"), QStringLiteral("100001") },
      output, diagnostics));
  EXPECT_TRUE(diagnostics.contains("must be between 0 and 100000"));

  ASSERT_TRUE(runPython(
      python, { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), bag,
                QStringLiteral("--inspect") },
      output, diagnostics))
      << diagnostics.constData();
  const QJsonDocument index = QJsonDocument::fromJson(output);
  ASSERT_TRUE(index.isObject());
  EXPECT_EQ(index.object().value(QStringLiteral("version")).toInt(), 1);
  EXPECT_EQ(index.object().value(QStringLiteral("topics")).toArray().size(), 6);

  const QString topics_path = temporary.filePath(QStringLiteral("topics.json"));
  QFile topics_file(topics_path);
  ASSERT_TRUE(topics_file.open(QFile::WriteOnly | QFile::Truncate));
  ASSERT_EQ(topics_file.write("[\"/vehicle/speed\"]"), 18);
  topics_file.close();

  ASSERT_TRUE(runPython(
      python,
      { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), bag,
        QStringLiteral("--protocol"), QStringLiteral("binary-v1"),
        QStringLiteral("--topics-file"), topics_path },
      output, diagnostics))
      << diagnostics.constData();

  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagBinaryDecoder decoder(data);
  ASSERT_TRUE(decoder.append(output));
  ASSERT_TRUE(decoder.finish());
  EXPECT_TRUE(decoder.done());
  EXPECT_EQ(decoder.messages(), 50u);
  EXPECT_EQ(decoder.recordCount(), 50u);
  ASSERT_EQ(data.numeric.count("/vehicle/speed/data"), 1u);
  EXPECT_EQ(data.numeric.at("/vehicle/speed/data").size(), 50u);
  EXPECT_EQ(data.numeric.size(), 1u);
  EXPECT_TRUE(data.strings.empty());

  ASSERT_TRUE(runPython(
      python,
      { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), bag,
        QStringLiteral("--protocol"), QStringLiteral("text"),
        QStringLiteral("--topics-file"), topics_path },
      output, diagnostics))
      << diagnostics.constData();
  PJ::PlotDataMapRef legacy_data;
  PJ::ROSBag::RecordParser legacy_parser(legacy_data);
  for (const QByteArray& line : output.split('\n'))
  {
    if (line.startsWith("N\t") || line.startsWith("S\t"))
    {
      ASSERT_TRUE(legacy_parser.parse(line));
    }
  }
  EXPECT_EQ(legacy_parser.recordCount(), 50u);
  EXPECT_EQ(legacy_data.numeric.at("/vehicle/speed/data").size(), 50u);
}

TEST(RosbagWorkerE2E, ProducesEquivalentDataForAllSupportedFormats)
{
  const QString python = pythonExecutable();
  if (python.isEmpty())
  {
    GTEST_SKIP() << "Python is not available";
  }

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QStringList formats = { QStringLiteral("ros1"), QStringLiteral("ros2-sqlite"),
                                QStringLiteral("ros2-mcap") };
  for (const QString& format : formats)
  {
    const QString output_path =
        temporary.filePath(format == QStringLiteral("ros1")
                               ? QStringLiteral("fixture.bag")
                               : QStringLiteral("fixture-") + format);
    QByteArray output;
    QByteArray diagnostics;
    ASSERT_TRUE(runPython(
        python,
        { QString::fromUtf8(ROSBAG_BENCHMARK_GENERATOR_SOURCE),
          QStringLiteral("--format"), format, QStringLiteral("--output"), output_path,
          QStringLiteral("--messages"), QStringLiteral("12"), QStringLiteral("--topics"),
          QStringLiteral("3"), QStringLiteral("--array-width"), QStringLiteral("5"),
          QStringLiteral("--empty-string-ratio"), QStringLiteral("0.25") },
        output, diagnostics))
        << format.toStdString() << ": " << diagnostics.constData();

    QString worker_input = output_path;
    if (format != QStringLiteral("ros1"))
    {
      const QString pattern = format == QStringLiteral("ros2-sqlite")
                                  ? QStringLiteral("*.db3")
                                  : QStringLiteral("*.mcap");
      const QStringList storage_files =
          QDir(output_path).entryList({ pattern }, QDir::Files);
      ASSERT_EQ(storage_files.size(), 1);
      worker_input = QDir(output_path).filePath(storage_files.front());
    }
    ASSERT_TRUE(runPython(
        python, { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), worker_input,
                  QStringLiteral("--protocol"), QStringLiteral("binary-v1") },
        output, diagnostics))
        << format.toStdString() << ": " << diagnostics.constData();

    PJ::PlotDataMapRef data;
    PJ::ROSBag::RosbagBinaryDecoder decoder(data);
    ASSERT_TRUE(decoder.append(output)) << format.toStdString();
    ASSERT_TRUE(decoder.finish()) << format.toStdString();
    EXPECT_EQ(decoder.messages(), 48u) << format.toStdString();
    EXPECT_EQ(decoder.recordCount(), 336u) << format.toStdString();
    EXPECT_EQ(data.numeric.size(), 24u) << format.toStdString();
    EXPECT_EQ(data.strings.size(), 4u) << format.toStdString();
    ASSERT_EQ(data.numeric.count("/benchmark/numeric_000/data[0]"), 1u);
    EXPECT_EQ(data.numeric.at("/benchmark/numeric_000/data[0]").size(), 12u);
    ASSERT_EQ(data.strings.count("/benchmark/text/data"), 1u);
    const auto& text = data.strings.at("/benchmark/text/data");
    ASSERT_EQ(text.size(), 12u);
    size_t empty_strings = 0;
    for (const auto& point : text)
    {
      empty_strings += text.getString(point.y).empty() ? 1u : 0u;
    }
    EXPECT_EQ(empty_strings, 3u);
  }
}

TEST(RosbagWorkerE2E, StreamsRawRos1MessagesForCppParser)
{
  const QString python = pythonExecutable();
  if (python.isEmpty())
  {
    GTEST_SKIP() << "Python is not available";
  }

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString bag = temporary.filePath(QStringLiteral("raw.bag"));
  QByteArray output;
  QByteArray diagnostics;
  ASSERT_TRUE(runPython(
      python,
      { QString::fromUtf8(ROSBAG_BENCHMARK_GENERATOR_SOURCE), QStringLiteral("--format"),
        QStringLiteral("ros1"), QStringLiteral("--output"), bag,
        QStringLiteral("--messages"), QStringLiteral("12"), QStringLiteral("--topics"),
        QStringLiteral("3"), QStringLiteral("--array-width"), QStringLiteral("5"),
        QStringLiteral("--empty-string-ratio"), QStringLiteral("0.25") },
      output, diagnostics))
      << diagnostics.constData();

  ASSERT_TRUE(runPython(
      python, { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), bag,
                QStringLiteral("--inspect") },
      output, diagnostics))
      << diagnostics.constData();
  const QJsonDocument index = QJsonDocument::fromJson(output);
  ASSERT_TRUE(index.isObject());
  EXPECT_EQ(index.object().value(QStringLiteral("version")).toInt(), 1);
  EXPECT_FALSE(index.object().value(QStringLiteral("topics")).toArray().isEmpty());
  EXPECT_FALSE(index.object()
                   .value(QStringLiteral("topics"))
                   .toArray()
                   .at(0)
                   .toObject()
                   .value(QStringLiteral("schema"))
                   .toString()
                   .isEmpty());

  ASSERT_TRUE(runPython(
      python, { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), bag,
                QStringLiteral("--protocol"), QStringLiteral("raw-v1") },
      output, diagnostics))
      << diagnostics.constData();

  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [&data](const std::string& topic, const std::string& type, const std::string& schema) {
        auto parser = std::make_shared<ParserROS>(
            topic, type, schema, new RosMsgParser::ROS_Deserializer(), data);
        parser->enableTruncationCheck(false);
        return parser;
      },
      5);
  ASSERT_TRUE(decoder.append(output));
  ASSERT_TRUE(decoder.finish());
  EXPECT_EQ(decoder.messages(), 48u);
  EXPECT_TRUE(decoder.failedTopics().isEmpty());
  ASSERT_EQ(data.numeric.count("/benchmark/numeric_000/data[0]"), 1u);
  EXPECT_EQ(data.numeric.at("/benchmark/numeric_000/data[0]").size(), 12u);
  ASSERT_EQ(data.strings.count("/benchmark/text/data"), 1u);
  EXPECT_EQ(data.strings.at("/benchmark/text/data").size(), 12u);

  ASSERT_TRUE(runPython(
      python, { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), bag,
                QStringLiteral("--protocol"), QStringLiteral("binary-v1"),
                QStringLiteral("--max-array"), QStringLiteral("5") },
      output, diagnostics))
      << diagnostics.constData();
  PJ::PlotDataMapRef binary_data;
  PJ::ROSBag::RosbagBinaryDecoder binary_decoder(binary_data);
  ASSERT_TRUE(binary_decoder.append(output));
  ASSERT_TRUE(binary_decoder.finish());
  ASSERT_EQ(binary_data.numeric.count("/benchmark/numeric_000/data[0]"), 1u);
  EXPECT_EQ(binary_data.numeric.at("/benchmark/numeric_000/data[0]").size(),
            data.numeric.at("/benchmark/numeric_000/data[0]").size());
  for (size_t index = 0; index < data.numeric.at("/benchmark/numeric_000/data[0]").size(); ++index)
  {
    EXPECT_DOUBLE_EQ(data.numeric.at("/benchmark/numeric_000/data[0]").at(index).y,
                     binary_data.numeric.at("/benchmark/numeric_000/data[0]").at(index).y);
  }
  ASSERT_EQ(binary_data.strings.count("/benchmark/text/data"), 1u);
  EXPECT_EQ(binary_data.strings.at("/benchmark/text/data").size(),
            data.strings.at("/benchmark/text/data").size());
}

TEST(RosbagWorkerE2E, StreamsRawRos2MessagesForCppParser)
{
  const QString python = pythonExecutable();
  if (python.isEmpty())
  {
    GTEST_SKIP() << "Python is not available";
  }

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  for (const QString& format : { QStringLiteral("ros2-sqlite"), QStringLiteral("ros2-mcap") })
  {
    const QString bag_dir = temporary.filePath(QStringLiteral("raw-") + format);
    QByteArray output;
    QByteArray diagnostics;
    ASSERT_TRUE(runPython(
        python,
        { QString::fromUtf8(ROSBAG_BENCHMARK_GENERATOR_SOURCE), QStringLiteral("--format"),
          format, QStringLiteral("--output"), bag_dir, QStringLiteral("--messages"),
          QStringLiteral("12"), QStringLiteral("--topics"), QStringLiteral("3"),
          QStringLiteral("--array-width"), QStringLiteral("5"),
          QStringLiteral("--empty-string-ratio"), QStringLiteral("0.25") },
        output, diagnostics))
        << format.toStdString() << ": " << diagnostics.constData();
    const QStringList storage_files = QDir(bag_dir).entryList(
        { format == QStringLiteral("ros2-sqlite") ? QStringLiteral("*.db3")
                                                  : QStringLiteral("*.mcap") },
        QDir::Files);
    ASSERT_EQ(storage_files.size(), 1) << format.toStdString();
    const QString storage = QDir(bag_dir).filePath(storage_files.front());

    ASSERT_TRUE(runPython(python,
                          { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), storage,
                            QStringLiteral("--protocol"), QStringLiteral("raw-v1"),
                            QStringLiteral("--max-array"), QStringLiteral("5") },
                          output, diagnostics))
        << format.toStdString() << ": " << diagnostics.constData();
    PJ::PlotDataMapRef data;
    PJ::ROSBag::RosbagRawDecoder decoder(
        data,
        [&data](const std::string& topic, const std::string& type, const std::string& schema) {
          const std::string msg_type =
              QString::fromStdString(type).replace(QStringLiteral("/msg/"), QStringLiteral("/"))
                  .toStdString();
          auto parser = std::make_shared<ParserROS>(
              topic, msg_type, schema, new RosMsgParser::ROS2_Deserializer(), data);
          parser->enableTruncationCheck(false);
          return parser;
        },
        5);
    ASSERT_TRUE(decoder.append(output)) << format.toStdString();
    ASSERT_TRUE(decoder.finish()) << format.toStdString();
    EXPECT_EQ(decoder.messages(), 48u) << format.toStdString();
    EXPECT_TRUE(decoder.failedTopics().isEmpty()) << format.toStdString();
    EXPECT_TRUE(decoder.warnings().isEmpty()) << format.toStdString();

    ASSERT_TRUE(runPython(python,
                          { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE), storage,
                            QStringLiteral("--protocol"), QStringLiteral("binary-v1"),
                            QStringLiteral("--max-array"), QStringLiteral("5") },
                          output, diagnostics))
        << format.toStdString() << ": " << diagnostics.constData();
    PJ::PlotDataMapRef binary_data;
    PJ::ROSBag::RosbagBinaryDecoder binary_decoder(binary_data);
    ASSERT_TRUE(binary_decoder.append(output)) << format.toStdString();
    ASSERT_TRUE(binary_decoder.finish()) << format.toStdString();

    for (const char* series_name :
         { "/benchmark/numeric_000/data[0]", "/benchmark/numeric_002/data[4]" })
    {
      ASSERT_EQ(data.numeric.count(series_name), 1u) << format.toStdString() << series_name;
      ASSERT_EQ(binary_data.numeric.count(series_name), 1u) << format.toStdString();
      const auto& raw_series = data.numeric.at(series_name);
      const auto& binary_series = binary_data.numeric.at(series_name);
      ASSERT_EQ(raw_series.size(), 12u) << format.toStdString() << series_name;
      ASSERT_EQ(binary_series.size(), raw_series.size()) << format.toStdString();
      for (size_t index = 0; index < raw_series.size(); ++index)
      {
        EXPECT_DOUBLE_EQ(raw_series.at(index).x, binary_series.at(index).x);
        EXPECT_DOUBLE_EQ(raw_series.at(index).y, binary_series.at(index).y);
      }
    }
    ASSERT_EQ(data.strings.count("/benchmark/text/data"), 1u) << format.toStdString();
    EXPECT_EQ(data.strings.at("/benchmark/text/data").size(), 12u) << format.toStdString();
  }
}

TEST(RosbagWorkerE2E, DecodesRos2FixtureWithNestedArraysFromRawStream)
{
  const QString python = pythonExecutable();
  if (python.isEmpty())
  {
    GTEST_SKIP() << "Python is not available";
  }
  QByteArray output;
  QByteArray diagnostics;
  ASSERT_TRUE(runPython(python,
                        { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE),
                          QString::fromUtf8(ROSBAG_ROS2_FIXTURE), QStringLiteral("--protocol"),
                          QStringLiteral("raw-v1") },
                        output, diagnostics))
      << diagnostics.constData();

  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [&data](const std::string& topic, const std::string& type, const std::string& schema) {
        const std::string msg_type =
            QString::fromStdString(type).replace(QStringLiteral("/msg/"), QStringLiteral("/"))
                .toStdString();
        auto parser = std::make_shared<ParserROS>(topic, msg_type, schema,
                                                  new RosMsgParser::ROS2_Deserializer(), data);
        parser->enableTruncationCheck(false);
        return parser;
      },
      100);
  ASSERT_TRUE(decoder.append(output));
  ASSERT_TRUE(decoder.finish());
  EXPECT_EQ(decoder.messages(), 7u);
  EXPECT_TRUE(decoder.failedTopics().isEmpty());
  EXPECT_TRUE(decoder.warnings().isEmpty()) << decoder.warnings().join('\n').toStdString();
  // test_msgs/Arrays nests test_msgs/Constants, a message without fields, before more fields.
  ASSERT_EQ(data.numeric.count("/array_topic/alignment_check"), 1u);
  ASSERT_EQ(data.strings.count("/array_topic/string_values_default[2]"), 1u);

  ASSERT_TRUE(runPython(python,
                        { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE),
                          QString::fromUtf8(ROSBAG_ROS2_FIXTURE), QStringLiteral("--protocol"),
                          QStringLiteral("binary-v1") },
                        output, diagnostics))
      << diagnostics.constData();
  PJ::PlotDataMapRef binary_data;
  PJ::ROSBag::RosbagBinaryDecoder binary_decoder(binary_data);
  ASSERT_TRUE(binary_decoder.append(output));
  ASSERT_TRUE(binary_decoder.finish());

  // The Python flattener also emits constants and rosidl's placeholder member of
  // test_msgs/Constants; ParserROS skips both.
  size_t binary_only = 0;
  for (const auto& [name, series] : binary_data.numeric)
  {
    if (data.numeric.count(name) == 0)
    {
      EXPECT_NE(name.find("/array_topic/constants_values["), std::string::npos) << name;
      binary_only++;
    }
  }
  EXPECT_EQ(binary_only, 3u * 14u);
  for (const auto& [name, raw_series] : data.numeric)
  {
    ASSERT_EQ(binary_data.numeric.count(name), 1u) << name;
    const auto& binary_series = binary_data.numeric.at(name);
    ASSERT_EQ(raw_series.size(), binary_series.size()) << name;
    // rosbags unpacks ROS 2 byte, an unsigned octet, as int8.
    const bool is_byte = name.find("byte_value") != std::string::npos;
    for (size_t index = 0; index < raw_series.size(); ++index)
    {
      const double binary_y = binary_series.at(index).y;
      EXPECT_DOUBLE_EQ(raw_series.at(index).x, binary_series.at(index).x) << name;
      EXPECT_DOUBLE_EQ(raw_series.at(index).y,
                       is_byte && binary_y < 0 ? binary_y + 256.0 : binary_y)
          << name;
    }
  }
  ASSERT_EQ(data.strings.size(), binary_data.strings.size());
  for (const auto& [name, raw_series] : data.strings)
  {
    ASSERT_EQ(binary_data.strings.count(name), 1u) << name;
    const auto& binary_series = binary_data.strings.at(name);
    ASSERT_EQ(raw_series.size(), binary_series.size()) << name;
    for (size_t index = 0; index < raw_series.size(); ++index)
    {
      EXPECT_EQ(raw_series.getString(raw_series.at(index).y),
                binary_series.getString(binary_series.at(index).y))
          << name;
    }
  }
}

TEST(RosbagWorkerE2E, TopicRejectedByTruncationCheckIsReloadedWithoutLeftovers)
{
  const QString python = pythonExecutable();
  if (python.isEmpty())
  {
    GTEST_SKIP() << "Python is not available";
  }
  QByteArray output;
  QByteArray diagnostics;
  ASSERT_TRUE(runPython(python,
                        { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE),
                          QString::fromUtf8(ROSBAG_ROS2_FIXTURE), QStringLiteral("--protocol"),
                          QStringLiteral("raw-v1") },
                        output, diagnostics))
      << diagnostics.constData();

  PJ::PlotDataMapRef data;
  PJ::ROSBag::RosbagRawDecoder decoder(
      data,
      [&data](const std::string& topic, const std::string& type, const std::string& schema) {
        const std::string msg_type =
            QString::fromStdString(type).replace(QStringLiteral("/msg/"), QStringLiteral("/"))
                .toStdString();
        // The default preference: uint64 extremes in test_msgs/Arrays do not fit a double.
        auto parser = std::make_shared<ParserROS>(topic, msg_type, schema,
                                                  new RosMsgParser::ROS2_Deserializer(), data);
        parser->enableTruncationCheck(true);
        return parser;
      },
      100);
  ASSERT_TRUE(decoder.append(output));
  ASSERT_TRUE(decoder.finish());
  ASSERT_EQ(decoder.failedTopics(), QStringList{ "/array_topic" });
  EXPECT_TRUE(decoder.warnings().isEmpty());
  for (const auto& [name, series] : data.numeric)
  {
    EXPECT_EQ(name.rfind("/test_topic/", 0), 0u) << name;
  }
  EXPECT_TRUE(data.strings.empty());

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString topics_path = temporary.filePath(QStringLiteral("fallback.json"));
  QFile topics_file(topics_path);
  ASSERT_TRUE(topics_file.open(QFile::WriteOnly | QFile::Truncate));
  ASSERT_GT(topics_file.write("[\"/array_topic\"]"), 0);
  topics_file.close();
  ASSERT_TRUE(runPython(python,
                        { QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE),
                          QString::fromUtf8(ROSBAG_ROS2_FIXTURE), QStringLiteral("--protocol"),
                          QStringLiteral("binary-v1"), QStringLiteral("--topics-file"),
                          topics_path },
                        output, diagnostics))
      << diagnostics.constData();
  PJ::ROSBag::RosbagBinaryDecoder fallback(data);
  ASSERT_TRUE(fallback.append(output));
  ASSERT_TRUE(fallback.finish());
  ASSERT_EQ(data.numeric.count("/array_topic/bool_values[0]"), 1u);
  for (const auto& [name, series] : data.numeric)
  {
    EXPECT_EQ(series.size(), name.rfind("/array_topic/", 0) == 0 ? 4u : 3u) << name;
  }
}

}  // namespace

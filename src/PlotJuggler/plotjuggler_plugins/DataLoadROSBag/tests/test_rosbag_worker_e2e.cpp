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

#include "rosbag_binary_decoder.h"
#include "rosbag_record_parser.h"

namespace
{

QString pythonExecutable()
{
  const QString configured = qEnvironmentVariable("RSPJ_PYTHON");
  if (!configured.isEmpty() && QFile::exists(configured))
  {
    return configured;
  }
  return QStandardPaths::findExecutable(QStringLiteral("python"));
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
  if (!process.waitForStarted(10000) || !process.waitForFinished(30000))
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

}  // namespace

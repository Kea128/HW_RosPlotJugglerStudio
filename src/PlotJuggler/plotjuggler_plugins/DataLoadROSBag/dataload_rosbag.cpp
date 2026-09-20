#include "dataload_rosbag.h"
#include "rosbag_binary_decoder.h"
#include "rosbag_raw_decoder.h"
#include "rosbag_record_parser.h"
#include "rosbag_topic_dialog.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <QtConcurrent>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace
{
constexpr qsizetype MAX_PENDING_RECORD_BYTES = 16 * 1024 * 1024;
constexpr qsizetype MAX_INSPECT_BYTES = 16 * 1024 * 1024;

bool headlessRosbagLoad()
{
  return qEnvironmentVariableIsSet("RSPJ_ROSBAG_SELECT_ALL") ||
         qEnvironmentVariableIsSet("RSPJ_ROSBAG_EXIT_AFTER_LOAD");
}

struct InspectResult
{
  QByteArray output;
  QByteArray diagnostics;
  QString error;
  bool canceled = false;
};

struct LoadResult
{
  PJ::PlotDataMapRef data;
  QString error;
  QStringList warnings;
  QStringList metrics;
  std::atomic<qint64> current{ 0 };
  std::atomic<qint64> total{ 0 };
  std::atomic<bool> cancel_requested{ false };
  uint64_t info_messages = 0;
  uint64_t done_messages = 0;
  uint64_t done_emitted = 0;
  bool info_received = false;
  bool done = false;
  bool canceled = false;
};

QString findWorker()
{
  const QString application_dir = QCoreApplication::applicationDirPath();
  const QStringList candidates = {
    QDir(application_dir).filePath("runtime/rosbag_python/extract_rosbag.py"),
    QDir(application_dir).filePath("../runtime/rosbag_python/extract_rosbag.py"),
#ifdef ROSBAG_PYTHON_WORKER_SOURCE
    QString::fromUtf8(ROSBAG_PYTHON_WORKER_SOURCE),
#endif
  };
  for (const QString& candidate : candidates)
  {
    const QFileInfo info(candidate);
    if (info.isFile() && info.isReadable())
    {
      return info.canonicalFilePath();
    }
  }
  return {};
}

QString findPython()
{
  const QString configured = qEnvironmentVariable("RSPJ_PYTHON");
  if (!configured.isEmpty() && QFileInfo::exists(configured))
  {
    return configured;
  }
  const QString bundled =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("python.exe"));
  if (QFileInfo::exists(bundled))
  {
    return bundled;
  }
  for (const QString& name : { QStringLiteral("python3"), QStringLiteral("python") })
  {
    const QString executable = QStandardPaths::findExecutable(name);
    if (!executable.isEmpty())
    {
      return executable;
    }
  }
  return {};
}

QByteArray normalizedLine(QByteArray line)
{
  if (line.endsWith('\r'))
  {
    line.chop(1);
  }
  return line;
}

template <typename Callback>
void consumeLines(QByteArray& buffer, Callback callback)
{
  qsizetype newline = -1;
  while ((newline = buffer.indexOf('\n')) >= 0)
  {
    const QByteArray line = normalizedLine(buffer.left(newline));
    buffer.remove(0, newline + 1);
    if (!line.isEmpty())
    {
      callback(line);
    }
  }
}

void runInspectProcess(const QString& python, const QString& worker, const QString& bag,
                       const std::shared_ptr<InspectResult>& result,
                       const std::shared_ptr<std::atomic<bool>>& cancel_requested)
{
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(
      python, { QStringLiteral("-u"), worker, bag, QStringLiteral("--inspect") });
  if (!process.waitForStarted(10000))
  {
    result->error =
        QStringLiteral("Could not start Python worker: %1").arg(process.errorString());
    return;
  }

  while (process.state() != QProcess::NotRunning)
  {
    process.waitForReadyRead(20);
    result->output += process.readAllStandardOutput();
    result->diagnostics += process.readAllStandardError();
    if (result->output.size() > MAX_INSPECT_BYTES ||
        result->diagnostics.size() > MAX_INSPECT_BYTES)
    {
      result->error = QStringLiteral("ROS bag topic index exceeded the 16 MiB safety limit.");
      process.kill();
      process.waitForFinished(3000);
      break;
    }
    if (cancel_requested->load(std::memory_order_relaxed))
    {
      result->canceled = true;
      process.kill();
      process.waitForFinished(3000);
      break;
    }
  }

  result->output += process.readAllStandardOutput();
  result->diagnostics += process.readAllStandardError();
  if (!result->canceled && result->error.isEmpty() &&
      (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0))
  {
    result->error =
        QString::fromUtf8(result->diagnostics).trimmed().left(2000);
    if (result->error.isEmpty())
    {
      result->error =
          QStringLiteral("ROS bag worker exited with code %1.").arg(process.exitCode());
    }
  }
}

bool parseTopicIndex(const QByteArray& json, std::vector<PJ::ROSBag::TopicInfo>& topics,
                     QString& error)
{
  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject())
  {
    error = QStringLiteral("Invalid topic index from ROS bag worker: %1")
                .arg(parse_error.errorString());
    return false;
  }
  const QJsonObject root = document.object();
  if (root.value(QStringLiteral("version")).toInt() != 1 ||
      !root.value(QStringLiteral("topics")).isArray())
  {
    error = QStringLiteral("Unsupported ROS bag topic index.");
    return false;
  }

  const QJsonArray topic_array = root.value(QStringLiteral("topics")).toArray();
  topics.reserve(static_cast<size_t>(topic_array.size()));
  for (const QJsonValue& value : topic_array)
  {
    if (!value.isObject())
    {
      error = QStringLiteral("ROS bag topic index contains an invalid entry.");
      return false;
    }
    const QJsonObject object = value.toObject();
    PJ::ROSBag::TopicInfo topic;
    topic.name = object.value(QStringLiteral("name")).toString();
    topic.type = object.value(QStringLiteral("type")).toString();
    topic.messageCount =
        static_cast<qint64>(object.value(QStringLiteral("messageCount")).toDouble(-1));
    if (topic.name.isEmpty() || topic.type.isEmpty() || topic.messageCount < 0)
    {
      error = QStringLiteral("ROS bag topic index contains incomplete metadata.");
      return false;
    }
    topics.push_back(std::move(topic));
  }
  return true;
}

void consumeControlLine(const QByteArray& line, const std::shared_ptr<LoadResult>& result)
{
  const QList<QByteArray> fields = line.split('\t');
  if (fields.isEmpty())
  {
    return;
  }
  if (fields[0] == "INFO" && fields.size() >= 3)
  {
    bool ok = false;
    const qint64 total = fields[2].toLongLong(&ok);
    if (ok && total >= 0)
    {
      result->total.store(total, std::memory_order_relaxed);
      result->info_messages = static_cast<uint64_t>(total);
      result->info_received = true;
    }
  }
  else if (fields[0] == "PROGRESS" && fields.size() >= 4)
  {
    bool current_ok = false;
    bool total_ok = false;
    const qint64 current = fields[1].toLongLong(&current_ok);
    const qint64 total = fields[2].toLongLong(&total_ok);
    if (current_ok && total_ok && current >= 0 && total >= 0)
    {
      result->current.store(current, std::memory_order_relaxed);
      result->total.store(total, std::memory_order_relaxed);
    }
  }
  else if (fields[0] == "WARN")
  {
    result->warnings.push_back(QString::fromUtf8(line));
  }
  else if (fields[0] == "METRIC")
  {
    result->metrics.push_back(QString::fromUtf8(line));
  }
  else if (fields[0] == "ERROR")
  {
    result->error = QString::fromUtf8(line.mid(6)).trimmed();
  }
  else if (fields[0] == "DONE" && fields.size() >= 3)
  {
    bool messages_ok = false;
    bool emitted_ok = false;
    const qulonglong messages = fields[1].toULongLong(&messages_ok);
    const qulonglong emitted = fields[2].toULongLong(&emitted_ok);
    if (!messages_ok || !emitted_ok)
    {
      result->error = QStringLiteral("ROS bag worker returned an invalid DONE record.");
    }
    else
    {
      result->done_messages = static_cast<uint64_t>(messages);
      result->done_emitted = static_cast<uint64_t>(emitted);
      result->done = true;
    }
  }
}

QStringList workerArguments(const QString& worker, const QString& bag, const QString& protocol,
                            const QString& topics_file, int max_array)
{
  return { QStringLiteral("-u"),          worker,
           bag,                           QStringLiteral("--protocol"),
           protocol,                      QStringLiteral("--topics-file"),
           topics_file,                   QStringLiteral("--max-array"),
           QString::number(max_array) };
}

template <typename OnStdout>
void runWorkerProcess(const QString& python, const QStringList& arguments,
                      const std::shared_ptr<LoadResult>& result, OnStdout on_stdout,
                      bool line_buffered_stdout)
{
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(python, arguments);
  if (!process.waitForStarted(10000))
  {
    result->error =
        QStringLiteral("Could not start Python worker: %1").arg(process.errorString());
    return;
  }

  QByteArray stdout_buffer;
  QByteArray stderr_buffer;
  const auto consume_stderr = [&]() {
    consumeLines(stderr_buffer,
                 [&result](const QByteArray& line) { consumeControlLine(line, result); });
  };
  const auto stop = [&]() {
    process.kill();
    process.waitForFinished(3000);
  };

  while (process.state() != QProcess::NotRunning)
  {
    process.waitForReadyRead(20);
    if (line_buffered_stdout)
    {
      stdout_buffer += process.readAllStandardOutput();
      consumeLines(stdout_buffer, on_stdout);
    }
    else
    {
      on_stdout(process.readAllStandardOutput());
    }
    stderr_buffer += process.readAllStandardError();
    consume_stderr();
    if (stdout_buffer.size() > MAX_PENDING_RECORD_BYTES ||
        stderr_buffer.size() > MAX_PENDING_RECORD_BYTES)
    {
      result->error =
          QStringLiteral("ROS bag worker produced an oversized diagnostic record.");
    }
    if (result->cancel_requested.load(std::memory_order_relaxed))
    {
      result->canceled = true;
      stop();
      break;
    }
    if (!result->error.isEmpty())
    {
      stop();
      break;
    }
  }

  if (line_buffered_stdout)
  {
    stdout_buffer += process.readAllStandardOutput();
    consumeLines(stdout_buffer, on_stdout);
    if (!stdout_buffer.trimmed().isEmpty())
    {
      on_stdout(normalizedLine(stdout_buffer));
    }
  }
  else
  {
    on_stdout(process.readAllStandardOutput());
  }
  stderr_buffer += process.readAllStandardError();
  consume_stderr();
  if (!stderr_buffer.isEmpty())
  {
    consumeControlLine(normalizedLine(stderr_buffer), result);
  }

  if (result->canceled)
  {
    return;
  }
  if (result->error.isEmpty() &&
      (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0))
  {
    result->error = QStringLiteral("ROS bag worker exited with code %1: %2")
                        .arg(process.exitCode())
                        .arg(process.errorString());
  }
}

void runBinaryLoadProcess(const QString& python, const QString& worker, const QString& bag,
                          const QString& topics_file, int max_array,
                          const std::shared_ptr<LoadResult>& result)
{
  PJ::ROSBag::RosbagBinaryDecoder decoder(result->data);
  runWorkerProcess(
      python, workerArguments(worker, bag, QStringLiteral("binary-v1"), topics_file, max_array),
      result,
      [&](const QByteArray& data) {
        if (!data.isEmpty() && result->error.isEmpty() && !decoder.append(data))
        {
          result->error = QStringLiteral("Invalid binary data from ROS bag worker.");
        }
      },
      false);
  if (result->canceled || !result->error.isEmpty())
  {
    return;
  }
  if (!decoder.finish())
  {
    result->error = QStringLiteral("ROS bag worker returned an incomplete binary stream.");
    return;
  }
  if (!result->info_received || !result->done)
  {
    result->error =
        QStringLiteral("ROS bag worker returned incomplete binary control records.");
    return;
  }
  if (result->info_messages != result->done_messages ||
      result->done_messages != decoder.messages() ||
      result->done_emitted != static_cast<uint64_t>(decoder.recordCount()))
  {
    result->error =
        QStringLiteral("ROS bag worker binary control counts do not match decoded data.");
    return;
  }
  result->current.store(static_cast<qint64>(decoder.messages()), std::memory_order_relaxed);
}

void runRawLoadProcess(const QString& python, const QString& worker, const QString& bag,
                       const QString& topics_file, int max_array,
                       const PJ::ParserFactories* factories,
                       const std::shared_ptr<LoadResult>& result)
{
  auto create_parser = [factories, result](const std::string& topic, const std::string& type,
                                           const std::string& schema) -> PJ::MessageParserPtr {
    if (!factories)
    {
      return {};
    }
    const auto found = factories->find(QStringLiteral("ros1msg"));
    if (found == factories->end())
    {
      return {};
    }
    return found->second->createParser(topic, type, schema, result->data);
  };

  PJ::ROSBag::RosbagRawDecoder decoder(result->data, create_parser, max_array);
  runWorkerProcess(
      python, workerArguments(worker, bag, QStringLiteral("raw-v1"), topics_file, max_array),
      result,
      [&](const QByteArray& data) {
        if (!data.isEmpty() && result->error.isEmpty() && !decoder.append(data))
        {
          result->error = QStringLiteral("Invalid raw data from ROS bag worker.");
        }
      },
      false);
  result->warnings.append(decoder.warnings());
  if (result->canceled || !result->error.isEmpty())
  {
    return;
  }
  if (!decoder.finish())
  {
    result->error = QStringLiteral("ROS bag worker returned an incomplete raw stream.");
    return;
  }
  if (!result->info_received || !result->done)
  {
    result->error = QStringLiteral("ROS bag worker returned incomplete raw control records.");
    return;
  }
  if (result->info_messages != result->done_messages ||
      result->done_messages != decoder.messages() ||
      result->done_emitted != decoder.messages())
  {
    result->error =
        QStringLiteral("ROS bag worker raw control counts do not match decoded data.");
    return;
  }
  result->current.store(static_cast<qint64>(decoder.messages()), std::memory_order_relaxed);
  if (decoder.failedTopics().isEmpty())
  {
    return;
  }

  QTemporaryFile fallback_topics(
      QDir(QDir::tempPath()).filePath(QStringLiteral("rspj-raw-fallback-XXXXXX.json")));
  fallback_topics.setAutoRemove(true);
  if (!fallback_topics.open())
  {
    result->error = QStringLiteral("Could not create the raw-protocol fallback topic file.");
    return;
  }
  QJsonArray topics;
  for (const QString& topic : decoder.failedTopics())
  {
    topics.push_back(topic);
  }
  const QByteArray json = QJsonDocument(topics).toJson(QJsonDocument::Compact);
  if (fallback_topics.write(json) != json.size() || !fallback_topics.flush())
  {
    result->error = QStringLiteral("Could not write the raw-protocol fallback topic file.");
    return;
  }
  fallback_topics.close();
  result->warnings.push_back(
      QStringLiteral("WARN\traw-v1\tfalling back to binary-v1 for %1 topic(s)")
          .arg(decoder.failedTopics().size()));

  const uint64_t raw_info = result->info_messages;
  const uint64_t raw_done_messages = result->done_messages;
  const uint64_t raw_done_emitted = result->done_emitted;
  runBinaryLoadProcess(python, worker, bag, fallback_topics.fileName(), max_array, result);
  if (result->error.isEmpty())
  {
    result->info_messages = raw_info;
    result->done_messages = raw_done_messages;
    result->done_emitted = raw_done_emitted;
    result->info_received = true;
    result->done = true;
  }
}

void runTextLoadProcess(const QString& python, const QString& worker, const QString& bag,
                        const QString& topics_file, int max_array,
                        const std::shared_ptr<LoadResult>& result)
{
  PJ::ROSBag::RecordParser parser(result->data);
  runWorkerProcess(
      python, workerArguments(worker, bag, QStringLiteral("text"), topics_file, max_array),
      result,
      [&](const QByteArray& line) {
        if (line.startsWith("N\t") || line.startsWith("S\t"))
        {
          if (!parser.parse(line))
          {
            result->error = QStringLiteral("Invalid legacy data from ROS bag worker.");
          }
        }
        else
        {
          consumeControlLine(line, result);
        }
      },
      true);
  if (result->canceled || !result->error.isEmpty())
  {
    return;
  }
  if (!result->done)
  {
    result->error = QStringLiteral("Legacy ROS bag worker stopped without a DONE record.");
    return;
  }
  if (!result->info_received)
  {
    result->error = QStringLiteral("Legacy ROS bag worker stopped without an INFO record.");
    return;
  }
  if (result->done_messages != result->info_messages)
  {
    result->error =
        QStringLiteral("Legacy ROS bag worker DONE message count does not match INFO.");
    return;
  }
  if (result->done_emitted != static_cast<uint64_t>(parser.recordCount()))
  {
    result->error =
        QStringLiteral("Legacy ROS bag worker DONE count does not match decoded records.");
  }
}

void waitForInspect(const QFuture<void>& future, QProgressDialog& progress,
                    const std::shared_ptr<std::atomic<bool>>& cancel_requested)
{
  QFutureWatcher<void> watcher;
  QEventLoop loop;
  QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
  QObject::connect(&progress, &QProgressDialog::canceled, &progress,
                   [cancel_requested]() {
                     cancel_requested->store(true, std::memory_order_relaxed);
                   });
  watcher.setFuture(future);
  progress.show();
  if (!future.isFinished())
  {
    loop.exec();
  }
  watcher.waitForFinished();
  progress.close();
}

void waitForLoad(const QFuture<void>& future, QProgressDialog& progress,
                 const std::shared_ptr<LoadResult>& result)
{
  QFutureWatcher<void> watcher;
  QEventLoop loop;
  QTimer update_timer;
  update_timer.setInterval(100);
  QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
  QObject::connect(&progress, &QProgressDialog::canceled, &progress, [result]() {
    result->cancel_requested.store(true, std::memory_order_relaxed);
  });
  QObject::connect(&update_timer, &QTimer::timeout, &progress, [&progress, result]() {
    const qint64 total = result->total.load(std::memory_order_relaxed);
    const qint64 current = result->current.load(std::memory_order_relaxed);
    if (total > 0 && total <= std::numeric_limits<int>::max())
    {
      progress.setRange(0, static_cast<int>(total));
      progress.setValue(
          static_cast<int>(qBound<qint64>(0, current, total)));
    }
  });
  watcher.setFuture(future);
  update_timer.start();
  progress.show();
  if (!future.isFinished())
  {
    loop.exec();
  }
  watcher.waitForFinished();
  update_timer.stop();
  progress.close();
}

void writePerformanceLog(const QFileInfo& input_file, int selected_topics, qint64 inspect_ms,
                         qint64 load_ms, qint64 merge_ms, const QStringList& metrics)
{
  const QString directory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (directory.isEmpty() || !QDir().mkpath(directory))
  {
    return;
  }
  QFile log_file(QDir(directory).filePath(QStringLiteral("rosbag-load-performance.log")));
  if (!log_file.open(QFile::WriteOnly | QFile::Append | QFile::Text))
  {
    return;
  }
  QTextStream stream(&log_file);
  stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
         << "\tfile=" << input_file.fileName() << "\tbytes=" << input_file.size()
         << "\tselected_topics=" << selected_topics << "\tinspect_ms=" << inspect_ms
         << "\tload_ms=" << load_ms << "\tmerge_ms=" << merge_ms << '\n';
  for (const QString& metric : metrics)
  {
    stream << "  " << metric << '\n';
  }
}

void mergeData(PJ::PlotDataMapRef& source, PJ::PlotDataMapRef& destination)
{
  for (auto& [name, series] : source.numeric)
  {
    destination.getOrCreateNumeric(name).clonePoints(std::move(series));
  }
  for (auto& [name, series] : source.strings)
  {
    destination.getOrCreateStringSeries(name).clonePoints(std::move(series));
  }
}

enum class LoadProtocol
{
  Text,
  Binary,
  Raw
};

LoadProtocol selectedLoadProtocol(const QFileInfo& input_file,
                                  const PJ::ParserFactories* factories)
{
  if (qEnvironmentVariableIsSet("RSPJ_FORCE_LEGACY_ROSBAG_PROTOCOL"))
  {
    return LoadProtocol::Text;
  }
  if (!qEnvironmentVariableIsSet("RSPJ_FORCE_BINARY_ROSBAG_PROTOCOL") &&
      input_file.suffix().compare(QStringLiteral("bag"), Qt::CaseInsensitive) == 0 &&
      factories && factories->find(QStringLiteral("ros1msg")) != factories->end())
  {
    return LoadProtocol::Raw;
  }
  return LoadProtocol::Binary;
}
}  // namespace

const std::vector<const char*>& DataLoadROSBag::compatibleFileExtensions() const
{
  static const std::vector<const char*> native_mcap_default = { "bag", "db3" };
  static const std::vector<const char*> python_mcap_fallback = { "bag", "db3", "mcap" };
  return qEnvironmentVariableIsSet("RSPJ_ENABLE_PYTHON_MCAP_FALLBACK")
             ? python_mcap_fallback
             : native_mcap_default;
}

bool DataLoadROSBag::readDataFromFile(PJ::FileLoadInfo* fileload_info,
                                      PJ::PlotDataMapRef& destination)
{
  const auto fail = [this](const QString& text, const QString& details = {}) {
    if (!headlessRosbagLoad())
    {
      QMessageBox message(QMessageBox::Critical, tr("ROS bag load failed"), text);
      if (!details.isEmpty())
      {
        message.setDetailedText(details);
      }
      message.exec();
    }
    return false;
  };

  const QFileInfo input_file(fileload_info ? fileload_info->filename : QString());
  if (!fileload_info || !input_file.exists() || !input_file.isFile() ||
      !input_file.isReadable())
  {
    return fail(tr("The selected ROS bag does not exist."));
  }

  const QString worker = findWorker();
  const QString python = findPython();
  if (worker.isEmpty())
  {
    return fail(
        tr("The packaged ROS bag worker was not found (runtime/rosbag_python/extract_rosbag.py)."));
  }
  if (python.isEmpty())
  {
    return fail(tr("Python 3 was not found. Set RSPJ_PYTHON to a Python executable "
                   "with the 'rosbags' and 'numpy' packages installed."));
  }

  auto inspect_result = std::make_shared<InspectResult>();
  auto inspect_cancel = std::make_shared<std::atomic<bool>>(false);
  QElapsedTimer stage_timer;
  stage_timer.start();
  const QString bag_path = fileload_info->filename;
  QFuture<void> inspect_future = QtConcurrent::run(
      [python, worker, bag_path, inspect_result, inspect_cancel]() {
        runInspectProcess(python, worker, bag_path, inspect_result, inspect_cancel);
      });
  QProgressDialog inspect_progress(tr("Reading ROS bag topic index..."), tr("Cancel"), 0, 0);
  inspect_progress.setWindowTitle(tr("Inspecting ROS bag"));
  inspect_progress.setWindowModality(Qt::ApplicationModal);
  inspect_progress.setMinimumDuration(250);
  waitForInspect(inspect_future, inspect_progress, inspect_cancel);
  const qint64 inspect_ms = stage_timer.elapsed();
  if (inspect_result->canceled)
  {
    return false;
  }
  if (!inspect_result->error.isEmpty())
  {
    return fail(inspect_result->error, QString::fromUtf8(inspect_result->diagnostics));
  }

  std::vector<PJ::ROSBag::TopicInfo> topics;
  QString index_error;
  if (!parseTopicIndex(inspect_result->output, topics, index_error) || topics.empty())
  {
    return fail(index_error.isEmpty() ? tr("The ROS bag does not contain any readable topics.")
                                      : index_error);
  }

  const QByteArray signature =
      input_file.canonicalFilePath().toUtf8() + '|' +
      QByteArray::number(input_file.size()) + '|' +
      QByteArray::number(input_file.lastModified().toMSecsSinceEpoch());
  const QString settings_key =
      QStringLiteral("ROSBagTopicSelection/") +
      QString::fromLatin1(
          QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex().left(24));
  QStringList selected_topic_names;
  int max_array = 100;
  if (qEnvironmentVariableIsSet("RSPJ_ROSBAG_SELECT_ALL"))
  {
    for (const auto& topic : topics)
    {
      selected_topic_names.push_back(topic.name);
    }
  }
  else
  {
    PJ::ROSBag::TopicDialog topic_dialog(topics, settings_key);
    if (topic_dialog.exec() != QDialog::Accepted)
    {
      return false;
    }
    selected_topic_names = topic_dialog.selectedTopics();
    max_array = topic_dialog.maxArraySize();
  }

  QTemporaryFile topics_file(
      QDir(QDir::tempPath()).filePath(QStringLiteral("rspj-topics-XXXXXX.json")));
  topics_file.setAutoRemove(true);
  if (!topics_file.open())
  {
    return fail(tr("Could not create the temporary topic selection file."));
  }
  QJsonArray selected_topics;
  for (const QString& topic : selected_topic_names)
  {
    selected_topics.push_back(topic);
  }
  const QByteArray selection_json =
      QJsonDocument(selected_topics).toJson(QJsonDocument::Compact);
  if (topics_file.write(selection_json) != selection_json.size() ||
      !topics_file.flush())
  {
    return fail(tr("Could not write the temporary topic selection file."));
  }
  topics_file.close();

  auto load_result = std::make_shared<LoadResult>();
  stage_timer.restart();
  const QString topics_path = topics_file.fileName();
  const PJ::ParserFactories* factories = parserFactories();
  const LoadProtocol protocol = selectedLoadProtocol(input_file, factories);
  QFuture<void> load_future = QtConcurrent::run(
      [python, worker, bag_path, topics_path, max_array, factories, protocol, load_result]() {
        switch (protocol)
        {
          case LoadProtocol::Text:
            runTextLoadProcess(python, worker, bag_path, topics_path, max_array, load_result);
            break;
          case LoadProtocol::Raw:
            runRawLoadProcess(python, worker, bag_path, topics_path, max_array, factories,
                              load_result);
            break;
          case LoadProtocol::Binary:
            runBinaryLoadProcess(python, worker, bag_path, topics_path, max_array, load_result);
            break;
        }
      });
  QProgressDialog load_progress(tr("Reading selected ROS bag topics..."), tr("Cancel"), 0, 0);
  load_progress.setWindowTitle(tr("Loading ROS bag"));
  load_progress.setWindowModality(Qt::ApplicationModal);
  load_progress.setMinimumDuration(250);
  waitForLoad(load_future, load_progress, load_result);
  const qint64 load_ms = stage_timer.elapsed();
  if (load_result->canceled)
  {
    return false;
  }
  if (load_result->error.isEmpty() && load_result->info_messages > 0 &&
      load_result->done_emitted == 0 && !load_result->warnings.isEmpty())
  {
    load_result->error =
        tr("None of the selected ROS bag messages could be decoded.");
  }
  if (!load_result->error.isEmpty())
  {
    QStringList details = load_result->warnings;
    details.append(load_result->metrics);
    return fail(load_result->error, details.join('\n'));
  }
  if (!load_result->warnings.isEmpty() && !headlessRosbagLoad())
  {
    QMessageBox message(
        QMessageBox::Warning, tr("ROS bag loaded with skipped data"),
        tr("%1 topic stage(s) could not be decoded. Other selected data was loaded.")
            .arg(load_result->warnings.size()));
    message.setDetailedText(load_result->warnings.join('\n'));
    message.exec();
  }

  for (const QString& metric : load_result->metrics)
  {
    qInfo().noquote() << "ROS bag" << metric;
  }
  stage_timer.restart();
  mergeData(load_result->data, destination);
  const qint64 merge_ms = stage_timer.elapsed();
  writePerformanceLog(input_file, selected_topic_names.size(), inspect_ms, load_ms, merge_ms,
                      load_result->metrics);
  return true;
}
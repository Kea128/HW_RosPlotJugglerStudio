#include "dataload_rosbag.h"
#include "rosbag_binary_decoder.h"
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
#include <limits>
#include <memory>
#include <utility>

namespace
{
constexpr qsizetype MAX_PENDING_RECORD_BYTES = 16 * 1024 * 1024;
constexpr qsizetype MAX_INSPECT_BYTES = 16 * 1024 * 1024;

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
    result->done = true;
  }
}

void runBinaryLoadProcess(const QString& python, const QString& worker, const QString& bag,
                          const QString& topics_file, int max_array,
                          const std::shared_ptr<LoadResult>& result)
{
  PJ::ROSBag::RosbagBinaryDecoder decoder(result->data);
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(python,
                { QStringLiteral("-u"), worker, bag, QStringLiteral("--protocol"),
                  QStringLiteral("binary-v1"), QStringLiteral("--topics-file"), topics_file,
                  QStringLiteral("--max-array"), QString::number(max_array) });
  if (!process.waitForStarted(10000))
  {
    result->error =
        QStringLiteral("Could not start Python worker: %1").arg(process.errorString());
    return;
  }

  QByteArray stderr_buffer;
  while (process.state() != QProcess::NotRunning)
  {
    process.waitForReadyRead(20);
    const QByteArray data = process.readAllStandardOutput();
    if (!data.isEmpty() && !decoder.append(data))
    {
      result->error = QStringLiteral("Invalid binary data from ROS bag worker.");
    }
    stderr_buffer += process.readAllStandardError();
    consumeLines(stderr_buffer,
                 [&result](const QByteArray& line) { consumeControlLine(line, result); });
    if (stderr_buffer.size() > MAX_PENDING_RECORD_BYTES)
    {
      result->error =
          QStringLiteral("ROS bag worker produced an oversized diagnostic record.");
    }
    if (result->cancel_requested.load(std::memory_order_relaxed))
    {
      result->canceled = true;
      process.kill();
      process.waitForFinished(3000);
      break;
    }
    if (!result->error.isEmpty())
    {
      process.kill();
      process.waitForFinished(3000);
      break;
    }
  }

  const QByteArray final_data = process.readAllStandardOutput();
  if (!final_data.isEmpty() && result->error.isEmpty() && !decoder.append(final_data))
  {
    result->error = QStringLiteral("Invalid binary data from ROS bag worker.");
  }
  stderr_buffer += process.readAllStandardError();
  consumeLines(stderr_buffer,
               [&result](const QByteArray& line) { consumeControlLine(line, result); });
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
    result->error =
        QStringLiteral("ROS bag worker exited with code %1: %2")
            .arg(process.exitCode())
            .arg(process.errorString());
  }
  if (result->error.isEmpty() && !decoder.finish())
  {
    result->error =
        QStringLiteral("ROS bag worker returned an incomplete binary stream.");
  }
  if (result->error.isEmpty())
  {
    result->current.store(static_cast<qint64>(decoder.messages()),
                          std::memory_order_relaxed);
  }
}

void runTextLoadProcess(const QString& python, const QString& worker, const QString& bag,
                        const QString& topics_file, int max_array,
                        const std::shared_ptr<LoadResult>& result)
{
  PJ::ROSBag::RecordParser parser(result->data);
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(python,
                { QStringLiteral("-u"), worker, bag, QStringLiteral("--protocol"),
                  QStringLiteral("text"), QStringLiteral("--topics-file"), topics_file,
                  QStringLiteral("--max-array"), QString::number(max_array) });
  if (!process.waitForStarted(10000))
  {
    result->error =
        QStringLiteral("Could not start Python worker: %1").arg(process.errorString());
    return;
  }

  QByteArray stdout_buffer;
  QByteArray stderr_buffer;
  const auto consume_stdout = [&parser, &result](const QByteArray& line) {
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
  };

  while (process.state() != QProcess::NotRunning)
  {
    process.waitForReadyRead(20);
    stdout_buffer += process.readAllStandardOutput();
    stderr_buffer += process.readAllStandardError();
    consumeLines(stdout_buffer, consume_stdout);
    consumeLines(stderr_buffer,
                 [&result](const QByteArray& line) { consumeControlLine(line, result); });
    if (stdout_buffer.size() > MAX_PENDING_RECORD_BYTES ||
        stderr_buffer.size() > MAX_PENDING_RECORD_BYTES)
    {
      result->error =
          QStringLiteral("ROS bag worker produced an oversized legacy record.");
    }
    if (result->cancel_requested.load(std::memory_order_relaxed))
    {
      result->canceled = true;
      process.kill();
      process.waitForFinished(3000);
      break;
    }
    if (!result->error.isEmpty())
    {
      process.kill();
      process.waitForFinished(3000);
      break;
    }
  }

  stdout_buffer += process.readAllStandardOutput();
  stderr_buffer += process.readAllStandardError();
  consumeLines(stdout_buffer, consume_stdout);
  consumeLines(stderr_buffer,
               [&result](const QByteArray& line) { consumeControlLine(line, result); });
  if (!stdout_buffer.trimmed().isEmpty())
  {
    consume_stdout(normalizedLine(stdout_buffer));
  }
  if (!stderr_buffer.trimmed().isEmpty())
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
    result->error =
        QStringLiteral("Legacy ROS bag worker exited with code %1: %2")
            .arg(process.exitCode())
            .arg(process.errorString());
  }
  if (result->error.isEmpty() && !result->done)
  {
    result->error = QStringLiteral("Legacy ROS bag worker stopped without a DONE record.");
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
  const QFileInfo input_file(fileload_info ? fileload_info->filename : QString());
  if (!fileload_info || !input_file.exists() || !input_file.isFile() ||
      !input_file.isReadable())
  {
    QMessageBox::critical(nullptr, tr("ROS bag load failed"),
                          tr("The selected ROS bag does not exist."));
    return false;
  }

  const QString worker = findWorker();
  const QString python = findPython();
  if (worker.isEmpty())
  {
    QMessageBox::critical(
        nullptr, tr("ROS bag load failed"),
        tr("The packaged ROS bag worker was not found (runtime/rosbag_python/extract_rosbag.py)."));
    return false;
  }
  if (python.isEmpty())
  {
    QMessageBox::critical(nullptr, tr("ROS bag load failed"),
                          tr("Python 3 was not found. Set RSPJ_PYTHON to a Python executable "
                             "with the 'rosbags' and 'numpy' packages installed."));
    return false;
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
    QMessageBox message(QMessageBox::Critical, tr("ROS bag load failed"),
                        inspect_result->error);
    message.setDetailedText(QString::fromUtf8(inspect_result->diagnostics));
    message.exec();
    return false;
  }

  std::vector<PJ::ROSBag::TopicInfo> topics;
  QString index_error;
  if (!parseTopicIndex(inspect_result->output, topics, index_error) || topics.empty())
  {
    QMessageBox::critical(
        nullptr, tr("ROS bag load failed"),
        index_error.isEmpty() ? tr("The ROS bag does not contain any readable topics.")
                              : index_error);
    return false;
  }

  const QByteArray signature =
      input_file.canonicalFilePath().toUtf8() + '|' +
      QByteArray::number(input_file.size()) + '|' +
      QByteArray::number(input_file.lastModified().toMSecsSinceEpoch());
  const QString settings_key =
      QStringLiteral("ROSBagTopicSelection/") +
      QString::fromLatin1(
          QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex().left(24));
  PJ::ROSBag::TopicDialog topic_dialog(topics, settings_key);
  if (topic_dialog.exec() != QDialog::Accepted)
  {
    return false;
  }

  QTemporaryFile topics_file(
      QDir(QDir::tempPath()).filePath(QStringLiteral("rspj-topics-XXXXXX.json")));
  topics_file.setAutoRemove(true);
  if (!topics_file.open())
  {
    QMessageBox::critical(nullptr, tr("ROS bag load failed"),
                          tr("Could not create the temporary topic selection file."));
    return false;
  }
  QJsonArray selected_topics;
  const QStringList selected_topic_names = topic_dialog.selectedTopics();
  for (const QString& topic : selected_topic_names)
  {
    selected_topics.push_back(topic);
  }
  const QByteArray selection_json =
      QJsonDocument(selected_topics).toJson(QJsonDocument::Compact);
  if (topics_file.write(selection_json) != selection_json.size() ||
      !topics_file.flush())
  {
    QMessageBox::critical(nullptr, tr("ROS bag load failed"),
                          tr("Could not write the temporary topic selection file."));
    return false;
  }
  topics_file.close();

  auto load_result = std::make_shared<LoadResult>();
  stage_timer.restart();
  const QString topics_path = topics_file.fileName();
  const int max_array = topic_dialog.maxArraySize();
  QFuture<void> load_future =
      QtConcurrent::run([python, worker, bag_path, topics_path, max_array, load_result]() {
        if (qEnvironmentVariableIsSet("RSPJ_FORCE_LEGACY_ROSBAG_PROTOCOL"))
        {
          runTextLoadProcess(python, worker, bag_path, topics_path, max_array, load_result);
        }
        else
        {
          runBinaryLoadProcess(python, worker, bag_path, topics_path, max_array, load_result);
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
  if (!load_result->error.isEmpty())
  {
    QMessageBox message(QMessageBox::Critical, tr("ROS bag load failed"),
                        load_result->error);
    QStringList details = load_result->warnings;
    details.append(load_result->metrics);
    if (!details.isEmpty())
    {
      message.setDetailedText(details.join('\n'));
    }
    message.exec();
    return false;
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
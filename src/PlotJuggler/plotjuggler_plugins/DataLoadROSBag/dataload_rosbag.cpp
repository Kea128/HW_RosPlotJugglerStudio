#include "dataload_rosbag.h"
#include "rosbag_record_parser.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>

#include <utility>

namespace
{
constexpr qsizetype MAX_PENDING_RECORD_BYTES = 16 * 1024 * 1024;

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
  static const std::vector<const char*> extensions = { "bag", "db3", "mcap" };
  return extensions;
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

  PJ::PlotDataMapRef staged_data;
  PJ::ROSBag::RecordParser parser(staged_data);
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(python, { QStringLiteral("-u"), worker, fileload_info->filename });
  if (!process.waitForStarted(10000))
  {
    QMessageBox::critical(nullptr, tr("ROS bag load failed"),
                          tr("Could not start Python worker '%1': %2")
                              .arg(python, process.errorString()));
    return false;
  }

  QProgressDialog progress(tr("Reading ROS bag..."), tr("Cancel"), 0, 0);
  progress.setWindowTitle(tr("Loading ROS bag"));
  progress.setWindowModality(Qt::ApplicationModal);
  progress.setMinimumDuration(250);

  QByteArray stdout_buffer;
  QByteArray stderr_buffer;
  QString protocol_error;
  QStringList warnings;
  bool done = false;
  bool canceled = false;

  const auto consumeLine = [&](const QByteArray& raw_line, bool from_stderr) {
    QByteArray line = raw_line;
    if (line.endsWith('\r'))
    {
      line.chop(1);
    }
    if (line.isEmpty())
    {
      return;
    }
    const QList<QByteArray> fields = line.split('\t');
    const QByteArray kind = fields.front();
    if (kind == "N" || kind == "S")
    {
      if (from_stderr || !parser.parse(line))
      {
        protocol_error = tr("Invalid data record from ROS bag worker: %1")
                             .arg(QString::fromUtf8(line.left(240)));
      }
    }
    else if (kind == "INFO" && fields.size() == 3)
    {
      bool ok = false;
      const int total = fields[2].toInt(&ok);
      if (ok && total >= 0)
      {
        progress.setRange(0, total);
      }
    }
    else if (kind == "PROGRESS" && fields.size() == 4)
    {
      bool current_ok = false;
      bool total_ok = false;
      const int current = fields[1].toInt(&current_ok);
      const int total = fields[2].toInt(&total_ok);
      if (current_ok && total_ok && total >= 0)
      {
        progress.setRange(0, total);
        progress.setValue(qBound(0, current, total));
      }
    }
    else if (kind == "DONE" && fields.size() == 3)
    {
      done = true;
    }
    else if (kind == "WARN" && fields.size() >= 3)
    {
      warnings.push_back(QString::fromUtf8(line));
    }
    else if (kind == "ERROR" && fields.size() >= 2)
    {
      protocol_error = QString::fromUtf8(line.mid(6)).trimmed();
    }
    else
    {
      protocol_error =
          tr("Unexpected output from ROS bag worker: %1").arg(QString::fromUtf8(line.left(240)));
    }
  };

  const auto consumeBuffer = [&](QByteArray& buffer, bool from_stderr) {
    qsizetype newline = -1;
    while ((newline = buffer.indexOf('\n')) >= 0)
    {
      const QByteArray line = buffer.left(newline);
      buffer.remove(0, newline + 1);
      consumeLine(line, from_stderr);
    }
  };

  while (process.state() != QProcess::NotRunning)
  {
    process.waitForReadyRead(50);
    stdout_buffer += process.readAllStandardOutput();
    stderr_buffer += process.readAllStandardError();
    if (stdout_buffer.size() > MAX_PENDING_RECORD_BYTES ||
        stderr_buffer.size() > MAX_PENDING_RECORD_BYTES)
    {
      protocol_error = tr("ROS bag worker produced an unterminated record larger than 16 MiB.");
    }
    consumeBuffer(stdout_buffer, false);
    consumeBuffer(stderr_buffer, true);
    QApplication::processEvents();

    if (progress.wasCanceled())
    {
      canceled = true;
      process.kill();
      process.waitForFinished(3000);
      break;
    }
    if (!protocol_error.isEmpty())
    {
      process.kill();
      process.waitForFinished(3000);
      break;
    }
  }

  stdout_buffer += process.readAllStandardOutput();
  stderr_buffer += process.readAllStandardError();
  consumeBuffer(stdout_buffer, false);
  consumeBuffer(stderr_buffer, true);
  if (!stdout_buffer.trimmed().isEmpty())
  {
    consumeLine(stdout_buffer, false);
  }
  if (!stderr_buffer.trimmed().isEmpty())
  {
    consumeLine(stderr_buffer, true);
  }

  if (canceled)
  {
    return false;
  }
  if (protocol_error.isEmpty() &&
      (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0))
  {
    protocol_error = tr("ROS bag worker exited with code %1: %2")
                         .arg(process.exitCode())
                         .arg(process.errorString());
  }
  if (protocol_error.isEmpty() && !done)
  {
    protocol_error = tr("ROS bag worker stopped without a DONE record.");
  }
  if (!protocol_error.isEmpty())
  {
    QMessageBox message(QMessageBox::Critical, tr("ROS bag load failed"), protocol_error);
    if (!warnings.isEmpty())
    {
      message.setDetailedText(warnings.join('\n'));
    }
    message.exec();
    return false;
  }

  if (!fileload_info->prefix.isEmpty())
  {
    const std::string prefix = fileload_info->prefix.toStdString();
    PJ::AddPrefixToPlotData(prefix, staged_data.numeric);
    PJ::AddPrefixToPlotData(prefix, staged_data.strings);
  }
  mergeData(staged_data, destination);
  return true;
}
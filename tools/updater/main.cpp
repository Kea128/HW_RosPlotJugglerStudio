#include "updater_logic.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTextStream>
#include <QThread>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
QFile* logFile()
{
  static QFile file;
  if (!file.isOpen())
  {
    QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty())
    {
      base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    const QString directory = QDir(base).filePath(QStringLiteral("RosPlotJugglerStudio/Updater"));
    QDir().mkpath(directory);
    file.setFileName(QDir(directory).filePath(QStringLiteral("updater.log")));
    file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
  }
  return &file;
}

void log(const QString& message)
{
  QTextStream stream(logFile());
  stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << " " << message << "\n";
  stream.flush();
}

bool waitForParent(quint64 pid, QString* error)
{
#ifdef Q_OS_WIN
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
  if (!process)
  {
    *error = QStringLiteral("Unable to open parent process");
    return false;
  }
  const DWORD result = WaitForSingleObject(process, 5 * 60 * 1000);
  CloseHandle(process);
  if (result != WAIT_OBJECT_0)
  {
    *error = QStringLiteral("Parent application did not exit normally within five minutes");
    return false;
  }
  return true;
#else
  Q_UNUSED(pid);
  *error = QStringLiteral("The updater is supported only on Windows");
  return false;
#endif
}

bool renameDirectory(const QString& from, const QString& to)
{
  return QDir().rename(QDir::cleanPath(from), QDir::cleanPath(to));
}

bool restoreBackup(const QString& installRoot, const QString& backupRoot)
{
  const QString failedRoot = installRoot + QStringLiteral(".failed-") +
                             QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (QFileInfo::exists(installRoot) && !renameDirectory(installRoot, failedRoot))
  {
    log(QStringLiteral("Rollback could not move failed installation aside"));
    return false;
  }
  if (!renameDirectory(backupRoot, installRoot))
  {
    log(QStringLiteral("Rollback could not restore backup"));
    return false;
  }
  QDir(failedRoot).removeRecursively();
  return true;
}

bool runHealthProbe(const QString& application, const QString& healthFile, int timeoutSeconds,
                    QString* error)
{
  QFile::remove(healthFile);
  QProcess process;
  process.setProgram(application);
  process.setArguments(
      { QStringLiteral("--nosplash"), QStringLiteral("--update-health-file"), healthFile });
  process.setWorkingDirectory(QFileInfo(application).absolutePath());
  process.start();
  if (!process.waitForStarted(10000))
  {
    *error = QStringLiteral("Updated application could not start: %1").arg(process.errorString());
    return false;
  }
  process.closeWriteChannel();

  const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutSeconds * 1000LL;
  while (QDateTime::currentMSecsSinceEpoch() < deadline && !QFileInfo::exists(healthFile) &&
         process.state() != QProcess::NotRunning)
  {
    process.waitForFinished(100);
  }
  if (!QFileInfo::exists(healthFile))
  {
    *error = process.state() == QProcess::NotRunning
                 ? QStringLiteral("Updated application exited before health confirmation")
                 : QStringLiteral("Updated application health confirmation timed out");
    if (process.state() != QProcess::NotRunning)
    {
      log(QStringLiteral("Health probe is still running; refusing to force-kill it"));
    }
    return false;
  }
  if (process.state() != QProcess::NotRunning && !process.waitForFinished(15000))
  {
    *error = QStringLiteral("Health probe confirmed startup but did not exit normally");
    log(QStringLiteral("Health probe remained running; refusing to force-kill it"));
    return false;
  }
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
}  // namespace

int main(int argc, char* argv[])
{
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("RosPlotJugglerStudio"));
  QCoreApplication::setApplicationName(QStringLiteral("Updater"));

  StudioUpdater::Options options;
  QString error;
  if (!StudioUpdater::parseOptions(app.arguments(), &options, &error))
  {
    log(QStringLiteral("Argument validation failed: %1").arg(error));
    return 2;
  }

  QString installedVersion;
  if (!QFileInfo(options.packagePath).isFile() ||
      !QFileInfo(options.extractionScript).isFile() ||
      !StudioUpdater::validateInstallMarker(options.installRoot, &installedVersion, &error))
  {
    log(QStringLiteral("Preflight validation failed: %1").arg(error));
    return 3;
  }

  const QString installParent = QFileInfo(options.installRoot).absolutePath();
  QLockFile lock(QDir(installParent).filePath(QStringLiteral(".rspj-update.lock")));
  lock.setStaleLockTime(30 * 60 * 1000);
  if (!lock.tryLock(0))
  {
    log(QStringLiteral("Another updater instance owns the install-root lock"));
    return 4;
  }
  if (!StudioUpdater::checkDirectoryWritable(installParent, &error))
  {
    log(error);
    return 5;
  }

  QStorageInfo storage(installParent);
  storage.refresh();
  const quint64 requiredSpace =
      static_cast<quint64>(options.expectedSize) * 3 + StudioUpdater::directorySize(options.installRoot);
  if (!storage.isValid() || !storage.isReady() || storage.bytesAvailable() < requiredSpace)
  {
    log(QStringLiteral("Insufficient free disk space for staging and rollback"));
    return 6;
  }
  if (!waitForParent(options.parentPid, &error))
  {
    log(error);
    return 7;
  }
  if (!StudioUpdater::verifyFile(options.packagePath, options.expectedSize,
                                 options.expectedSha256, &error))
  {
    log(QStringLiteral("Second package verification failed: %1").arg(error));
    return 8;
  }

  const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString rootName = QFileInfo(options.installRoot).fileName();
  const QString staging = QDir(installParent).filePath(QStringLiteral(".%1-staging-%2").arg(rootName, token));
  const QString backup = QDir(installParent).filePath(QStringLiteral(".%1-backup-%2").arg(rootName, token));
  QDir(staging).removeRecursively();
  QDir(backup).removeRecursively();
  if (!QDir().mkpath(staging))
  {
    log(QStringLiteral("Unable to create staging directory"));
    return 9;
  }

  QProcess extractor;
  extractor.setProgram(QStringLiteral("powershell.exe"));
  extractor.setArguments({ QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
                           QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
                           QStringLiteral("Bypass"), QStringLiteral("-File"),
                           options.extractionScript, QStringLiteral("-Archive"),
                           options.packagePath, QStringLiteral("-Destination"), staging });
  extractor.start();
  const bool extractorStarted = extractor.waitForStarted(10000);
  if (extractorStarted)
  {
    extractor.closeWriteChannel();
  }
  if (!extractorStarted || !extractor.waitForFinished(30 * 60 * 1000) ||
      extractor.exitStatus() != QProcess::NormalExit || extractor.exitCode() != 0)
  {
    log(QStringLiteral("Secure extraction failed: %1").arg(QString::fromLocal8Bit(extractor.readAllStandardError())));
    QDir(staging).removeRecursively();
    return 10;
  }

  const QStringList topLevel =
      QDir(staging).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  const QString stagedRoot =
      topLevel.size() == 1 ? QDir(staging).filePath(topLevel.constFirst()) : QString();
  if (topLevel.size() != 1 ||
      !StudioUpdater::validateStagedPackage(stagedRoot, options.expectedVersion, &error))
  {
    log(QStringLiteral("Staging validation failed: %1").arg(error));
    QDir(staging).removeRecursively();
    return 11;
  }

  if (!renameDirectory(options.installRoot, backup) ||
      !renameDirectory(stagedRoot, options.installRoot))
  {
    log(QStringLiteral("Atomic directory swap failed"));
    if (QFileInfo::exists(backup) && !QFileInfo::exists(options.installRoot))
    {
      renameDirectory(backup, options.installRoot);
    }
    QDir(staging).removeRecursively();
    return 12;
  }
  QDir(staging).removeRecursively();

  const QString healthFile =
      QDir(QFileInfo(logFile()->fileName()).absolutePath()).filePath(QStringLiteral("health-%1.ok").arg(token));
  const QString newApplication =
      QDir(options.installRoot).filePath(QStringLiteral("bin/RosPlotJugglerStudio.exe"));
  if (!runHealthProbe(newApplication, healthFile, options.healthTimeoutSeconds, &error))
  {
    log(QStringLiteral("Health check failed: %1").arg(error));
    if (!restoreBackup(options.installRoot, backup))
    {
      return 13;
    }
    const QString oldApplication =
        QDir(options.installRoot).filePath(QStringLiteral("bin/RosPlotJugglerStudio.exe"));
    QProcess::startDetached(oldApplication, {}, QFileInfo(oldApplication).absolutePath());
    return 14;
  }
  QFile::remove(healthFile);

  if (!QProcess::startDetached(newApplication, {}, QFileInfo(newApplication).absolutePath()))
  {
    log(QStringLiteral("Updated application failed to restart; rolling back"));
    if (restoreBackup(options.installRoot, backup))
    {
      const QString oldApplication =
          QDir(options.installRoot).filePath(QStringLiteral("bin/RosPlotJugglerStudio.exe"));
      QProcess::startDetached(oldApplication, {}, QFileInfo(oldApplication).absolutePath());
    }
    return 15;
  }
  QDir(backup).removeRecursively();
  log(QStringLiteral("Updated successfully from %1 to %2")
          .arg(installedVersion, options.expectedVersion));
  return 0;
}

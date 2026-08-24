#include "update_coordinator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QWidget>
#include <climits>

namespace StudioUpdate
{

UpdateCoordinator::UpdateCoordinator(QWidget* parentWidget)
  : QObject(parentWidget), _parentWidget(parentWidget), _checker(this), _downloader(this)
{
  connect(&_checker, &UpdateChecker::checkStarted, this,
          [this](UpdateChecker::Mode) { setState(State::Checking); });
  connect(&_checker, &UpdateChecker::updateAvailable, this, &UpdateCoordinator::offerUpdate);
  connect(&_checker, &UpdateChecker::upToDate, this, [this](UpdateChecker::Mode mode) {
    setState(State::Idle);
    if (mode == UpdateChecker::Mode::Manual)
    {
      QMessageBox::information(_parentWidget, tr("Check for Updates"),
                               tr("RosPlotJugglerStudio is up to date."));
    }
  });
  connect(&_checker, &UpdateChecker::checkFailed, this,
          [this](const QString& error, UpdateChecker::Mode mode) {
            setState(State::Error);
            if (mode == UpdateChecker::Mode::Manual)
            {
              QMessageBox::warning(_parentWidget, tr("Update Check Failed"), error);
            }
            setState(State::Idle);
          });

  connect(&_downloader, &UpdateDownloader::progress, this, [this](qint64 received, qint64 total) {
    if (_progress)
    {
      _progress->setMaximum(total > INT_MAX ? INT_MAX : static_cast<int>(total));
      const qint64 scaled = total > INT_MAX && total > 0 ? received * INT_MAX / total : received;
      _progress->setValue(static_cast<int>(qMin<qint64>(scaled, INT_MAX)));
    }
  });
  connect(&_downloader, &UpdateDownloader::cancelled, this, [this]() {
    if (_progress)
    {
      _progress->deleteLater();
      _progress = nullptr;
    }
    setState(State::Idle);
  });
  connect(&_downloader, &UpdateDownloader::failed, this, [this](const QString& error) {
    if (_progress)
    {
      _progress->deleteLater();
      _progress = nullptr;
    }
    setState(State::Error);
    QMessageBox::warning(_parentWidget, tr("Update Download Failed"), error);
    setState(State::Idle);
  });
  connect(&_downloader, &UpdateDownloader::completed, this, [this](const QString& path) {
    if (_progress)
    {
      _progress->setValue(_progress->maximum());
      _progress->deleteLater();
      _progress = nullptr;
    }
    _downloadedPath = path;
    _readyManifest = _downloader.manifest();
    setState(State::Ready);
    const auto answer = QMessageBox::question(
        _parentWidget, tr("Update Downloaded"),
        tr("The update package has been downloaded and verified:\n%1\n\n"
           "Install now and restart?（立即安装并重启）")
            .arg(QDir::toNativeSeparators(path)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes)
    {
      QString error;
      if (!launchExternalUpdater(&error))
      {
        QMessageBox::warning(_parentWidget, tr("Update Installation Failed"), error);
        setState(State::Idle);
      }
    }
  });
}

void UpdateCoordinator::checkManually()
{
  check(UpdateChecker::Mode::Manual);
}

void UpdateCoordinator::checkAutomatically()
{
  if (QSettings().value(QStringLiteral("Updates/automaticCheck"), true).toBool())
  {
    check(UpdateChecker::Mode::Automatic);
  }
}

void UpdateCoordinator::check(UpdateChecker::Mode mode)
{
  if (_state == State::Checking || _state == State::Downloading)
  {
    if (mode == UpdateChecker::Mode::Manual)
    {
      QMessageBox::information(_parentWidget, tr("Check for Updates"),
                               tr("An update operation is already in progress."));
    }
    return;
  }
  const QString channel =
      QSettings().value(QStringLiteral("Updates/channel"), QStringLiteral("stable")).toString();
  _checker.check(channel, mode);
}

void UpdateCoordinator::offerUpdate(const UpdateManifest& manifest, UpdateChecker::Mode mode)
{
  Q_UNUSED(mode);
  setState(State::UpdateAvailable);
  QString message =
      tr("RosPlotJugglerStudio %1 is available (%2 MiB).\n\nDownload it now?")
          .arg(manifest.version.toString())
          .arg(static_cast<double>(manifest.size) / (1024.0 * 1024.0), 0, 'f', 1);
  if (!manifest.supportsCurrentVersion(QCoreApplication::applicationVersion()))
  {
    message += tr("\n\nThis release is outside the direct-update support range. "
                  "You may still download it, but installation will not be automatic.");
  }
  if (QMessageBox::question(_parentWidget, tr("Update Available"), message,
                            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
  {
    setState(State::Idle);
    return;
  }

  const QString directory =
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::TempLocation)
          : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  const QString fileName = QFileInfo(manifest.assetUrl.path()).fileName().isEmpty()
                               ? QStringLiteral("RosPlotJugglerStudio-update.bin")
                               : QFileInfo(manifest.assetUrl.path()).fileName();
  const QString destination = QDir(directory).filePath(fileName);

  _progress = new QProgressDialog(tr("Downloading update..."), tr("Cancel"), 0, 0, _parentWidget);
  _progress->setWindowModality(Qt::NonModal);
  _progress->setAutoClose(false);
  _progress->show();
  connect(_progress, &QProgressDialog::canceled, &_downloader, &UpdateDownloader::cancel);
  setState(State::Downloading);
  _downloader.download(manifest, destination);
}

bool UpdateCoordinator::launchExternalUpdater(QString* error)
{
  if (_downloadedPath.isEmpty() || _readyManifest.schemaVersion != 1)
  {
    if (error)
    {
      *error = tr("No verified update package is available.");
    }
    return false;
  }

#ifndef Q_OS_WIN
  if (error)
  {
    *error = tr("Automatic installation is currently supported only on Windows.");
  }
  return false;
#else
  const QString installRoot = QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(".."));
  const QString bundledDirectory =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("updater"));
  const QString bundledUpdater =
      QDir(bundledDirectory).filePath(QStringLiteral("RosPlotJugglerUpdater.exe"));
  const QString bundledScript =
      QDir(bundledDirectory).filePath(QStringLiteral("extract-update.ps1"));
  if (!QFileInfo::exists(QDir(installRoot).filePath(QStringLiteral("manifest.json"))) ||
      !QFileInfo(bundledUpdater).isFile() || !QFileInfo(bundledScript).isFile())
  {
    if (error)
    {
      *error = tr("This installation does not contain the updater or package manifest marker.");
    }
    return false;
  }

  const QString temporaryRoot =
      QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
          .filePath(QStringLiteral("RosPlotJugglerStudio-update-%1")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  if (!QDir().mkpath(temporaryRoot))
  {
    if (error)
    {
      *error = tr("Unable to create the temporary updater directory.");
    }
    return false;
  }
  const QString updater = QDir(temporaryRoot).filePath(QStringLiteral("RosPlotJugglerUpdater.exe"));
  const QString script = QDir(temporaryRoot).filePath(QStringLiteral("extract-update.ps1"));
  if (!QFile::copy(bundledUpdater, updater) || !QFile::copy(bundledScript, script))
  {
    QDir(temporaryRoot).removeRecursively();
    if (error)
    {
      *error = tr("Unable to copy updater components to the user temporary directory.");
    }
    return false;
  }

  const QStringList arguments = {
    QStringLiteral("--protocol"), QStringLiteral("1"),
    QStringLiteral("--package"), QDir::cleanPath(QFileInfo(_downloadedPath).absoluteFilePath()),
    QStringLiteral("--install-root"), installRoot,
    QStringLiteral("--extract-script"), QDir::cleanPath(script),
    QStringLiteral("--version"), _readyManifest.version.toString(),
    QStringLiteral("--sha256"), QString::fromLatin1(_readyManifest.sha256),
    QStringLiteral("--size"), QString::number(_readyManifest.size),
    QStringLiteral("--parent-pid"), QString::number(QCoreApplication::applicationPid()),
    QStringLiteral("--health-timeout"), QStringLiteral("45")
  };
  QProcess updaterProcess;
  updaterProcess.setProgram(updater);
  updaterProcess.setArguments(arguments);
  updaterProcess.setWorkingDirectory(temporaryRoot);
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(
      QStringLiteral("PATH"),
      QCoreApplication::applicationDirPath() + QDir::listSeparator() +
          environment.value(QStringLiteral("PATH")));
  updaterProcess.setProcessEnvironment(environment);
  if (!updaterProcess.startDetached())
  {
    QDir(temporaryRoot).removeRecursively();
    if (error)
    {
      *error = tr("Unable to start the external updater.");
    }
    return false;
  }
  emit normalCloseRequested();
  return true;
#endif
}

UpdateCoordinator::State UpdateCoordinator::state() const
{
  return _state;
}

void UpdateCoordinator::setState(State state)
{
  if (_state != state)
  {
    _state = state;
    emit stateChanged(state);
  }
}

}  // namespace StudioUpdate

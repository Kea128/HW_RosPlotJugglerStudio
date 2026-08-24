#pragma once

#include "update_checker.h"
#include "update_downloader.h"

#include <QObject>

class QProgressDialog;
class QWidget;

namespace StudioUpdate
{

class UpdateCoordinator : public QObject
{
  Q_OBJECT

public:
  enum class State
  {
    Idle,
    Checking,
    UpdateAvailable,
    Downloading,
    Ready,
    Error
  };
  Q_ENUM(State)

  explicit UpdateCoordinator(QWidget* parentWidget);
  void checkManually();
  void checkAutomatically();
  bool launchExternalUpdater(QString* error = nullptr);
  State state() const;

signals:
  void stateChanged(StudioUpdate::UpdateCoordinator::State state);
  void normalCloseRequested();

private:
  void check(UpdateChecker::Mode mode);
  void setState(State state);
  void offerUpdate(const UpdateManifest& manifest, UpdateChecker::Mode mode);

  QWidget* _parentWidget = nullptr;
  UpdateChecker _checker;
  UpdateDownloader _downloader;
  QProgressDialog* _progress = nullptr;
  State _state = State::Idle;
  QString _downloadedPath;
  UpdateManifest _readyManifest;
};

}  // namespace StudioUpdate

#pragma once

#include "update_manifest.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

namespace StudioUpdate
{

class UpdateChecker : public QObject
{
  Q_OBJECT

public:
  enum class Mode
  {
    Automatic,
    Manual
  };
  Q_ENUM(Mode)

  explicit UpdateChecker(QObject* parent = nullptr);
  void check(const QString& channel, Mode mode);
  void cancel();
  bool isChecking() const;

  static QUrl manifestUrl(const QString& channel);

signals:
  void checkStarted(StudioUpdate::UpdateChecker::Mode mode);
  void updateAvailable(const StudioUpdate::UpdateManifest& manifest,
                       StudioUpdate::UpdateChecker::Mode mode);
  void upToDate(StudioUpdate::UpdateChecker::Mode mode);
  void checkFailed(const QString& error, StudioUpdate::UpdateChecker::Mode mode);

private:
  void request(const QUrl& url);
  void finishWithError(const QString& error);

  QNetworkAccessManager _manager;
  QPointer<QNetworkReply> _reply;
  QTimer _timeout;
  QString _channel;
  Mode _mode = Mode::Automatic;
  int _redirects = 0;
};

}  // namespace StudioUpdate

Q_DECLARE_METATYPE(StudioUpdate::UpdateManifest)

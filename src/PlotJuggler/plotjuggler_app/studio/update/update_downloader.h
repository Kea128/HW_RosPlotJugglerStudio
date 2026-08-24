#pragma once

#include "update_manifest.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <memory>

class QNetworkReply;

namespace StudioUpdate
{

class UpdateDownloader : public QObject
{
  Q_OBJECT

public:
  explicit UpdateDownloader(QObject* parent = nullptr);
  UpdateDownloader(QNetworkAccessManager* manager, QObject* parent);
  void download(const UpdateManifest& manifest, const QString& destination);
  void cancel();
  bool isDownloading() const;
  const UpdateManifest& manifest() const { return _manifest; }

signals:
  void progress(qint64 received, qint64 total);
  void completed(const QString& path);
  void cancelled();
  void failed(const QString& error);

private:
  void request(const QUrl& url);
  void consumeAvailableData();
  void fail(const QString& error);
  void reset();

  QNetworkAccessManager* _manager = nullptr;
  QPointer<QNetworkReply> _reply;
  QTimer _timeout;
  std::unique_ptr<QSaveFile> _file;
  QCryptographicHash _hash{ QCryptographicHash::Sha256 };
  UpdateManifest _manifest;
  QString _destination;
  qint64 _received = 0;
  int _redirects = 0;
  bool _cancelling = false;
};

}  // namespace StudioUpdate

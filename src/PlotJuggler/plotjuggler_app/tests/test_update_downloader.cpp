#include "studio/update/update_downloader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>
#include <cstring>

namespace
{
QCoreApplication* ensureApplication()
{
  if (QCoreApplication::instance())
  {
    return QCoreApplication::instance();
  }
  static int argc = 1;
  static char name[] = "test_update_downloader";
  static char* argv[] = { name, nullptr };
  static QCoreApplication application(argc, argv);
  return &application;
}

class FakeReply : public QNetworkReply
{
public:
  FakeReply(const QNetworkRequest& request, QByteArray payload, bool hold, QObject* parent)
    : QNetworkReply(parent), _payload(std::move(payload)), _hold(hold)
  {
    setRequest(request);
    setUrl(request.url());
    setOperation(QNetworkAccessManager::GetOperation);
    open(QIODevice::ReadOnly);
    QTimer::singleShot(0, this, [this]() {
      if (_finished)
      {
        return;
      }
      _available = true;
      emit readyRead();
      emit downloadProgress(_payload.size(), _payload.size());
      if (!_hold)
      {
        _finished = true;
        setFinished(true);
        emit finished();
      }
    });
  }

  void abort() override
  {
    if (_finished)
    {
      return;
    }
    _finished = true;
    setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
    setFinished(true);
    emit finished();
  }

  qint64 bytesAvailable() const override
  {
    return (_available ? _payload.size() - _offset : 0) + QIODevice::bytesAvailable();
  }

protected:
  qint64 readData(char* data, qint64 maximum) override
  {
    if (!_available || _offset >= _payload.size())
    {
      return -1;
    }
    const qint64 count = qMin(maximum, _payload.size() - _offset);
    std::memcpy(data, _payload.constData() + _offset, static_cast<size_t>(count));
    _offset += count;
    return count;
  }

private:
  QByteArray _payload;
  qint64 _offset = 0;
  bool _hold = false;
  bool _available = false;
  bool _finished = false;
};

class FakeNetworkManager : public QNetworkAccessManager
{
public:
  QByteArray payload;
  bool hold = false;

protected:
  QNetworkReply* createRequest(Operation, const QNetworkRequest& request,
                               QIODevice*) override
  {
    return new FakeReply(request, payload, hold, this);
  }
};

StudioUpdate::UpdateManifest manifestFor(const QByteArray& payload, qint64 declaredSize = -1)
{
  StudioUpdate::UpdateManifest manifest;
  manifest.schemaVersion = 1;
  manifest.assetUrl = QUrl(QStringLiteral("https://github.com/example/update.zip"));
  manifest.size = declaredSize >= 0 ? declaredSize : payload.size();
  manifest.sha256 =
      QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
  return manifest;
}

bool waitFor(QSignalSpy& spy)
{
  return !spy.isEmpty() || spy.wait(2000);
}
}  // namespace

TEST(UpdateDownloader, CommitsOnlyCompleteVerifiedDownload)
{
  ensureApplication();
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  FakeNetworkManager manager;
  manager.payload = QByteArray("complete package");
  StudioUpdate::UpdateDownloader downloader(&manager, nullptr);
  QSignalSpy completed(&downloader, &StudioUpdate::UpdateDownloader::completed);
  QSignalSpy failed(&downloader, &StudioUpdate::UpdateDownloader::failed);
  const QString destination = temporary.filePath(QStringLiteral("update.zip"));

  downloader.download(manifestFor(manager.payload), destination);

  ASSERT_TRUE(waitFor(completed));
  EXPECT_TRUE(failed.isEmpty());
  EXPECT_TRUE(QFileInfo(destination).isFile());
}

TEST(UpdateDownloader, RejectsTruncatedDownloadWithoutDestination)
{
  ensureApplication();
  QTemporaryDir temporary;
  FakeNetworkManager manager;
  manager.payload = QByteArray("short");
  StudioUpdate::UpdateDownloader downloader(&manager, nullptr);
  QSignalSpy failed(&downloader, &StudioUpdate::UpdateDownloader::failed);
  const QString destination = temporary.filePath(QStringLiteral("truncated.zip"));

  downloader.download(manifestFor(manager.payload, manager.payload.size() + 10), destination);

  ASSERT_TRUE(waitFor(failed));
  EXPECT_FALSE(QFileInfo::exists(destination));
}

TEST(UpdateDownloader, RejectsHashMismatchWithoutDestination)
{
  ensureApplication();
  QTemporaryDir temporary;
  FakeNetworkManager manager;
  manager.payload = QByteArray("tampered package");
  StudioUpdate::UpdateDownloader downloader(&manager, nullptr);
  QSignalSpy failed(&downloader, &StudioUpdate::UpdateDownloader::failed);
  const QString destination = temporary.filePath(QStringLiteral("tampered.zip"));
  StudioUpdate::UpdateManifest manifest = manifestFor(manager.payload);
  manifest.sha256 = QByteArray(64, '0');

  downloader.download(manifest, destination);

  ASSERT_TRUE(waitFor(failed));
  EXPECT_FALSE(QFileInfo::exists(destination));
}

TEST(UpdateDownloader, CancellationDiscardsPartialDownload)
{
  ensureApplication();
  QTemporaryDir temporary;
  FakeNetworkManager manager;
  manager.payload = QByteArray(128 * 1024, 'x');
  manager.hold = true;
  StudioUpdate::UpdateDownloader downloader(&manager, nullptr);
  QSignalSpy cancelled(&downloader, &StudioUpdate::UpdateDownloader::cancelled);
  QSignalSpy completed(&downloader, &StudioUpdate::UpdateDownloader::completed);
  const QString destination = temporary.filePath(QStringLiteral("cancelled.zip"));

  downloader.download(manifestFor(manager.payload), destination);
  QTimer::singleShot(0, &downloader, &StudioUpdate::UpdateDownloader::cancel);

  ASSERT_TRUE(waitFor(cancelled));
  EXPECT_TRUE(completed.isEmpty());
  EXPECT_FALSE(QFileInfo::exists(destination));
}

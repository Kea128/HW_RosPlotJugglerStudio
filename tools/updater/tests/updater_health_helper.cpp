#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace
{
bool writeFile(const QString& path, const QByteArray& contents)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    return false;
  }
  return file.write(contents) == contents.size();
}
}

int main(int argc, char* argv[])
{
  QCoreApplication application(argc, argv);
  const QString binDirectory = QCoreApplication::applicationDirPath();
  const QString versionFile = QDir(binDirectory).filePath(QStringLiteral("../version.txt"));
  QFile version(versionFile);
  QByteArray versionText("unknown");
  if (version.open(QIODevice::ReadOnly))
  {
    versionText = version.readAll().trimmed();
  }

  const QStringList arguments = application.arguments();
  const int healthIndex = arguments.indexOf(QStringLiteral("--update-health-file"));
  if (healthIndex >= 0 && healthIndex + 1 < arguments.size())
  {
    QFile mode(QDir(binDirectory).filePath(QStringLiteral("health-mode.txt")));
    if (mode.open(QIODevice::ReadOnly) && mode.readAll().trimmed() == QByteArray("fail"))
    {
      return 23;
    }
    return writeFile(arguments.at(healthIndex + 1), QByteArray("ok")) ? 0 : 24;
  }

  const QString launchMarker = qEnvironmentVariable("RSPJ_TEST_LAUNCH_MARKER");
  if (!launchMarker.isEmpty() && !writeFile(launchMarker, versionText))
  {
    return 25;
  }
  return 0;
}

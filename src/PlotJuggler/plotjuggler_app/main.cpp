/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "mainwindow.h"
#include <iostream>
#include <QApplication>
#include <QThread>
#include <QCommandLineParser>
#include <QFontDatabase>
#include <QSettings>
#include <QSaveFile>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QDialog>
#include <QDesktopServices>
#include <QHostInfo>
#include <QStyleFactory>
#include <QMessageBox>
#include <QTimer>
#include <QCheckBox>

#include "PlotJuggler/transform_function.h"
#include "transforms/binary_filter.h"
#include "transforms/first_derivative.h"
#include "transforms/samples_count.h"
#include "transforms/scale_transform.h"
#include "transforms/moving_average_filter.h"
#include "transforms/moving_variance.h"
#include "transforms/moving_rms.h"
#ifdef PJ_HAS_PYTHON
#include "transforms/python_custom_function.h"
#endif
#include "transforms/outlier_removal.h"
#include "transforms/integral_transform.h"
#include "transforms/absolute_transform.h"
#include "transforms/time_since_previous_point.h"

#ifdef COMPILED_WITH_CATKIN
#include <ros/ros.h>
#endif
#ifdef COMPILED_WITH_AMENT
#include <string_view>

// Strip ROS 2 CLI arguments ("--ros-args ... [--]") from argv.
// Equivalent to rclcpp::remove_ros_arguments, but without pulling the rclcpp /
// rosidl typesupport stack into PlotJuggler just for one call.
static std::vector<std::string> RemoveRos2Arguments(int argc, char* argv[])
{
  std::vector<std::string> out;
  out.reserve(argc);
  bool in_ros_block = false;
  for (int i = 0; i < argc; ++i)
  {
    const std::string_view tok(argv[i]);
    if (!in_ros_block)
    {
      if (tok == "--ros-args")
      {
        in_ros_block = true;
        continue;
      }
      out.emplace_back(argv[i]);
    }
    else if (tok == "--")
    {
      in_ros_block = false;
    }
  }
  return out;
}
#endif

static QString VERSION_STRING = QStringLiteral(PJ_STUDIO_VERSION);

std::vector<std::string> MergeArguments(const std::vector<std::string>& args)
{
#ifdef PJ_DEFAULT_ARGS
  auto default_cmdline_args = QString(PJ_DEFAULT_ARGS).split(" ", PJ::SkipEmptyParts);

  std::vector<std::string> new_args;
  new_args.push_back(args.front());

  // Add the remain arguments, replacing escaped characters if necessary.
  // Escaping needed because some chars cannot be entered easily in the -DPJ_DEFAULT_ARGS
  // preprocessor directive
  //   _0x20_   -->   ' '   (space)
  //   _0x3b_   -->   ';'   (semicolon)
  for (auto cmdline_arg : default_cmdline_args)
  {
    // replace(const QString &before, const QString &after, Qt::CaseSensitivity cs =
    // Qt::CaseSensitive)
    cmdline_arg = cmdline_arg.replace("_0x20_", " ", Qt::CaseSensitive);
    cmdline_arg = cmdline_arg.replace("_0x3b_", ";", Qt::CaseSensitive);
    new_args.push_back(strdup(cmdline_arg.toLocal8Bit().data()));
  }

  // If an argument appears repeated, the second value overrides previous one.
  // Do this after adding default_cmdline_args so the command-line override default
  for (size_t i = 1; i < args.size(); ++i)
  {
    new_args.push_back(args[i]);
  }

  return new_args;

#else
  return args;
#endif
}

int main(int argc, char* argv[])
{
  std::vector<std::string> args;

#if !defined(COMPILED_WITH_CATKIN) && !defined(COMPILED_WITH_AMENT)
  for (int i = 0; i < argc; i++)
  {
    args.push_back(argv[i]);
  }
#elif defined(COMPILED_WITH_CATKIN)
  ros::removeROSArgs(argc, argv, args);
#elif defined(COMPILED_WITH_AMENT)
  args = RemoveRos2Arguments(argc, argv);
#endif

  args = MergeArguments(args);

  int new_argc = args.size();
  std::vector<char*> new_argv;
  for (int i = 0; i < new_argc; i++)
  {
    new_argv.push_back(args[i].data());
  }

  // Must be set before QApplication is constructed. Tells Qt to scale
  // widget metrics and QSS pixel values by the screen's scale factor,
  // so XWayland (which reports DPI=N*96 with dpr=1) renders identically
  // to native Wayland (DPI=96 dpr=N). Without this, Fusion metrics
  // auto-scale by DPI but QSS hardcoded `px` values don't, causing
  // inconsistent widget sizing in the AppImage.
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

  QApplication app(new_argc, new_argv.data());

  // Pin the Qt base style so the app's QSS paints on top of a known
  // palette. Without this, the AppImage inherits the host's Qt platform
  // theme (e.g. GTK3 dark on Ubuntu), causing unstyled widgets like the
  // menu bar to render with system-dark colors while QSS-covered widgets
  // stay light — the mixed-theme look users report.
  QApplication::setStyle(QStyleFactory::create("Fusion"));

  //-------------------------

  QCoreApplication::setOrganizationName("PlotJuggler");
  QCoreApplication::setApplicationName("io.plotjuggler.PlotJuggler");
  QSettings::setDefaultFormat(QSettings::IniFormat);

  QSettings settings;

  if (!settings.isWritable())
  {
    qDebug() << "ERROR: the file [" << settings.fileName()
             << "] is not writable. This may happen when you run PlotJuggler with sudo. "
                "Change the permissions of the file (\"sudo chmod 666 <file_name>\"on "
                "linux)";
  }

  app.setApplicationVersion(VERSION_STRING);

  //---------------------------
  TransformFactory::registerTransform<FirstDerivative>();
  TransformFactory::registerTransform<ScaleTransform>();
  TransformFactory::registerTransform<MovingAverageFilter>();
  TransformFactory::registerTransform<MovingRMS>();
  TransformFactory::registerTransform<OutlierRemovalFilter>();
  TransformFactory::registerTransform<IntegralTransform>();
  TransformFactory::registerTransform<AbsoluteTransform>();
  TransformFactory::registerTransform<TimeSincePreviousPointTranform>();
  TransformFactory::registerTransform<MovingVarianceFilter>();
  TransformFactory::registerTransform<SamplesCountFilter>();
  TransformFactory::registerTransform<BinaryFilter>();
  //---------------------------

  QCommandLineParser parser;
  parser.setApplicationDescription("PlotJuggler: the time series visualization"
                                   " tool that you deserve ");
  parser.addVersionOption();
  parser.addHelpOption();

  QCommandLineOption nosplash_option(QStringList() << "n"
                                                   << "nosplash",
                                     "Don't display the splashscreen");
  parser.addOption(nosplash_option);

  QCommandLineOption test_option(QStringList() << "t"
                                               << "test",
                                 "Generate test curves at startup");
  parser.addOption(test_option);

  QCommandLineOption loadfile_option(QStringList() << "d"
                                                   << "datafile",
                                     "Load a file containing data", "file_path");
  parser.addOption(loadfile_option);

  QCommandLineOption layout_option(QStringList() << "l"
                                                 << "layout",
                                   "Load a file containing the layout configuration", "file_path");
  parser.addOption(layout_option);

  QCommandLineOption publish_option(QStringList() << "p"
                                                  << "publish",
                                    "Automatically start publisher when loading the "
                                    "layout file");
  parser.addOption(publish_option);

  QCommandLineOption folder_option(QStringList() << "plugin_folders",
                                   "Add semicolon-separated list of folders where you "
                                   "should look "
                                   "for additional plugins.",
                                   "directory_paths");
  parser.addOption(folder_option);

  QCommandLineOption buffersize_option(
      QStringList() << "buffer_size",
      QCoreApplication::translate(
          "main", "Change the maximum size of the streaming buffer (minimum: 10 default: 60)"),
      QCoreApplication::translate("main", "seconds"));
  parser.addOption(buffersize_option);

  QCommandLineOption nogl_option(
      QStringList() << "disable_opengl",
      "Disable OpenGL display before starting the application. You can enable it again in the 'Preferences' menu.");
  parser.addOption(nogl_option);

  QCommandLineOption enabled_plugins_option(
      QStringList() << "enabled_plugins",
      "Limit the loaded plugins to ones in the semicolon-separated list", "name_list");
  parser.addOption(enabled_plugins_option);

  QCommandLineOption disabled_plugins_option(
      QStringList() << "disabled_plugins",
      "Do not load any of the plugins in the semicolon separated list", "name_list");
  parser.addOption(disabled_plugins_option);

  QCommandLineOption skin_path_option(
      QStringList() << "skin_path",
      "New \"skin\". Refer to the sample in [plotjuggler_app/resources/skin] path to folder");
  parser.addOption(skin_path_option);

  QCommandLineOption start_streamer(
      QStringList() << "start_streamer",
      "Automatically start a Streaming Plugin with the given file_name (no extension)");
  parser.addOption(start_streamer);

  QCommandLineOption window_title(QStringList() << "window_title", "Set the window title",
                                  "window_title");
  parser.addOption(window_title);

  QCommandLineOption auto_prefix_option("auto-prefix",
                                        "Automatically prefix each data file with its filename");
  parser.addOption(auto_prefix_option);

  QCommandLineOption update_health_option(QStringLiteral("update-health-file"),
                                          QStringLiteral("Updater health confirmation file"),
                                          QStringLiteral("absolute_path"));
  update_health_option.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(update_health_option);

  parser.process(*qApp);

  if (parser.isSet(publish_option) && !parser.isSet(layout_option))
  {
    std::cerr << "Option [ -p / --publish ] is invalid unless [ -l / --layout ] is used too."
              << std::endl;
    return -1;
  }

  if (parser.isSet(enabled_plugins_option) && parser.isSet(disabled_plugins_option))
  {
    std::cerr << "Option [ --enabled_plugins ] and [ --disabled_plugins ] can't be used together."
              << std::endl;
    return -1;
  }

  if (parser.isSet(nogl_option))
  {
    settings.setValue("Preferences::use_opengl", false);
  }

  if (parser.isSet(skin_path_option))
  {
    QDir path(parser.value(skin_path_option));
    if (!path.exists())
    {
      qDebug() << "Skin path [" << parser.value(skin_path_option) << "] not found";
      return -1;
    }
  }

  QIcon app_icon("://resources/rosplotjuggler_studio.png");
  QApplication::setWindowIcon(app_icon);

#ifdef PJ_HAS_PYTHON
  // Probe the embedded Python interpreter BEFORE constructing MainWindow, so
  // FunctionEditorWidget (built inside the MainWindow ctor) sees the correct
  // PythonCustomFunction::isAvailable() state when it decides whether to
  // enable / disable the Python radio buttons.
  const bool python_ok = PythonCustomFunction::probeAvailable();
  if (!python_ok)
  {
    qWarning() << "Embedded Python could not be initialized — Python custom "
                  "functions will be disabled for this session.";
  }
#endif

  MainWindow* window = new MainWindow(parser);
  window->show();

  if (parser.isSet(update_health_option))
  {
    const QString healthPath = QDir::cleanPath(parser.value(update_health_option));
    if (!QFileInfo(healthPath).isAbsolute())
    {
      qCritical() << "Updater health file path must be absolute";
      return 2;
    }
    QTimer::singleShot(0, window, [&app, healthPath]() {
      QSaveFile healthFile(healthPath);
      if (!healthFile.open(QIODevice::WriteOnly) ||
          healthFile.write("RosPlotJugglerStudio healthy\n") < 0 || !healthFile.commit())
      {
        qCritical() << "Unable to atomically write updater health file:" << healthPath;
        app.exit(3);
        return;
      }
      app.quit();
    });
    return app.exec();
  }

#ifdef PJ_HAS_PYTHON
  if (!python_ok)
  {
    // Show a one-time, dismissible warning after the main window is up so the
    // user immediately knows Python custom functions won't work on this host.
    const QString suppress_key = "PythonUnavailable.suppressWarning";
    if (!QSettings().value(suppress_key, false).toBool())
    {
      QTimer::singleShot(0, window, [window, suppress_key]() {
        QMessageBox box(window);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QObject::tr("Python disabled"));
        box.setText(QObject::tr("PlotJuggler could not initialize the embedded "
                                "Python interpreter."));
        box.setInformativeText(
            QObject::tr("Python custom functions are disabled for this session. Lua custom "
                        "functions remain available.\n\n"
                        "This usually means the Python standard library expected by this "
                        "build is not present on the host system (common when running an "
                        "AppImage on a distro with a different Python version)."));
        QCheckBox* dont_show = new QCheckBox(QObject::tr("Don't show this again"), &box);
        box.setCheckBox(dont_show);
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
        if (dont_show->isChecked())
        {
          QSettings().setValue(suppress_key, true);
        }
      });
    }
  }
#endif

  if (parser.isSet(start_streamer))
  {
    window->on_buttonStreamingStart_clicked();
  }

  QNetworkAccessManager manager_message;
  QObject::connect(
      &manager_message, &QNetworkAccessManager::finished, [window](QNetworkReply* reply) {
        if (reply->error())
        {
          qDebug() << "Telemetry reply error:" << reply->error() << reply->errorString();
          return;
        }
        qDebug() << "Telemetry reply received";
        QString answer = reply->readAll();
        QJsonDocument document = QJsonDocument::fromJson(answer.toUtf8());
        QJsonObject data = document.object();
        QString message = data["message"].toString();
        window->setStatusBarMessage(message);
      });

  // These are 100% anonymous requests; no personal data is sent.
  // We collect your statistics to improve PlotJuggler.
  // Create JSON payload
  QJsonObject payload;
  payload["user_id"] = QString::fromLatin1(QSysInfo::machineUniqueId());
  payload["os"] = QSysInfo::productType();
  payload["version"] = VERSION_STRING;
  payload["installation"] = QString(PJ_INSTALLATION);

  QJsonDocument doc(payload);
  QByteArray jsonData = doc.toJson();

  // Test DNS resolution first
  QHostInfo hostInfo = QHostInfo::fromName("app.plotjuggler.io");
  if (hostInfo.error() != QHostInfo::NoError)
  {
    qDebug() << "DNS lookup failed:" << hostInfo.errorString()
             << " Addresses found:" << hostInfo.addresses();
  }

  // Create network request
  QNetworkRequest request_message;
  request_message.setUrl(QUrl("https://app.plotjuggler.io/telemetry"));
  request_message.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  // Send POST request
  manager_message.post(request_message, jsonData);

  return app.exec();
}

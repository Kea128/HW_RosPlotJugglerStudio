# 上游基线

## PlotJuggler

- 项目：[`facontidavide/PlotJuggler`](https://github.com/facontidavide/PlotJuggler)
- 版本：3.17.2
- 固定提交：
  [`034a5cc919909aebb124e0dc1abbe2029c3eed0f`](https://github.com/facontidavide/PlotJuggler/commit/034a5cc919909aebb124e0dc1abbe2029c3eed0f)
- 本地目录：`src/PlotJuggler/`
- 恢复日期：2026-08-22

固定提交是内容身份；后续上游分支、标签或最新发布不能替代它。

## 本次恢复规则

1. 读取固定提交的 GitHub tree 元数据。
2. 仅检查本地为空或实际只有换行的文件。
3. 只有当路径在固定提交中是非空 blob 时，才从固定提交的 raw URL 恢复。
4. 下载后按 tree 中记录的字节数校验。
5. 不覆盖任何现有非空 Studio 源码，不从 `build/`、`install/` 或 `release/`
   反向生成源码。

## 从固定提交恢复的文件

以下 34 个本地 0 字节文件已恢复：

- `CMakeLists.txt`
- `plotjuggler_app/aboutdialog.ui`
- `plotjuggler_app/CMakeLists.txt`
- `plotjuggler_app/curvelist_panel.cpp`
- `plotjuggler_app/curvelist_panel.h`
- `plotjuggler_app/curve_tracker.cpp`
- `plotjuggler_app/curve_tracker.h`
- `plotjuggler_app/main.cpp`
- `plotjuggler_app/mainwindow.cpp`
- `plotjuggler_app/mainwindow.h`
- `plotjuggler_app/mainwindow.ui`
- `plotjuggler_app/plotjuggler.rc`
- `plotjuggler_app/plotwidget.cpp`
- `plotjuggler_app/plotwidget.h`
- `plotjuggler_app/plot_docker_toolbar.ui`
- `plotjuggler_app/preferences_dialog.cpp`
- `plotjuggler_app/preferences_dialog.ui`
- `plotjuggler_app/resource.qrc`
- `plotjuggler_app/cheatsheet/cheatsheet_dialog.ui`
- `plotjuggler_app/resources/stylesheet_dark.qss`
- `plotjuggler_app/resources/stylesheet_light.qss`
- `plotjuggler_app/resources/skin/about_window_body.html`
- `plotjuggler_app/transforms/function_editor_help.ui`
- `plotjuggler_app/transforms/python_custom_function.cpp`
- `plotjuggler_plugins/DataLoadCSV/dataload_csv.cpp`
- `plotjuggler_plugins/DataLoadMCAP/dataload_mcap.cpp`
- `plotjuggler_plugins/DataStreamMQTT/datastream_mqtt.cpp`
- `plotjuggler_plugins/DataStreamMQTT/datastream_mqtt.h`
- `plotjuggler_plugins/DataStreamMQTT/mqtt_client.cpp`
- `plotjuggler_plugins/DataStreamPlotJugglerBridge/CMakeLists.txt`
- `plotjuggler_plugins/DataStreamPlotJugglerBridge/websocket_client.h`
- `plotjuggler_plugins/DataStreamZMQ/datastream_zmq.cpp`
- `plotjuggler_plugins/ToolboxCSV/toolbox_csv.ui`
- `plotjuggler_plugins/VideoViewer/video_dialog.cpp`

另有 4 个上游文件本地仅含换行，按同一规则恢复：

- `plotjuggler.svg`
- `plotjuggler_app/resources/svg/reference_line.svg`
- `snap_core22/gui/plotjuggler.svg`
- `snap_core24/gui/plotjuggler.svg`

## 未从该上游恢复

- `.gitmodules` 和
  `datasamples/rosbag2_test/rosbag2_test_0.db3-wal` 在固定提交中本来就是
  0 字节，保持不变。
- `plotjuggler_plugins/DataLoadROSBag/` 下 5 个空文件在固定提交中不存在，
  因此不能冒充上游内容恢复；见 `docs/PROJECT_STATUS.md`。
- `STUDIO_VERSION`、Studio ruler/label 相关文件和 `single_ruler.svg` 不属于该
  固定提交，未使用上游内容覆盖。

## 同步要求

后续同步必须记录新的提交、来源和差异范围。合并上游前先保留 Studio 修改，
同步后重新执行源码完整性、配置、编译、测试和插件加载检查。

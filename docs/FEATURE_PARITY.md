# PlotJuggler 全功能兼容清单

本清单是发布门禁。`Preserved` 表示必须保持上游行为，`Enhanced` 表示保留后
增加能力。任何未验证项都不能宣称发布版本具备完整兼容性。

证据标记：

- `[S]`：固定上游或 Studio 源码存在，且本阶段检查过路径/构建声明；
- `[V]`：已在当前恢复后的源码上运行验证；
- 未标记：尚无足够源码或验证证据。

`[S]` 不等于功能可用，也不满足发布门禁。只有完成构建和行为验证后才可将
checkbox 改为 `[x]`；本阶段没有任何项目达到 `[V]`。

## Core application

- [ ] [S] Preserved: main window startup and shutdown
- [ ] [S] Preserved: command-line parser and all upstream arguments
- [ ] [S] Preserved: plugin discovery from default and extra folders
- [ ] Preserved: plugin enable/disable preferences
- [ ] Preserved: recent files
- [ ] Preserved: quick reload
- [ ] Preserved: toast notifications
- [ ] Preserved: crash diagnostics
- [ ] [S] Preserved: light theme
- [ ] [S] Preserved: dark theme
- [ ] Preserved: custom stylesheet loading
- [ ] Enhanced: RosPlotJuggler Studio branding
- [ ] Enhanced: high-density responsive shell

## File loaders

- [ ] [S] Preserved: CSV
- [ ] [S] Preserved: ULog
- [ ] [S] Preserved: MCAP
- [ ] [S] Preserved: Parquet
- [ ] Preserved: directory expansion from CLI
- [ ] Preserved: loader plugin configuration persistence
- [ ] Enhanced: multiple files in one session
- [ ] Enhanced: source identity and alignment
- [ ] [S] Enhanced: standalone ROS1 bag
- [ ] [S] Enhanced: standalone ROS2 SQLite3 bag
- [ ] [S] Enhanced: standalone ROS2 MCAP bag

以上 ROS bag 项仅表示 C++ loader、worker 协议解析、打包声明和单元测试源码已经
恢复；尚未使用真实 bag、安装树或 GUI 执行运行验证，因此不标记 `[V]`。

## Streaming

- [ ] [S] Preserved: MQTT
- [ ] [S] Preserved: WebSocket
- [ ] [S] Preserved: Foxglove Bridge
- [ ] [S] Preserved: PlotJuggler Bridge
- [ ] [S] Preserved: UDP
- [ ] [S] Preserved: Serial Port
- [ ] [S] Preserved: ZeroMQ
- [ ] [S] Preserved: ZCM
- [ ] [S] Preserved: sample streamer
- [ ] Preserved: start/pause streaming
- [ ] Preserved: buffer size
- [ ] Preserved: streaming animation/status
- [ ] Preserved: startup streamer CLI option

## Parsers

- [ ] Preserved: JSON
- [ ] Preserved: CBOR
- [ ] Preserved: BSON
- [ ] Preserved: MessagePack
- [ ] Preserved: Protobuf
- [ ] Preserved: IDL
- [ ] Preserved: ROS
- [ ] Preserved: DataTamer
- [ ] Preserved: Influx line protocol
- [ ] Preserved: parser option widgets
- [ ] Preserved: parser factory registration

## Plot workspace

- [ ] [S] Preserved: time-series plot
- [ ] [S] Preserved: XY plot
- [ ] Preserved: drag one curve
- [ ] Preserved: drag multiple curves
- [ ] Preserved: create XY plot from selected series
- [ ] Preserved: add tab
- [ ] Preserved: close tab
- [ ] Preserved: rename tab
- [ ] Preserved: reorder tab
- [ ] Preserved: horizontal split
- [ ] Preserved: vertical split
- [ ] Preserved: recursive split
- [ ] Preserved: close plot
- [ ] Preserved: rename plot
- [ ] Preserved: copy/paste plot
- [ ] Preserved: fullscreen plot
- [ ] Preserved: plot background selection
- [ ] Preserved: equal split sizing
- [ ] Enhanced: compact hover plot toolbar
- [ ] Enhanced: active plot selection context
- [ ] Enhanced: independent X/Y/cursor synchronization
- [ ] Enhanced: named synchronization groups

## Navigation

- [ ] Preserved: rectangle zoom
- [ ] Preserved: horizontal zoom
- [ ] Preserved: vertical zoom
- [ ] Preserved: wheel zoom
- [ ] Preserved: axis-only wheel zoom
- [ ] Preserved: mouse pan
- [ ] Preserved: swapped pan/zoom preference
- [ ] Preserved: zoom out
- [ ] Preserved: maximum zoom
- [ ] Preserved: undo/redo plot state
- [ ] Preserved: synchronized time range
- [ ] Enhanced: fit visible curves
- [ ] Enhanced: optional synchronized Y
- [ ] Enhanced: zoom history inspector

`linked_zoom_policy.h` 已随 app 测试目标接入并完成源码级单测，但尚无 GUI 行为
验证，因此本节不新增 `[V]`。

## Curves and legend

- [ ] Preserved: curve visibility toggle
- [ ] Preserved: compact legend
- [ ] Preserved: legend font size
- [ ] Preserved: legend position/state
- [ ] Preserved: curve color
- [ ] Preserved: curve width
- [ ] Preserved: curve style
- [ ] Preserved: points/scatter
- [ ] Preserved: curve rename
- [ ] Preserved: remove selected curves
- [ ] Preserved: vertical limits
- [ ] Preserved: statistics dialog
- [ ] Preserved: curve export
- [ ] Enhanced: fixed high-contrast color sequence
- [ ] Enhanced: property inspector curve list
- [ ] Enhanced: batch curve appearance editing

## Time and playback

- [ ] Preserved: global time tracker
- [ ] Preserved: tracker Shift interaction
- [ ] Preserved: relative time
- [ ] Preserved: absolute date/time
- [ ] Preserved: time offset
- [ ] Preserved: playback start
- [ ] Preserved: playback pause
- [ ] Preserved: playback step
- [ ] Preserved: playback rate
- [ ] Preserved: playback loop
- [ ] Preserved: streaming time behavior
- [ ] Enhanced: cursor A
- [ ] Enhanced: cursor B
- [ ] Enhanced: nearest-sample snapping
- [ ] Enhanced: free cursor movement
- [ ] Enhanced: Δt, Δvalue, slope
- [ ] Enhanced: draggable and sortable readings
- [ ] Enhanced: bookmarks and loop regions

Ruler metrics 与 tracker label layout 的纯逻辑源码和测试已恢复；这不证明 cursor
A/B、拖动、吸附或读数面板已经在应用运行时接通。

## Data browser

- [ ] Preserved: time-series tree
- [ ] Preserved: custom/derived series tree
- [ ] Preserved: text filtering
- [ ] Preserved: regular expression filtering
- [ ] Preserved: tree expansion/collapse
- [ ] Preserved: current tracker values
- [ ] Preserved: delete selected data
- [ ] Preserved: delete all data
- [ ] Enhanced: space-token fuzzy matching
- [ ] Enhanced: camelCase matching
- [ ] Enhanced: source/topic/type filters
- [ ] Enhanced: favorites
- [ ] Enhanced: lazy model/view tree
- [ ] Enhanced: multi-source grouping

## Transforms and tools

- [ ] [S] Preserved: built-in transforms
- [ ] [S] Preserved: transform editor
- [ ] [S] Preserved: function library
- [ ] [S] Preserved: Lua custom function
- [ ] [S] Preserved: Python custom function
- [ ] Preserved: multi-input function
- [ ] Preserved: transform save/load
- [ ] [S] Preserved: Quaternion toolbox
- [ ] [S] Preserved: FFT toolbox
- [ ] [S] Preserved: CSV toolbox
- [ ] Preserved: Mosaico toolbox when dependency is available
- [ ] Preserved: toolbox plugin discovery
- [ ] Enhanced: derivative template
- [ ] Enhanced: integral template
- [ ] Enhanced: arithmetic template
- [ ] Enhanced: resampling template
- [ ] Enhanced: delay and cross-correlation template
- [ ] Enhanced: processing dependency graph

## Publishers

- [ ] Preserved: publisher plugin discovery
- [ ] Preserved: dynamically generated publisher rows
- [ ] Preserved: StatePublisher tracker updates
- [ ] Preserved: ROS2 topic publisher when ROS plugin is present
- [ ] Enhanced: cursor A/B publication policy

## Layouts and projects

- [ ] Preserved: save PlotJuggler XML layout
- [ ] Preserved: load PlotJuggler XML layout
- [ ] Preserved: plugin XML state
- [ ] Preserved: curves and plot ranges
- [ ] Preserved: tabs and dock layout
- [ ] Preserved: transforms in layouts
- [ ] Enhanced: `.rspj` project format
- [ ] Enhanced: multiple source paths and fingerprints
- [ ] Enhanced: missing source rebinding
- [ ] Enhanced: autosave and crash recovery
- [ ] Enhanced: schema migration
- [ ] Enhanced: export compatible upstream XML

## Packaging and platforms

- [ ] Preserved: Linux native build
- [ ] Preserved: Linux AppImage
- [x] [V] Preserved: Windows native build
- [ ] Preserved: Windows installer
- [ ] Preserved: macOS native build
- [ ] Preserved: ROS2 package build
- [x] [V] Preserved: non-ROS build
- [ ] Enhanced: plugin diagnostics page
- [x] [V] Enhanced: reproducible dependency manifest

Windows UCRT64 的非 ROS 构建、86 项 CTest、安装树、便携 smoke test、ZIP、
双 SHA-256 文件和 schema v1 文件清单已在当前机器实际生成。此验证不包含因
内存限制显式关闭的可选 Mosaico 插件，也不等同于 Windows installer 或完整 GUI
验收。

## Release rule

A release is “PlotJuggler feature complete” only when:

1. every `Preserved` item is checked on at least one supported platform;
2. loaders, parsers, streamers and toolboxes have plugin-load smoke tests;
3. upstream XML fixtures open without destructive migration;
4. no upstream command-line argument is silently ignored;
5. unsupported optional dependencies are reported clearly, not hidden;
6. the exact upstream commit and dependency versions are published.

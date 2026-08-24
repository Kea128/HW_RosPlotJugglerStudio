# RosPlotJuggler Studio 产品与架构设计

## 1. 项目定位

RosPlotJuggler Studio 是 PlotJuggler 的直接二次开发版本。它完整保留
PlotJuggler 的数据加载、数据流、解析器、绘图、布局、回放、发布、变换和插件
能力，在此基础上重点增强：

1. 离线 ROS1/ROS2 bag 分析；
2. 多 bag 会话与跨 bag 对比；
3. 专业、高密度但不拥挤的工作台；
4. 双标尺和工程测量；
5. 大规模信号检索与批量操作；
6. 可复用分析项目和处理流程；
7. Windows、Linux 的可靠构建和发布。

## 2. 不可破坏的兼容性原则

### 2.1 功能完整性

不允许通过删除 PlotJuggler 原有功能来降低重构难度。每个发布版本必须通过
功能完整性清单，覆盖：

- 原有文件加载器；
- 原有 streaming 插件；
- 原有消息解析器；
- 原有 Toolbox 和 Transform；
- 时序图、XY 图、多 tab、多分栏；
- 布局保存和恢复；
- 播放、循环、时间 tracker；
- 发布器和状态发布；
- Lua 与 Python 自定义函数；
- CLI 参数、插件目录和启动布局；
- 浅色/深色主题和偏好设置。

### 2.2 插件 ABI

- 保留 `PJ::DataLoader`、`PJ::DataStreamer`、`PJ::MessageParserFactory`、
  `PJ::StatePublisher`、`PJ::ToolboxPlugin`、`PJ::TransformFunction`。
- 不随意修改现有 IID、目标名和插件发现规则。
- 新接口优先采用可选扩展接口或能力查询。
- 必须修改 ABI 时，提升插件 API 版本并提供适配层。

### 2.3 数据和布局

- 继续读取 PlotJuggler XML 布局。
- 原 XML 布局中的曲线、分栏、范围、变换和插件状态必须可恢复。
- 新项目格式不得覆盖旧布局；使用新的扩展名和 schema version。
- 导出 PlotJuggler 兼容布局时，对扩展字段做安全降级。

### 2.4 许可证

上游 PlotJuggler 使用 MPL-2.0。修改的 MPL 文件继续按 MPL-2.0 分发，
保留上游版权和许可证声明。新增独立文件默认也采用 MPL-2.0，以减少分发歧义。
第三方库继续遵守其各自许可证。

## 3. 上游功能基线

### 3.1 数据输入

必须保留上游支持的文件和数据流：

- CSV；
- ULog；
- MCAP；
- Parquet；
- ROS bag 插件；
- MQTT；
- WebSocket；
- Foxglove Bridge；
- PlotJuggler Bridge；
- UDP；
- Serial Port；
- ZeroMQ；
- ZCM；
- 示例数据流插件。

### 3.2 解析器

必须保留：

- JSON；
- CBOR；
- BSON；
- MessagePack；
- Protobuf；
- IDL；
- ROS；
- DataTamer；
- Influx line protocol；
- 已注册的自定义 parser factory。

### 3.3 绘图与导航

必须保留：

- 时序曲线；
- XY 图；
- 多曲线拖放；
- 多 tab；
- 水平/垂直递归分栏；
- 图表重命名、复制、粘贴、删除；
- 框选缩放、滚轮缩放、平移；
- 单轴缩放；
- 图例显隐和字号调整；
- 网格、参考线、时间格式；
- 全局 tracker；
- 播放、暂停、循环、步进和倍率；
- 同步时间范围；
- 曲线统计、导出和截图；
- Undo/Redo 视图状态。

### 3.4 数据处理

必须保留：

- 内置 transform；
- Lua 自定义函数；
- Python 自定义函数；
- 多输入单输出处理；
- Quaternion 工具；
- FFT 工具；
- CSV toolbox；
- 变换库和变换状态持久化。

### 3.5 平台能力

必须保留：

- 动态插件发现和禁用；
- 最近文件和快速重载；
- 布局模板；
- 主题和自定义样式；
- 命令行启动参数；
- toast 通知；
- 崩溃诊断；
- Windows、Linux、macOS 的上游可构建性。

## 4. 新增能力

## 4.1 无 ROS 环境的离线 bag

官方 ROS2 loader 依赖 `rclcpp`、`rosbag2_transport` 和 ROS2 类型支持。
新版本增加可选的 Standalone Bag Loader：

- 不替换官方 ROS2 loader；
- 与官方插件并存，由能力和文件类型决定优先级；
- 支持 ROS1 `.bag`；
- 支持 ROS2 SQLite3/MCAP 目录；
- 不要求 ROS master、DDS 网络或运行中的 ROS 节点；
- 自带常见消息定义；
- 支持用户导入额外 `.msg/.idl` 类型包；
- 未知类型只影响对应 topic，不中止整个 bag；
- bag 索引和字段发现放到后台线程；
- 支持取消、进度和增量显示。

实现候选：

1. 独立 Python worker，复用 `rosbags`，通过本地 IPC 输出列式块；
2. 新 C++ loader，复用 MCAP/SQLite 和 introspection；
3. 第一阶段采用 Python worker，稳定后评估 C++ 热路径。

## 4.2 多 bag 会话

- 一次打开或追加多个 bag；
- 每个 bag 有 source id、颜色和时间偏移；
- 支持绝对时间、相对起点和手动对齐；
- 同名信号按 source 分组；
- 搜索结果可限定 bag；
- 关闭一个 bag 不影响其余数据；
- 项目保存 bag 路径、指纹和重定位规则。

## 4.3 高级搜索

- 空格分词；
- 驼峰拆分；
- 拼音不作为第一阶段硬依赖；
- topic、字段、类型、bag 名联合匹配；
- `steer Pini` 可匹配 `steerPinion`；
- 支持 `bag:drive topic:/vehicle type:float` 过滤语法；
- 大于 10,000 字段时使用 model/proxy 和延迟过滤；
- 搜索结果支持多选、批量拖放、收藏和最近使用。

## 4.4 双标尺与测量

- Shift + 左键放置 A；
- 再次操作放置 B；
- 普通拖动吸附最近采样；
- Shift 拖动自由停留，但仍标记最近采样；
- 真实采样位置使用细小空心圆；
- 隐藏曲线不显示交点和读数；
- 显示绝对时间、A/B 值、Δt、Δvalue、斜率；
- 多曲线读数支持名称、数值、差值排序；
- 读数面板可拖动和复位；
- X、Y、标尺同步分别控制；
- 标尺移动可驱动 StatePublisher，但默认不强制发布。

## 4.5 处理工作流

在保留 Lua/Python Function Editor 的前提下增加：

- 微分；
- 积分；
- 加减乘除；
- 移动平均；
- 低通/高通；
- 重采样；
- 时间延迟；
- 互相关时延估计；
- 单位换算；
- 条件掩码；
- 多输入 Python 模板；
- 处理图（processing graph）；
- 配方版本和输出依赖；
- 输入改变后的增量重算；
- 算法执行时间和缓存命中率。

## 4.6 分析项目

新增 `.rspj` 项目格式，采用带版本号的 JSON：

- 数据源；
- bag 指纹和路径；
- PlotJuggler XML 布局嵌入或引用；
- tabs 和 splitter；
- 曲线引用和样式；
- 同步组；
- 标尺；
- 处理配方；
- 注释和书签；
- UI 面板状态；
- 最近活动视图。

项目格式支持迁移器：

```text
schema_version 1 -> 2 -> 3
```

加载失败必须保留原文件，不做原地破坏性升级。

## 5. 页面设计

## 5.1 总体结构

```text
┌ 菜单栏：File Edit Data View Analysis Tools Help ───────────────────────┐
├ 应用工具条：Open Append Reload Layout Stream Publish Transform         ┤
├──────────────┬─────────────────────────────────────────┬───────────────┤
│ Data Browser │ Display tabs / recursive plot workspace │ Inspector     │
│              │ ┌ Plot header ────────────────────────┐ │               │
│ search       │ │ plot + compact legend               │ │ selection     │
│ source tree  │ └─────────────────────────────────────┘ │ curve style   │
│ derived      │ ┌─────────────────────────────────────┐ │ measurements  │
│ favorites    │ │ synchronized plot                  │ │ processing    │
│              │ └─────────────────────────────────────┘ │               │
├──────────────┴─────────────────────────────────────────┴───────────────┤
│ Page controls: X sync | Y sync | Cursor sync | Legend | Grid | Time    │
├ Timeline: play | rate | step | range | tracker | A | B | Δt ──────────┤
└ Status: source count | curves | render FPS | task progress | messages ─┘
```

## 5.2 作用域

控件按作用域布置，防止每个图重复堆按钮：

- 应用级：加载、流、发布、布局、项目；
- 页面级：同步、时间格式、网格、图例、标尺；
- 图表级：标题、最大化、分栏、关闭；
- 曲线级：颜色、样式、单位、变换、删除。

## 5.3 数据浏览器

默认宽度 280 px，可折叠：

- 顶部统一搜索；
- Source / Timeseries / Derived / Favorites 四个视图；
- tree row 24–26 px；
- 值列按 tracker 位置更新；
- 只刷新可见节点；
- 支持拖动列宽；
- 多选后显示批量操作条；
- bag/topic/field 使用不同层级，但避免彩色图标泛滥。

长期将 `QTreeWidget` 迁移为：

```text
QTreeView
  SignalTreeModel
  SignalFilterProxyModel
  SignalValueDelegate
```

## 5.4 图表

- 标题条高度 22–24 px；
- 默认只显示标题、曲线数和更多；
- 分栏、最大化、关闭在活动/hover 时出现；
- active plot 使用 1 px 强调边框，不改变布局尺寸；
- 轴刻度保证可见；
- 绝对时间按缩放级别显示日期、秒和毫秒；
- 曲线笔使用 cosmetic width，缩放后视觉宽度不变；
- line+points 样式突出采样点但不遮挡曲线；
- legend 默认紧凑；
- 超过 8 条曲线时 Inspector 承担完整曲线列表；
- 空图只显示一行拖放提示。

## 5.5 Inspector

右侧可完全折叠，默认不强制占空间：

- Properties：当前 plot/curve；
- Measurement：双标尺和排序；
- Processing：变换参数和依赖；
- Session：数据源、缓存和任务；
- Diagnostics：渲染耗时、点数和插件状态。

## 5.6 Timeline

- 保留原 playback 全功能；
- tracker 滑块支持绝对时间；
- A/B 标尺时刻和 Δt 始终可见；
- 支持书签；
- 支持循环区间；
- 支持按消息跳转；
- 多 bag 可切换绝对时间、相对时间、对齐时间。

## 5.7 视觉系统

浅色主题默认令牌：

- canvas：`#F4F6F8`
- surface：`#FFFFFF`
- plot：`#FCFDFE`
- text：`#172033`
- secondary text：`#667085`
- border：`#D9E0EA`
- accent：`#4867E8`
- hover：`#EEF2FF`
- cursor A：`#E83E8C`
- cursor B：`#0097A7`
- error：`#D92D20`

规范：

- 不使用渐变；
- 不使用大面积阴影；
- 不使用卡片墙；
- 图标只表达动作，不做装饰；
- 默认字体 9–10 pt；
- 间距使用 4/8/12/16；
- 所有状态不能只依赖颜色。

深色主题必须使用同一语义令牌，而不是独立手工拼凑。

## 6. 技术架构

## 6.1 保留上游分层

```text
plotjuggler_base       data model and plugin interfaces
plotjuggler_qwt        rendering
plotjuggler_app        shell and coordination
plotjuggler_plugins    builtin plugins
ROS plugins            external ROS integration
```

## 6.2 新增模块

优先新增独立文件，减少对巨大 `MainWindow` 的继续堆叠：

```text
plotjuggler_app/studio/
  app_commands.*
  design_tokens.*
  data_browser.*
  property_inspector.*
  measurement_controller.*
  timeline_controller.*
  workspace_serializer.*
  project_document.*
  notification_center.*

plotjuggler_base/include/PlotJuggler/studio/
  capabilities.h
  measurement_types.h
  project_schema.h
```

## 6.3 Command 系统

每个用户动作注册为 command：

- id；
- 文本；
- 图标；
- shortcut；
- enable predicate；
- check state；
- scope；
- handler。

菜单、工具栏、右键菜单引用同一 command，避免状态不一致。

## 6.4 选择模型

建立统一 `SelectionContext`：

- active tab；
- active plot；
- selected curves；
- active source；
- active cursor；
- focused tool。

Inspector 和命令状态订阅选择模型，不读取其他 Widget 私有字段。

## 6.5 测量模型

双标尺状态属于页面，不属于单个 plot：

```text
MeasurementState
  enabled
  cursor_count
  cursor_a_time
  cursor_b_time
  snap_mode
  sort_mode
  card_positions
```

每个 plot 使用自身可见曲线计算交点，页面只同步时间。

## 6.6 后台任务

统一加载、索引、处理任务：

- cancellable task；
- progress；
- generation id；
- stale result rejection；
- structured error；
- UI 合并刷新；
- 不允许 worker 直接操作 QWidget/QwtPlot。

## 6.7 大数据绘制

- 保留 PlotJuggler/Qwt 当前性能路径；
- 按可见像素宽度选择 downsampling；
- min/max envelope 保留峰值；
- 缩放停止后可进行高质量重绘；
- 数据和绘制缓存分离；
- 标尺查找使用二分；
- 值树只更新可见行；
- 给出 render FPS、visible points、source points 指标。

## 7. 与上游同步策略

源码目录保持可识别的 upstream 结构。每次同步：

1. 记录 upstream commit；
2. 导入 upstream 更新；
3. 先解决基础库和插件 ABI；
4. 运行上游测试；
5. 运行 Studio compatibility tests；
6. 更新 `UPSTREAM.md`；
7. 禁止把本地功能变更混入纯 upstream sync 提交。

优先通过新增文件和窄接口扩展，减少长期 merge 冲突。

## 8. 实施阶段

## Phase 0：基线和可构建性

- 固定两个上游 commit；
- 建立 MPL 和第三方 notices；
- 建立 Windows/Linux 构建说明；
- 运行上游测试；
- 生成插件清单；
- 建立功能完整性测试清单。

验收：原始功能可构建、可启动、插件可发现。

## Phase 1：品牌与视觉系统

- 新名称和关于页面；
- 新 light/dark 语义令牌；
- 菜单、工具条、树、tab、plot chrome 统一；
- 不改变业务行为；
- 截图基准。

验收：功能无回归，1366×768 和 1920×1080 可用。

## Phase 2：AppShell 和 Command

- 拆解 `MainWindow` 高频动作；
- 建立 command registry；
- 数据浏览器和 Inspector 边界；
- 页面级工具条；
- panel 显隐和状态恢复。

验收：原菜单和快捷键仍可用，状态来源唯一。

## Phase 3：Workspace

- 22–24 px plot header；
- hover disclosure；
- equal split；
- active plot；
- X/Y/标尺独立同步；
- 单图最大化；
- 完整 splitter 状态。

验收：1–9 图布局均可操作，无尺寸漂移。

## Phase 4：数据浏览器

- model/view；
- fuzzy search；
- 多源分组；
- 批量拖放；
- Favorites；
- tracker value column；
- 10k–100k 字段性能。

验收：10,000 字段逐键搜索无明显阻塞。

## Phase 5：双标尺

- 页面级 MeasurementState；
- A/B 和吸附；
- 最近采样标记；
- Δt/Δvalue/斜率；
- Inspector 排序和导出；
- 跨 plot 同步。

验收：20 曲线拖动目标 30–60 FPS。

## Phase 6：Standalone ROS

- Python worker；
- ROS1/ROS2 catalog；
- 多 bag；
- 未知类型隔离；
- 取消和进度；
- source time alignment。

验收：无 ROS 环境可打开测试 bags。

## Phase 7：处理和项目

- processing graph；
- 模板；
- `.rspj`；
- unresolved source rebind；
- schema migration；
- autosave/recovery。

验收：完整会话可关闭后无损恢复。

## Phase 8：性能和发布

- benchmark；
- leak test；
- crash recovery；
- Windows/Linux installer；
- plugin diagnostics；
- 文档和快捷键面板。

验收：功能矩阵全部通过。

## 9. 测试策略

### 9.1 上游兼容

- 上游 unit tests；
- layout fixtures；
- plugin loading smoke tests；
- CLI tests；
- sample streaming tests。

### 9.2 新功能

- 模型和搜索单元测试；
- MeasurementState 测试；
- splitter/property tests；
- project migration tests；
- multi-bag integration tests；
- unknown ROS type tests；
- cancellation/stale result tests。

### 9.3 视觉回归

固定场景：

- 空会话；
- 单图三曲线；
- 四分栏；
- 20 曲线 legend；
- 双标尺；
- Inspector 打开/关闭；
- 1366×768；
- 1920×1080；
- 100%/125%/150% DPI；
- light/dark。

### 9.4 性能基准

- 1/10/100/1000 curves；
- 100k/1m/10m samples；
- 10k/100k fields；
- tracker drag；
- zoom/pan；
- file indexing；
- project restore。

基准退化超过阈值时阻止发布。

## 10. 第一轮改进点

第一轮只做低风险、立即可见的改进：

1. 更名为 RosPlotJuggler Studio；
2. 更新 light palette；
3. 降低 tab、按钮和树行的无效高度；
4. 降低重边框对视觉的干扰；
5. 强化 active、hover、checked 的层级；
6. 保持原插件、菜单、快捷键和布局逻辑不变；
7. 建立功能完整性文档；
8. 在完成构建基线前，不重写 MainWindow。

这样可先获得稳定的新视觉基线，再进入结构性改造。

# 2026-09-29 外部代码审查报告核查

## 基线与结论

- 源码基线：`5633e7e`，分支 `main`。
- 输入：外部报告 `ukui-liquid-desktop-code-review.md`，对应 Antigravity 任务 `7dcd6f94-b42b-4cfd-813a-88af869483cb`。
- 本轮工作：核查报告中的 23 项问题及相关调用路径，制定整改顺序。仅新增本文档，未修改、构建、安装或重启应用，未推送。
- 核查方式为静态源码与调用路径检查；没有进行性能采样、GPU 上下文故障注入或远端 CI 运行状态查询。本文中的性能风险不是实测结果，也不代表重新逐文件审计了全部源码。

报告抓住了真实方向：部分文件操作、目录统计、系统采样和材质准备仍在 GUI 线程。但“7 项 Critical”“默认遥测外发”“持续轮询造成 X Server 连接数激增”“CI 完全缺失”等结论不能直接沿用。当前证据不足以把所列问题统一定为 P0 紧急故障；应按实际可达路径、触发频率和用户影响排序。

## 23 项逐项核查

“成立”指代码事实成立，不表示报告中的卡顿时长或严重程度已测得。下表优先级为本次建议：P1 优先整改，P2 后续修复或测量，P3 维护优化；不成立的原指控不安排机械修改。

| 编号 | 判断 | 当前源码证据与修正 | 建议 |
|---|---|---|---|
| CR-01 同步进程 | 成立，耗时表述需修正 | [WallpaperBackdrop.cpp](../apps/ukui-panel-liquid/src/WallpaperBackdrop.cpp) 的 `setting()` 等待 1200ms，超时后还有无参数 `waitForFinished()`；`reload()` 条件性读取配置，并非每次固定启动 3 次。Fences 的目录解析、MIME 查询及回收站操作也有同步调用。超时参数不能作为实际卡顿时长或全路径严格上限。 | P1：优先活跃 UI 路径，逐条核对线程和调用者。 |
| CR-02 X11 连接 | 部分成立，轮询结论错误 | [StartMenu.cpp](../apps/ukui-kaishicaidan-v2/src/StartMenu.cpp) 的三个查询辅助函数仍分别开关连接；但 `setupActiveAppTracking()` 已用持久连接及 `QSocketNotifier` 监听属性，100ms 定时器为单次防抖，窗口 ID 未变直接返回。[PanelNative.cpp](../apps/ukui-panel-liquid/src/PanelNative.cpp) 也有短连接，但不能推定每帧调用。 | P2：复用同线程连接、缓存 atom。不是连接泄漏，不能称持续连接数激增。 |
| CR-03 主线程 /proc | 成立，频率夸大 | [SystemMonitor.cpp](../apps/ukui-fences/src/SystemMonitor.cpp) 的 `refreshStats()` / `readProcessSnapshot()` 同步采样。不可见时跳过后续刷新；重采样每 6 tick，默认间隔 5 秒，通常约 30 秒一次，首屏另有采样，紧凑模式跳过进程列表。实际进程读取为 stat、exe 链接及失败时的 cmdline，不能泛称每轮读取所有 status/io。 | P1：单个后台采集任务，合并请求，保留可见性和采样频率限制。 |
| CR-04 Popup 未复用 GL | 设计取舍，非 Critical | [共享气泡说明](../shared/liquid-popup/README.md) 明确采用小面积 CPU 材质缓存。各应用复用的是同一 popup 模块；没有引用 GL 库不等于没有公共模块。 | P3：先共享参数、背景来源和测试标准，依据测量决定是否增加 GL 后端。 |
| CR-05 同步截图 | 成立，时长待测 | [LiquidPopup.cpp](../shared/liquid-popup/src/LiquidPopup.cpp) 在打开时抓取局部矩形并生成材质，动画复用缓存，不逐帧抓屏。已有 `setBackdropProvider()`，当前仓库仅测试注入，应用尚未接入。报告的 20–100ms+ 没有本轮测量支持。 | P2：分别测截图和材质生成；允许壁纸语义的场景接入缓存。 |
| CR-06 目录大小 | 成立，实际触发更早 | [DesktopIcon.cpp](../apps/ukui-fences/src/DesktopIcon.cpp) 构造和 `setItem()` 即调用 `updateToolTip()`；其中全量列直接子项并递归统计大小，不必等悬停。约 500 个文件的截断不限制空目录数量、网络 I/O 等待或总耗时。 | P1：基础提示即时显示，详细统计按需后台执行、缓存并限制并发。 |
| CR-07 默认遥测/HTTP | 原指控不成立 | [SystemMonitor.cpp](../apps/ukui-fences/src/SystemMonitor.cpp) 诊断由按钮/菜单调用 `startDiagnosis()`，未发现初始化自动发送路径；请求校验 HTTPS、host 和 userinfo，curl 限制 `--proto =https`。点击诊断仍会发送系统/进程上下文，不能说完全无外发。 | 保留主动触发与 HTTPS；界面应明确发送范围。本结论不等同于完整隐私审计。 |
| CR-08 CPU 模糊 | 部分成立 | [LiquidOpticsRenderer.cpp](../shared/liquid-glass/src/LiquidOpticsRenderer.cpp) 与 [FenceGlassRenderer.cpp](../apps/ukui-fences/src/FenceGlassRenderer.cpp) 先缩小图像再做 6 pass；Fences 缓存 body/clear，壁纸改变时失效。GPU 路径也使用 CPU 预处理，并非只有 fallback。不是每帧对完整 4K 图做卷积。 | P2：测首次和换壁纸；后台 QImage 预处理、限制缓存；不直接大改 GPU。 |
| CR-09 RecentFiles XBEL | 解析缺陷成立，当前影响有限 | [RecentFiles.cpp](../apps/ukui-kaishicaidan-v2/src/RecentFiles.cpp) 将 modified 属性当元素，MIME 属性也误读，file URI 未解码，读取未按时间排序。但当前应用无 `RecentFiles::recent()` 调用；外部调用的是 `timeAgoString()`。XBEL 文件顺序本身不能保证“最老在前”。 | P2：接入该功能前修复解析；单独修复实际使用的时间文案。 |
| CR-10 GL 析构 | 风险待验证，原修法不完整 | [NextKdeGlassView.cpp](../apps/ukui-kaishicaidan-v2/src/NextKdeGlassView.cpp) 在 makeCurrent 失败时仍 reset；共享渲染器也用相同结构，报告却将后者列为完全正确。是否产生泄漏/崩溃需结合 Qt 资源 guard 与上下文生命周期验证。 | P2：做失败路径验证。仅把 reset 放进 if 不够，unique_ptr 成员仍会自动析构。 |
| CR-11 QAction 生命周期 | 原指控不成立 | [LiquidPopup.cpp](../shared/liquid-popup/src/LiquidPopup.cpp) 保存 `QPointer<QAction>`，`restoreIcons()` 判空后恢复；对象销毁自动置空。已销毁对象无需恢复，不是由此产生悬垂访问。 | 不增加冗余 destroyed 监听；动态移出、复用 action 可另列交互测试。 |
| CR-12 文件复制 | 部分已修，拖放仍成立 | [FileClipboard.cpp](../apps/ukui-fences/src/FileClipboard.cpp) 的 `pasteFilesToDirectoryAsync()` 已在线程中执行粘贴；但 `DesktopCanvas::dropEvent` 经辅助函数、`FenceWidget` 外部拖放仍直接调用同步 `transferPath()`；DesktopIcon 目录拖放另有同步实现。 | P1：统一异步文件任务，不能因为已有异步粘贴就关闭此项。 |
| CR-13 desktop 全量解析 | 成立 | [StartMenu.cpp](../apps/ukui-kaishicaidan-v2/src/StartMenu.cpp) 的 watcher 每个事件创建新的 singleShot，未重启同一个防抖定时器；事件突发可能多次 `rebuildAppList()`，同步解析并重建界面。 | P1：先真正合并事件，再后台解析纯数据，主线程应用最新结果。 |
| CR-14 无 CI | 不成立 | 已跟踪的 [fences.yml](../.github/workflows/fences.yml) 包含 push/PR 构建、Python、Xvfb/HiDPI 等回归与测试产物。 | P2：补开始菜单、面板构建覆盖和真机验证；没有查询远端结果，不声称 CI 已绿。 |
| CR-15 巨型 DesktopCanvas | 成立，维护债 | [DesktopCanvas.cpp](../apps/ukui-fences/src/DesktopCanvas.cpp) 承担布局、图标、输入、文件操作、壁纸等多职责。 | P3：随文件任务和壁纸改造逐步提取，不一次性拆完。 |
| CR-16 菜单硬编码 | 部分成立，颜色指控过时 | [LiquidPopup.cpp](../shared/liquid-popup/src/LiquidPopup.cpp) 选中态已用 palette(highlight)/palette(highlighted-text)，已有 QProxyStyle 绘制菜单符号；padding 等尺寸仍固定，为对号和箭头预留位置。 | P3：按需要参数化间距；不能把报告中的 #406080 当成当前故障原因。 |
| CR-17 动画进度重置 | 成立 | [LiquidPopup.cpp](../shared/liquid-popup/src/LiquidPopup.cpp) 的 `Shell::openAt()` 停止当前动画后从 0 开始，`dismiss()` 从当前进度退回，快速反向可能跳变。 | P2：同位置、同内容可连续反转；位置/内容变化仍需刷新材质。 |
| CR-18 Xext SONAME | 可移植性取舍 | [PanelNative.cpp](../apps/ukui-panel-liquid/src/PanelNative.cpp) 使用 `libXext.so.6`，resolve 失败退出相关路径。当前没有 ABI 不兼容的复现证据。 | P3：明确支持平台与 fallback；REQUIRED 链接会将可选依赖变成强制依赖。 |
| CR-19 Strut 坐标 | 成立，原修法不充分 | [TaskbarDetector.cpp](../apps/ukui-kaishicaidan-v2/src/TaskbarDetector.cpp) 仅在真实面板检测失败后走 strut 降级，但混合 X11 根窗口像素与 Qt 屏幕坐标；取首个有 strut 的窗口，未筛选 ukui-panel；硬选主屏且范围端点缺少 inclusive +1。 | P2：按目标面板/屏幕做物理到逻辑映射，覆盖多屏偏移与不同缩放，不能简单全部除 DPR。 |
| CR-20 notifier 悬垂 | 不成立 | [KeyInterceptor.cpp](../apps/ukui-kaishicaidan-v2/src/KeyInterceptor.cpp) 在 deleteLater 后紧接着清空指针，之前禁用 notifier；处理事件还有 display/active 守卫。 | 不为不存在的“未清空指针”做修改。 |
| CR-21 每帧亮度昂贵 | 性能结论缺证据 | [NextKdeGlassView.cpp](../apps/ukui-kaishicaidan-v2/src/NextKdeGlassView.cpp) 的 `luminanceAt()` 每次仅在已有 m_image 上取 3×5 共 15 个点，没有重新截图或全图扫描。 | P3：profile 显示占比明显后再缓存；避免增加失效管理复杂度。 |
| CR-22 malloc_trim 频繁 | 频率指控不成立 | [DesktopCanvas.cpp](../apps/ukui-fences/src/DesktopCanvas.cpp) 中释放辅助函数只在智能空间实际删除后延迟调用；贴边收起不是删除，不逐帧执行。 | P3：只有实测删除卡顿再处理；不能据此解释持续尖峰。 |
| CR-23 inherits 字符串 | 代码成立，收益待测 | [PanelController.cpp](../apps/ukui-panel-liquid/src/PanelController.cpp) 在相关输入事件中向上找 UKUIPanel。动态属性读取也有代价，换写法不保证更快。 | P3：需要时缓存面板 QPointer，保留销毁/重建失效处理。 |

## 报告之外需要补入的事项

1. **同步文件路径尚未统一。** 桌面、分区、文件夹图标的拖放分别走不同路径，回收站也仍有同步进程。修复应覆盖所有入口并保留复制/移动语义，不能只搜索后替换 `execute()`。异步粘贴退出时使用 `worker->wait()`，后台 I/O 卡住可能拖延退出；需要单独设计退出和取消策略。
2. **进程启动也可能阻塞。** 系统监视 AI 请求虽然异步读取网络输出，启动时仍 `waitForStarted()`（默认超时 30 秒，不表示正常启动会耗时 30 秒）。智能空间知识查询、开始菜单辅助启动也有显式等待。后续清单应同时查 `waitForStarted`、`waitForFinished`、同步文件 I/O，并核实线程。
3. **壁纸加载包含解码。** 面板 `WallpaperBackdrop::reload()` 不仅调用 gsettings，还同步 `QImage(path)`；仅把配置读取改异步仍可能保留大图解码卡顿。后台解码结果应带请求版本，防止快速换壁纸后旧图覆盖新图。
4. **真实公共化机会是已有渲染器重复。** Fences 的 `FenceGlassRenderer` 与 shared `LiquidOpticsRenderer` 存在相似模糊、形状场和 GL 管线，并同时纳入 Fences 构建；开始菜单也有自己的后端。应先比对适配差异再提取公共实现，比单凭 popup 未依赖 GL 就强制替换更合理。
5. **最近应用时间文案存在活跃问题。** `RecentFiles::timeAgoString()` 把 1 天至不足 7 天全部显示为“昨天”；该方法在当前开始菜单中确实使用。应按本地日历日处理“昨天”，其余显示“几天前”，并覆盖未来时间/时区边界。

## 推荐实施顺序与验收

### 第一批：直接缩短 GUI 阻塞路径

- 统一文件任务模块，首先覆盖三类拖放与回收站，再复用已有粘贴入口。后台只处理文件数据，GUI 负责进度、布局与提示。保留重名处理、禁止自复制、移动失败保留源文件、剪贴板后续变化不覆盖等语义。
- 目录 tooltip 改为懒加载：创建图标不递归扫描。统计限并发、可取消、按路径和变更状态失效；返回时验证对象仍存在且路径未变。网络文件系统单次读取可能阻塞，不能将普通线程的“取消标志”等同于可强制中断任意 I/O。
- 开始菜单用可重启的单次定时器合并 watcher 事件，解析采用单任务加最新请求版本，避免每次事件创建新线程。

验收：临时测试目录覆盖大文件、多文件、空目录树、中文/空格路径、重名、权限拒绝、跨文件系统移动；操作中删除组件或关闭窗口不访问已销毁对象。UI 心跳和输入仍响应；失败信息明确，移动失败不丢源文件。文件系统变更风暴应合并，任务不并发堆积，最终列表准确。

### 第二批：系统采样与壁纸准备

- 系统监视采集移到单个 worker，采集快照后主线程一次更新；保留隐藏/紧凑模式节流，诊断与普通采样避免互相覆盖基线。
- GUI 同步进程按实际路径替换：已存在的原生配置接口优先，否则异步 QProcess，加错误、超时和销毁处理。壁纸解码与纯 QImage 预处理放后台；QWidget/QPixmap 和当前 GL 资源不随意跨线程。
- 修复 strut 降级的面板筛选和屏幕坐标映射，再补时间文案及 Shell 快速反向动画。

验收：隐藏监视器无额外采样，慢任务不重入；快速连续切壁纸最终显示最新图，保持现有圆角、抗锯齿和文字对比度；单屏 1×/1.5×/2×、副屏偏移、四边任务栏及检测降级均测试。

### 第三批：依据测量调整材质与公共实现

- 为打开菜单分段记录截图、模糊/折射、上传/读回、首次显示耗时；区分冷启动与热缓存，记录 P50/P95、最大值和样本数。
- 分别测静止桌面、连续开合、换壁纸时应用 CPU、Xorg、GPU 和常驻内存，不把某个单帧尖峰直接归因于小组件或 GPU。
- 背景 provider 必须与视觉需求匹配：要求显示实际后方窗口的菜单不能无条件改用壁纸。缓存键包含背景版本、几何、DPR 和主题，并设置容量上限。
- 有测量证据后再选择复用渲染器或新增 GL 后端。保留 CPU fallback、减少动态效果选项；不重新引入已处理的 Peony 与合成器整窗模糊冲突。
- 补开始菜单/面板 CI 构建覆盖、GL 失败路径测试，逐步提取 DesktopCanvas 职责。

建议性能目标是降低长任务造成的 UI 事件延迟，保持空闲无新增持续渲染/轮询；具体毫秒阈值以当前机器、相同分辨率和电源策略的基线测量确定。本轮未实测，不承诺“必然流畅”或某个 CPU 降幅。

## 实现边界

- 当前兼容 Qt 5.12；不能直接采用报告建议的 Qt 5.15 `QFile::moveToTrash()`。`QSettings` 也不是 dconf/GSettings 的等价读取替代品。
- X11 连接按线程明确所有权，不把一个 Display 随意交给多个线程共用。
- 原生 KDE 模糊属性名为 `_KDE_NET_WM_BLUR_BEHIND_REGION`；合成器模糊不是液态折射的等价替换，也不能保证降低所有设备上的资源占用。
- 不机械套用 GL 析构 if 补丁，不为已有 QPointer 守卫增加无必要的对象管理，不为 15 点采样先造复杂缓存。
- 本文不改变用户已有液态参数、GPU 频率策略、登录启动配置或 Peony 兼容服务。原始用户分析文件保持不动。

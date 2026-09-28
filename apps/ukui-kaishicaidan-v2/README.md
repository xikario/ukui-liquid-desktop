# ukui-kaishicaidan — UKUI 开始菜单透明覆盖层

当前为 V2 液态主线。开始按钮通过 X11 面板事件更新位置，面板退出时隐藏、重新出现时跟随恢复；背景与玻璃控件采用 NextKde shader 的 Qt5 适配。历史验证记录在 V2_TEST.md。

## 产品概述

**ukui-kaishicaidan**（UKUI 开始菜单）是一款为 **UKUI 桌面环境**（银河麒麟桌面操作系统）设计的开始菜单替代方案。它采用创新的**透明覆盖层**架构——不替换原生的 `ukui-menu` 面板插件，而是作为一个独立无边框窗口悬浮在开始按钮上方，提供现代化、视觉增强的开始菜单体验。

### 架构设计

- 通过 X11 检测 UKUI 面板位置（查找 `_NET_WM_WINDOW_TYPE_DOCK` 且 `WM_CLASS` 为 `ukui-panel` 的窗口）
- 在原生开始按钮上方创建一个完全透明的覆盖层小部件
- 用户点击或按 Win 键时，在按钮上方弹出增强型开始菜单
- 原生开始按钮保持完全可见——透明覆盖层仅仅叠加在其上方

---

## 核心功能

### 1. 开始菜单面板

| 特性 | 描述 |
|---|---|
| **面板尺寸** | 680×720px，带左侧导航栏和主内容区 |
| **玻璃态设计** | 磨砂玻璃背景 + 边缘高光效果 |
| **6 种主题皮肤** | 深色、浅色、赛博、玻璃、壁纸色、极简液态（Eco Liquid） |
| **自绘引擎** | 所有按钮、图标、头像和电源符号均通过 QPainter 手绘，无外部图片资源 |
| **流畅动画** | QVariantAnimation 实现悬停效果（150ms OutCubic 缓动），显示时淡入/上滑动画（QPropertyAnimation） |

### 2. 固定应用网格

- **6 列网格**展示固定/收藏的应用程序
- **拖拽重排**：支持拖拽调整固定应用顺序，带可视化的拖放目标指示器
- **右键菜单**：固定/取消固定、打开、打开文件位置、修改图标、卸载

### 3. 全部应用视图

- 在"固定应用"和"全部应用"视图间切换
- **三种排序方式**：按字母序、按安装时间、按最近使用
- 返回按钮（箭头）返回固定应用视图

### 4. 应用搜索

- **实时搜索**：输入时自动过滤，140ms 防抖
- **多字段匹配**：按应用名称、描述（Comment）和执行命令搜索
- **一键启动**：回车直接启动第一个搜索结果

### 5. 最近文件

| 功能 | 描述 |
|---|---|
| **数据来源** | `~/.local/share/recently-used.xbel`（XDG 标准格式） |
| **显示信息** | 文件名、距离现在的时间、MIME 类型 |
| **更多按钮** | 切换到按应用分组的最近使用视图 |

### 6. 左侧导航栏

| 按钮 | 功能 |
|---|---|
| **头像按钮** | 通过 AccountsService（D-Bus `org.freedesktop.Accounts`）获取用户头像，点击打开账户设置 |
| **文档按钮** | 打开文档文件夹 |
| **设置按钮** | 右键菜单：系统设置、关于麒麟、开始菜单设置 |
| **电源按钮** | 完整电源菜单：锁定屏幕、挂起、休眠、混合睡眠、注销、重启、关机 |
| **悬停提示** | 带箭头指示的悬浮工具提示 |

### 7. 全局快捷键（Win 键）

- **XRecord 扩展**：通过 libXtst 的 XRecord 扩展全局拦截 Super_L / Super_R 键
- **智能判断**：仅响应**短按**（< 400ms 且无其他键同时按下）
- **组合键穿透**：Super+E 等组合键正常穿透，应用仅拦截裸 Win 键
- **双显示连接**：控制连接和数据连接分离，确保录制的实时性

### 8. 单实例 D-Bus

- **主服务**：`org.ukui.kaishicaidan.v2`
- **兼容别名**：同时注册 `org.ukui.menu` 兼容端口
- **命令参数**：`--toggle`（默认）、`--show`、`--hide`、`--quit`

### 9. 活动窗口追踪

- 每 1.2 秒轮询 `_NET_ACTIVE_WINDOW`
- 通过 WM_CLASS、进程名和执行令牌匹配识别用户当前使用的应用
- 记录应用启动时间，用于"最近使用"排序

### 10. 自动关闭逻辑

- **双重机制**：X11 ActivationChange 事件 + 轮询定时器
- 轮询定时器通过读取 X11 指针按钮状态检测用户是否在菜单外点击
- 开始按钮周围的缓冲区域防止闪烁误关

### 11. 卸载支持

- **用户级应用**：直接删除 `~/.local/share/applications/` 中的 `.desktop` 文件
- **系统级应用**：通过 `dpkg -S` 查找包名，调用 `pkexec apt-get purge` 在终端中卸载

### 12. 设置对话框

| 设置项 | 选项 |
|---|---|
| 皮肤选择 | 深色 / 浅色 / 赛博 / 玻璃 / 壁纸色 |
| 字体族 | 系统全部字体（QFontComboBox） |
| 字号 | 9–22pt |
| 面板透明度 | 0% / 30% / 50% / 70% / 100% |
| 恢复默认 | 一键重置所有设置为初始值 |

### 13. 自动应用发现

- 使用 `QFileSystemWatcher` 监控应用目录变化
- 扫描范围：XDG 数据目录、Flatpak、Snap、`~/Desktop`、`~/桌面`、`~/应用/*/usr/share/applications/`

---

## 技术栈

| 层次 | 技术 |
|---|---|
| UI 框架 | Qt 5（Core / Gui / Widgets / DBus） |
| 窗口管理 | libX11（Xlib 直接调用） |
| 键盘拦截 | libXtst（XRecord 扩展） |
| 构建系统 | CMake ≥ 3.16，AUTOMOC |
| 编程语言 | C++17 |
| 显示协议 | X11（`_NET_WM` 规范） |

---

## 项目结构

```
ukui-kaishicaidan/
├── CMakeLists.txt              # CMake 构建配置
├── packaging/
│   ├── ukui-kaishicaidan-launcher        # Shell 启动器（D-Bus 单例）
│   ├── ukui-kaishicaidan.desktop         # 桌面入口文件
│   └── ukui-kaishicaidan-autostart.desktop  # XDG 自动启动（5 秒延迟）
├── src/
│   ├── main.cpp                # 入口点、D-Bus 单例
│   ├── StartMenu.h/.cpp        # 开始菜单面板核心 (~2600 行)
│   ├── StartButton.h/.cpp      # 透明覆盖层按钮
│   ├── StartMenuTheme.h        # 主题/皮肤系统、调色板、配置存储
│   ├── AppRegistry.h/.cpp      # 应用发现、固定/最近管理
│   ├── RecentFiles.h/.cpp      # 最近文件追踪（读取 XBEL）
│   ├── KeyInterceptor.h/.cpp   # 全局 Win 键拦截（XRecord）
│   ├── TaskbarDetector.h/.cpp  # 任务栏位置检测（X11）
│   └── SettingsDialog.h/.cpp   # 设置对话框
├── os.tsx                      # React/TS 设计原型（非构建代码）
└── UI美化.md                   # UI 美化设计文档
```

## 依赖项

| 依赖 | 类型 | 用途 |
|---|---|---|
| Qt 5（Core, Gui, Widgets, DBus） | 构建时链接 | UI 框架 |
| libX11 | 构建时链接 | X11 窗口属性、活动窗口追踪 |
| libXtst | 构建时链接 | XRecord 扩展（全局键盘钩子） |
| pkexec / apt-get / dpkg | 运行时 | 应用卸载 |
| systemctl | 运行时 | 挂起/休眠/重启/关机 |
| gsettings | 运行时 | 壁纸路径读取 |
| gtk-launch | 运行时 | 从 .desktop 文件启动应用 |
| gdbus | 运行时 | 启动器脚本中的 D-Bus 通信 |

---

## 技术亮点

- **零侵入覆盖层**：不修改、不替换原生 `ukui-menu`，通过透明窗口覆盖实现功能增强
- **XRecord 键盘拦截**：使用 XRecord 扩展而非 XGrabKey，实现全局 Win 键捕获同时允许组合键正常穿透
- **全自绘 UI**：所有视觉元素使用 QPainter 手绘，无任何外部图片依赖，支持动态主题切换
- **壁纸取色皮肤**：实时分析当前壁纸主色调，动态生成配色方案
- **多桌面环境兼容**：主要针对 UKUI，同时具备对 MATE 和 GNOME 的部分回退支持

---

## 构建与安装

在完整仓库中构建，命令见 [根 README](../../README.md)。CMake 提供显式用户级安装规则，构建不启用自启动。启动/恢复见 [安装说明](../../docs/INSTALL_AND_RESTORE.md)。

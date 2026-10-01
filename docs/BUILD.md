# 构建与隔离测试

[项目主页](../README.md) · [安装与恢复](INSTALL_AND_RESTORE.md) · [验证记录](VALIDATION.md)

默认从**仓库根目录**执行命令。完整克隆仓库：三个应用通过相对路径引用 `shared/`，仅复制单个应用目录不足以构建。分别配置三个 CMake 项目，不把它们直接放进一个 add_subdirectory 超级工程，以免重复定义共享 target。

## 依赖

基础要求为 CMake 3.16+、C++17、匹配系统应用的 Qt 5 与 X11/OpenGL 开发环境。Debian/Ubuntu 系包名参考，麒麟请使用匹配系统 Qt 的包：

```sh
sudo apt install build-essential cmake qtbase5-dev libx11-dev libxtst-dev libgl1-mesa-dev zlib1g-dev python3
```

| 范围 | 附加依赖 |
| --- | --- |
| Fences | Qt Core/Gui/Widgets/DBus/Network、Zlib；GIO 文件操作、gdbus 启动器 |
| 开始菜单 | Qt Core/Gui/Widgets/DBus、Xtst/XRecord；gtk-launch、gsettings，卸载按需用 dpkg-query/apt-get/pkexec |
| 任务栏插件 | Qt Core/Gui/Widgets，与系统 OEM 面板 Qt 兼容；Python 安装与恢复工具 |
| 智能空间 | Python 3；PDF 按需用 poppler-utils，OCR 用 tesseract-ocr 及语言包，部分格式需要额外提取工具 |
| 系统监视诊断 | curl、libsecret-1.so.0 和可用的 Secret Service 密钥环；API 由用户自行配置 |
| 音乐 | 提供 MPRIS 的客户端；识别出的桌面入口用 gtk-launch（libgtk-3-bin） |
| 日历 | Python 标准库 SQLite、系统 ICU 运行库及对应 UKUI 日历数据库/服务；节假日同步需要网络 |
| 可选模糊兼容 | Python Xlib（python3-xlib）、X11 与用户 systemd 会话 |
| 可选 FTG340 策略 | 匹配驱动、systemd/udev、Python 3.8+；与主应用构建无关 |
| UI/图像测试 | xvfb、xauth、dbus-x11、xdotool、x11-utils；部分脚本需 Pillow，OEM 冒烟另需 bubblewrap |

SVG 图标显示还取决于系统 Qt 图像插件。农历使用系统 ICU，不需编译本仓库的自有 ICU。未安装某个可选提取工具不妨碍基础桌面运行，但相应格式或功能会受限。

## 构建三个应用

```sh
cmake -S apps/ukui-fences -B apps/ukui-fences/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-fences/build -j2
cmake -S apps/ukui-kaishicaidan-v2 -B apps/ukui-kaishicaidan-v2/build-v2 -DCMAKE_BUILD_TYPE=Release -DBUILD_GLASS_TESTS=ON
cmake --build apps/ukui-kaishicaidan-v2/build-v2 -j2
cmake -S apps/ukui-panel-liquid -B apps/ukui-panel-liquid/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-panel-liquid/build -j2
```

CTest 目标由 `BUILD_TESTING` 控制，默认开启；开始菜单额外的 GL 渲染测试由 `BUILD_GLASS_TESTS` 控制。Fences 预览程序默认开启，可用 `UKUI_FENCES_BUILD_POPUP_PREVIEW=OFF` 关闭。

构建不会安装、启动或替换运行中的实例；更新静态共享模块后需要重新构建并重启各宿主才能生效。

## 自定义 Qt SDK

标准开发环境无需以下变量。自备 SDK 可传：

```sh
cmake -S apps/ukui-fences -B apps/ukui-fences/build -DUKUI_FENCES_QT_SDK_ROOT=/path/to/sdk -DUKUI_FENCES_GL_INCLUDE_DIR=/path/to/include
cmake -S apps/ukui-kaishicaidan-v2 -B apps/ukui-kaishicaidan-v2/build-v2 -DKAISHICAIDAN_QT_SDK_ROOT=/path/to/sdk -DKAISHICAIDAN_GL_INCLUDE_DIR=/path/to/include
cmake -S apps/ukui-panel-liquid -B apps/ukui-panel-liquid/build -DCMAKE_PREFIX_PATH=/path/to/sdk/root/usr/lib/ARCH/cmake -DUKUI_LIQUID_GL_INCLUDE_DIR=/path/to/include
```

GL 目录需包含 `GL/gl.h`。两个应用 SDK_ROOT 回退约定是 `root/usr/lib/<架构>/cmake` 与 `root/usr/include`；其他布局直接指定 CMAKE_PREFIX_PATH 和 X11 头文件路径。本仓库不包含 SDK、机器路径或构建缓存。

## 安全的脚本与参数检查

这些测试使用临时文件、隔离配置或替身工具，不执行真实安装：

```sh
python3 apps/ukui-panel-liquid/tests/install_test.py
python3 apps/ukui-panel-liquid/tests/session_start_test.py
python3 integration/peony/test_blur_compat.py
(cd extras/ukui-desktop-performance && python3 -m unittest discover -v)
python3 apps/ukui-fences/tests/test_smart_space_knowledge.py
python3 apps/ukui-fences/scripts/smart_space_knowledge.py --help
python3 apps/ukui-fences/scripts/smart_space_indexer.py --help
```

不要通过运行安装器的 `--help` 猜测行为：并非每个安装脚本都实现无副作用的帮助参数。界面 CLI 也不是 argparse，错误参数可能打开应用，参数以入口源码及产品指南为准。

## 隔离 UI 回归

先在构建目录列出测试，再选择需要的用例；该写法兼容目标机较旧的 CTest：

```sh
(cd apps/ukui-fences/build && ctest -N)
(cd apps/ukui-fences/build && ctest --output-on-failure -R 'desktop_(monitor_diagnosis|music|settings-center)_')
(cd apps/ukui-kaishicaidan-v2/build-v2 && ctest --output-on-failure -R 'launcher-(metadata|removal-query)')
xvfb-run -a python3 integration/peony/test_blur_compat_x11.py
```

Fences 所选 UI 用例和开始菜单来源查询用例由 CTest 包装隔离的 Xvfb / D-Bus / XDG；面板基础 CTest 含需要 Qt 显示环境的用例，在独立显示/会话中运行：

```sh
xvfb-run -a -s '-screen 0 2880x1800x24' dbus-run-session -- sh -c 'cd apps/ukui-panel-liquid/build && ctest --output-on-failure'
```

开始按钮坐标回归固定为 150% 缩放，模拟 72 物理像素的面板：

```sh
env QT_FONT_DPI=96 QT_SCALE_FACTOR=1.5 QT_SCREEN_SCALE_FACTORS=1 xvfb-run -a -s '-screen 0 1920x1200x24' dbus-run-session -- python3 apps/ukui-kaishicaidan-v2/tests/button-follow-test.py
env QT_FONT_DPI=96 QT_SCALE_FACTOR=1.5 QT_SCREEN_SCALE_FACTORS=1 xvfb-run -a -s '-screen 0 1920x1200x24' dbus-run-session -- python3 apps/ukui-kaishicaidan-v2/tests/context-menu-stability-test.py
```

来源查询回归使用虚构应用和延迟的替身 dpkg-query，不实际卸载。其他文件操作用例可能调用真实 GIO，但只在隔离临时 HOME/XDG 中操作测试文件。不要把要求隔离的应用测试直接运行在个人桌面会话中。

## GPU 与历史测试限制

无可用 GL 的 Xvfb 中，部分 GPU 用例返回 77（跳过），不能计作 GPU 通过。开始菜单 glass-render-test 需要真实可用 GL，无 GL 时渲染断言会失败；CPU 回退可独立测试。真实显卡光学与首次动画流畅度需对应硬件验证。

部分历史智能空间 UI 脚本保留旧多空间/AI 断言，不能把旧报告当成当前全仓通过结论。各版本实测范围见 [验证记录](VALIDATION.md)。公共菜单单独预览与测试见 [模块说明](../shared/liquid-popup/README.md)。

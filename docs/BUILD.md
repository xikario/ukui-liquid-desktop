# 构建和验证

所有命令除特别说明外在仓库根目录执行。请完整克隆仓库：应用通过相对路径引用 `shared/`，仅拷贝单个应用目录不足以构建。

## 依赖

Debian/Ubuntu 系的包名参考（麒麟请使用匹配系统 Qt 的开发包）：

```sh
sudo apt install build-essential cmake qtbase5-dev libx11-dev libxtst-dev libgl1-mesa-dev zlib1g-dev python3 xvfb xauth dbus-x11 xdotool x11-utils libgtk-3-bin
```

UI 测试需要 `xvfb-run`、`dbus-run-session`、`xdotool`、`xwininfo`、`xprop`。部分 Python/图像验证需要 Pillow。日历农历使用 ICU 运行库；智能空间 PDF/OCR 按需使用 poppler-utils、tesseract-ocr、tesseract-ocr-chi-sim。系统日历、UKUI、Strawberry 等功能还需相应应用。

音乐客户端需要提供 MPRIS。自动识别的桌面启动入口使用 `gtk-launch`（Debian/Ubuntu 的 `libgtk-3-bin`）；直接配置可执行文件时不依赖该入口。音乐回归包含实际桌面入口启动，需要安装此运行工具。

构建命令见根 README。默认每次独立配置一个应用，不将三个 CMake 子项目直接放进同一个 add_subdirectory 超级工程，以免重复定义公共库 target。

## 自定义 Qt SDK

标准开发环境不需要设置以下变量。精简系统可在配置时传入 SDK，仓库不包含 SDK、机器绝对路径或缓存：

```sh
cmake -S apps/ukui-fences -B apps/ukui-fences/build   -DUKUI_FENCES_QT_SDK_ROOT=/path/to/sdk   -DUKUI_FENCES_GL_INCLUDE_DIR=/path/to/include
cmake -S apps/ukui-kaishicaidan-v2 -B apps/ukui-kaishicaidan-v2/build-v2   -DKAISHICAIDAN_QT_SDK_ROOT=/path/to/sdk   -DKAISHICAIDAN_GL_INCLUDE_DIR=/path/to/include
cmake -S apps/ukui-panel-liquid -B apps/ukui-panel-liquid/build   -DCMAKE_PREFIX_PATH=/path/to/sdk/root/usr/lib/ARCH/cmake   -DUKUI_LIQUID_GL_INCLUDE_DIR=/path/to/include
```

`/path/to/include` 下应有 `GL/gl.h`。两应用的 SDK_ROOT 回退约定为 `root/usr/lib/<架构>/cmake` 和 `root/usr/include`；其他 SDK 布局直接设置 CMAKE_PREFIX_PATH/X11 头文件路径。

## 隔离回归

```sh
(cd apps/ukui-fences/build && ctest --output-on-failure -R 'desktop_(music|calendar|desklets)_|calendar_snapshot|fence_glass_|smart_space_(wallpaper_sync|indexer|knowledge)|liquid-popup')
(cd apps/ukui-panel-liquid/build && ctest --output-on-failure)
python3 apps/ukui-panel-liquid/tests/session_start_test.py
(cd extras/ukui-desktop-performance && python3 -m unittest discover -v)
env QT_FONT_DPI=96 QT_SCALE_FACTOR=1.5 QT_SCREEN_SCALE_FACTORS=1 xvfb-run -a -s '-screen 0 1920x1200x24' dbus-run-session -- python3 apps/ukui-kaishicaidan-v2/tests/button-follow-test.py
env QT_FONT_DPI=96 QT_SCALE_FACTOR=1.5 QT_SCREEN_SCALE_FACTORS=1 xvfb-run -a -s '-screen 0 1920x1200x24' dbus-run-session -- python3 apps/ukui-kaishicaidan-v2/tests/context-menu-stability-test.py
```

开始按钮测试使用 150% 缩放，模拟面板高度 72 物理像素（48 逻辑像素）；其坐标断言按此环境编写。

右键菜单稳定性测试使用独立 XDG 配置和虚构固定应用，让安装来源查询延迟 600ms，逐次记录实际菜单从“检查中”到结果显示的几何；可将 QT_SCALE_FACTOR 改为 1、1.5、2。无需实际卸载应用。若存在 ImageMagick 的 import 命令，还会保存菜单截图；几何记录始终保存到 build-v2/artifacts。

音乐、日历、时钟/活动测试覆盖 DPR 1 / 1.5 / 2，使用临时 XDG 配置、独立 X server 与独立 D-Bus。不要把需要隔离的应用集成测试直接运行在个人桌面会话中。

Fences GPU 用例在无可用 GL 的 Xvfb 返回 77（跳过），不能算 GPU 通过。CPU 回退验证仍可运行。开始菜单的 `glass-render-test` 检查真实 GL 渲染，需要可用 GPU；无 GL 环境会报对应渲染断言失败。真实显卡验证与首次动画流畅度仍需对应硬件测试。

部分历史智能空间 UI 脚本保留了旧的多空间/AI 交互断言，见 VALIDATION.md，不应将历史报告当成最新源码的全量通过结论。

## 单独调公共气泡

```sh
cmake -S shared/liquid-popup -B shared/liquid-popup/build -DLIQUID_POPUP_BUILD_DEMO=ON
cmake --build shared/liquid-popup/build -j2
(cd shared/liquid-popup/build && ctest --output-on-failure)
./shared/liquid-popup/build/liquid-popup-preview
```

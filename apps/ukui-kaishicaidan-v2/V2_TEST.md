> 历史开发/设计记录，保留用于追溯。部分功能、路径、构建与部署状态已变化；当前版本以仓库根 README 和 docs/VALIDATION.md 为准，截图、配置和备份不随源码发布。

# ukui-kaishicaidan V2 默认开始菜单

这个目录现在是 V2 默认开始菜单的工作副本。视觉效果先在此验证，再由用户级自启动使用；正式版源码目录保持不变。

## 运行边界

- 二进制：`build-v2/ukui-kaishicaidan-v2`
- D-Bus 服务：`org.ukui.kaishicaidan.v2`
- D-Bus 对象：`/ukuiKaishicaidanV2`
- 配置目录：`~/.config/ukui-kaishicaidan-v2/`
- V2 当前注册全局 Win 键、创建开始按钮覆盖层，并接管 org.ukui.menu
- CMake 仍没有系统 install 规则；默认启动由用户级 autostart 和 D-Bus service 指向本目录
- V2 打开时抓取一次屏幕背景，分别生成清晰边缘纹理和柔化正文纹理。
- 原样内嵌 NextKde 的 glass.glsl、snells-glass.glsl，通过 Qt5 离屏 OpenGL
  调用真实 Snell 折射、RGB 色散、局部透镜和方向性细边高光；来源和许可证见
  vendor/nextkde-glass/README.md。
- 整块面板统一采样、统一绘制，左栏只是轻微遮色；不是把全幅背景压缩到右侧。
- 没有 OpenGL/Shader 时自动回退到 CPU 模糊、深色底和圆角，不再依赖 KWin
  blur 保证文字可读性。无屏幕捕获时使用近不透明底色。
- 重复 --show 不会重抓已显示菜单；液态皮肤取消位移动画，避免快照错位。

## 构建与运行

```bash
cmake -S . -B build-v2 -DCMAKE_BUILD_TYPE=Release
cmake --build build-v2 -j"$(nproc)"
./run-v2.sh
```

如果系统是极简开发环境，需要安装或提供 `GL/gl.h`；运行时只要求已有的
Qt5 OpenGL/GLX 能力。V2 的屏幕快照是“每次打开捕获一次”，不是 KWin6 那种
实时 compositor framebuffer 采样，适合先验证视觉和性能方向。

`run-v2.sh` 启动 V2 默认模式，并以 `--show` 打开菜单。退出 V2 可执行：

```bash
build-v2/ukui-kaishicaidan-v2 --quit
```

不要对这个副本执行 `sudo cmake --install`。当前用户级切换入口为：
~/.config/autostart/ukui-kaishicaidan.desktop
~/.local/share/dbus-1/services/org.ukui.menu.service

## 2026-09-24 第二轮验证

以下为历史记录；材质参数已被第三轮替代。

- 本机 1020×1080 / DPR 1.5 实测使用 upstream Snell/glints，最新一次生成耗时
  222ms（捕获后的材质生成，不代表完整打开耗时）；当前每次打开新建离屏上下文，
  没有常驻渲染循环。可后续优化上下文缓存。
- 自动测试 1× / 1.5× / 2× 共 30 项通过：Shader、尺寸/DPR、圆角 alpha、
  白背景压暗、上下采样方向、条纹抑制、CPU 回退、边缘变化、中心无畸变。
- 此实验皮肤的底色密度暂时固定，以保证浅色背景可读；配置中的透明度滑块不控制
  快照材质。它仍不是实时 KWin 合成特效，背后窗口变化要重新打开才会更新。

测试命令：

```bash
cmake -S . -B build-v2 -DBUILD_GLASS_TESTS=ON
cmake --build build-v2 -j2
./build-v2/glass-render-test /tmp/kaishicaidan-glass-regression
```

手动强制 CPU 回退需先退出 V2，再运行：

```bash
./build-v2/ukui-kaishicaidan-v2 --quit
KAISHICAIDAN_GLASS_NO_GL=1 ./run-v2.sh
```

## 第三轮：透镜而非深色磨砂板

- 去掉固定蓝灰颜料：正文约 7px 柔化，光学边缘约 2.8px 柔化。
- 双纹理混合：46px 内收边带保留背景结构，沿 SDF 法线做 Snell 折射，
  26px 边缘厚度、可见色散、局部焦散与方向性高光；正文使用自适应中性遮色。
- 标签使用局部投影保持辨识度；搜索框移除高饱和青色描边。
- 鼠标靠近边缘时有缓动反射（QPainter 叠层，不重抓屏幕）。
  33ms 定时器仅窗口显示时运行，指针静止后不再要求重绘。
- 修正离屏图像预乘 alpha：透明圆角 RGB 必须归零，避免亮色方角漏光；
  取消矩形 KWin blur 请求，避免在已处理的快照背后再模糊一次。
- 1× / 1.5× / 2× 共 33 项检查通过。测试验证渲染正确性，不代表视觉等同上游。
- 仍为打开时快照；没有实时追踪背后窗口。未改动正式版、壁纸或自启动。

临时彩色色带/文字背景验证（不是修改系统壁纸，180 秒自动退出）：

```bash
./build-v2/ukui-kaishicaidan-v2 --hide
./build-v2/glass-render-test --backdrop-preview
```

预览背景关闭后，应重新打开 V2 以刷新快照。

## 第四轮：2026-09-25

- 保留第三轮的双纹理透光材质。边缘带从 26px 收到 22px、清晰过渡范围从
  46px 收到 40px、位移强度 4.2→3.5，减轻厚卷边；高光同步略收敛。
- 亮处标签按实际背景采样增加贴字形的细暗轮廓，暗处不加；
  不使用每个标签一块黑色底牌。按下图标轻微收缩，不改变布局。
- 复用离屏 OpenGL context、shader program 和同尺寸 FBO；
  输入纹理在每次渲染后释放，背景依旧每次打开重新捕获。
  GL 资源析构在有效上下文中执行；强制 CPU 回退后可恢复使用缓存的 GPU。
- 鼠标反光只刷新 28px 边带，不再要求全菜单重绘。
- 同轮旧实现重复生成 400×300/600×450/800×600 耗时 198/194/215ms；
  新版对应重复生成约 15–35ms。1020×1080 全菜单两轮五次热生成均值分别
  84.4ms、88.4ms。
  实机菜单冷生成 279ms、再次打开生成 90ms。以上不包含抓屏、应用列表扫描、
  映射窗口及淡入时间，不能当作完整打开耗时。
- 48 项检查通过：包括缓存确定性、跨 DPR/FBO 尺寸切换、亮暗区域采样、
  清空背景、GPU/CPU 切换；另外进行了临时白色文档背景与彩色色带实机检查。

亮背景实测：

```bash
./build-v2/ukui-kaishicaidan-v2 --hide
./build-v2/glass-render-test --backdrop-preview --bright
```

限制不变：仍非实时合成背景，透明度滑块尚未接入此实验材质。

## 第五轮：按钮液态玻璃

- 面板的第四轮光学参数保持不变；新增独立 controlMode 分支。
- 固定应用、最近使用、左侧工具按钮、搜索框、所有应用/返回/排序胶囊
  使用菜单干净材质的局部裁切，进行小尺度 Snell 折射和边缘反光。
  裁切不包含图标、文字或其他按钮，不抓取窗口自身，避免反馈叠影。
- 应用格子常态有轻玻璃，悬停沿用 160ms 动画提高强度；指针靠近某一边时
  该处局部高光增强。按下显示独立材质、轻微收缩，图标仍保持清晰。
- 常态/按下态用位置、尺寸、圆角作为键缓存，预算 8MiB；
  更换背景就清空，悬停帧不重复跑 shader。无 GPU 时仍有圆角玻璃外观。
- 列表滚动时按新坐标重绘；裁切越界不拉伸残缺背景。
- “所有应用/返回/排序”改为左键释放在原目标内才触发，支持按住后移出取消。
  未改变电源、主题等左栏动作的触发逻辑。
- 73 项渲染检查通过，覆盖三种 DPR、小圆角 alpha、按钮缓存、按下差异、
  背景不被按钮修改、扁胶囊、裁切越界、GPU/CPU 回退和背景更换失效。
- 实机验证：所有应用进入/返回、移出取消、WPS 搜索、应用格子按下、
  拖拽后 Esc 取消（未产生固定列表配置）。键盘自动化测试曾以
  QT_IM_MODULE=none 启动临时 V2，避开输入法预编辑；正常交付实例恢复默认输入法。

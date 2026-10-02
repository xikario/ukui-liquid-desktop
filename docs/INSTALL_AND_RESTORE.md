# 安装、登录启动与恢复

[项目主页](../README.md) · [构建依赖](BUILD.md)

所有命令默认从**仓库根目录**执行。主桌面组件安装到用户目录，不需要 sudo；构建不会自动安装或重启。已有单实例仍使用旧二进制，升级时先保存工作与设置草稿、正常退出，再重新启动。

## Fences 与六类小组件

### 安装和启动

```sh
sh apps/ukui-fences/packaging/install-user.sh
~/.local/bin/ukui-fences-launcher --autostart
```

脚本默认使用 `apps/ukui-fences/build`，可将其他构建目录作为第一个参数；安装前缀默认 `~/.local`，可用 `UKUI_FENCES_USER_PREFIX` 更改。脚本安装程序、启动器、预览及索引/知识库/日历资源，写入用户应用菜单入口和 `~/.config/autostart/ukui-fences.desktop`。会话 PATH 应包含对应安装前缀的 bin 目录。

六类小组件是否随 Fences 启动由 **Fences 设置 → 桌面小组件**分别控制。显示开关与自启动开关独立；音乐不会主动启动播放器，智能空间打开不自动扫描。配置与布局路径见 [Fences 首页](../apps/ukui-fences/README.md#启动器与配置路径)。

### 升级和回退

安装脚本不保存历史二进制，也不是跨文件的事务安装器。升级前备份已有程序及资源、`~/.config/kylin/ukui-fences.ini`、`~/.config/kyfences/layout.json`；需要保留知识库/统计时另备份用户数据。布局导出不备份桌面文件或其他数据。

正常退出旧实例：

```sh
~/.local/bin/ukui-fences-launcher --quit
```

退出后安装并重新启动。临时切回原桌面可用 `--hide`，Fences 进程仍保留；完全恢复系统桌面则退出并禁用/移除 Fences 自启动项。回退旧版本需恢复自己的二进制/资源备份，系统 Peony 桌面没有被替换。

## 开始菜单 V2

### 安装和启用

```sh
cmake --install apps/ukui-kaishicaidan-v2/build-v2 --prefix "$HOME/.local"
~/.local/bin/ukui-kaishicaidan-launcher --autostart
```

首次启动保持隐藏，点击系统开始按钮或短按 Win 键显示。首次启动就显示可直接用 `~/.local/bin/ukui-kaishicaidan-v2 --show`；包装器 `--show` 只操作已有实例，不启动缺失实例。

CMake 安装二进制、启动器与应用菜单入口，**不自动注册登录启动，不替换 D-Bus 服务文件**。在桌面环境的启动应用设置中添加启动器完整路径及 `--autostart`；仓库 `packaging/ukui-kaishicaidan-autostart.desktop` 是依赖 PATH 的模板，不会自动安装启用。

主服务为 `org.ukui.kaishicaidan.v2`，兼容别名 `org.ukui.menu` 仅在可注册时取得；不要假定原系统菜单退出或别名一定可用。

### 恢复

```sh
~/.local/bin/ukui-kaishicaidan-launcher --quit
```

正常退出覆盖层与 Win 键监听，移除自己建立的登录启动项，恢复原菜单启动方式。历史文档中的 XRecord 退出挂起已修复，不再作为日常退出流程。更多控制参数见 [开始菜单首页](../apps/ukui-kaishicaidan-v2/README.md#启动隐藏和退出)。

## 系统任务栏液态主题

### 安装和切换

```sh
python3 apps/ukui-panel-liquid/scripts/install.py
~/.local/bin/ukui-panel-liquid-session
```

需先完成默认构建目录 `apps/ukui-panel-liquid/build` 的构建。安装器备份全部受管目标到本模块 `releases/<时间>-<随机后缀>/`，安装用户插件、包装器、恢复工具和两个自启动入口。它不替换 `/usr/bin/ukui-panel`，不立即停止当前面板。

session 入口显式切换当前面板，登录时也执行一次检查：已加载则退出，否则通过会话管理器正常停止旧实例再启动包装器。带互斥、等待上限和失败回退；OEM 会话接口不同需另行验证。不要同时启动第二个面板实例。

### 关闭或恢复

只关闭效果：任务栏右键 → **外观与特效**取消液态主题。撤销自启动并立即启动原面板：

```sh
~/.local/bin/ukui-panel-liquid-restore --restart
```

不加 `--restart` 仅撤销受管入口。不会自动还原安装前所有自定义覆盖，需从 `before.json` 对应备份恢复；也不会删除全部插件、包装器或备份。可捕获的安装失败会回滚，强制中止造成的半状态仍可能需要手工恢复。详见 [任务栏首页](../apps/ukui-panel-liquid/README.md)。

## 独立可选模块

- [系统应用模糊兼容](../integration/peony/README.md)：需要 X11、Python 3、libX11 运行库和用户 systemd；安装会立即启用用户服务并处理目标窗口属性，停用后重开窗口恢复其原模糊请求。
- [FTG340 策略](../extras/ukui-desktop-performance/README.md)：管理员安装，明确传入 `600000` 或 `800000`；会写系统服务/规则和匹配驱动参数，不随任何主应用安装，也不适用于其他显卡。

不使用可选模块不影响三个主应用的构建与安装。

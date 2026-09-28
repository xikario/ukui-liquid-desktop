# 安装与恢复

构建与发布不自动启用组件。以下命令会改变用户安装状态，应在自己决定启用时执行。桌面现有实例不自动切换到新二进制；退出旧实例后再启动，避免单实例总线或锁占用。

## Fences 与全部小组件

```sh
sh apps/ukui-fences/packaging/install-user.sh
```

默认使用 `apps/ukui-fences/build`，安装到 `~/.local`，并建立 Fences 用户自启动。请确保会话 PATH 包含 `~/.local/bin`。自定义构建目录可作为脚本第一个参数。

完整安装同时包含液态预览程序、索引/知识库脚本、日历脚本、农历模块和 2026 节假日 JSON。组件各自的显示、自启动、布局通过 Fences 右键菜单管理。

回退：先用 `~/.local/bin/ukui-fences-launcher --quit` 退出，关闭或移除用户的 Fences 自启动项；系统 Peony 桌面保留。升级前若已有版本，请自行备份安装文件及布局/配置；该安装脚本不保存历史二进制。

## 开始菜单 V2

```sh
cmake --install apps/ukui-kaishicaidan-v2/build-v2 --prefix "$HOME/.local"
```

安装二进制、启动器与菜单入口，**不会自动注册自启动或替换 D-Bus 服务文件**。`~/.local/bin/ukui-kaishicaidan-launcher` 可手动启动；自动启动可用桌面环境的启动应用设置选择这个完整路径，并加参数 `--autostart`。`packaging/ukui-kaishicaidan-autostart.desktop` 为 PATH 启动模板；会话需能找到 `ukui-kaishicaidan-launcher`。

程序运行时占用 `org.ukui.kaishicaidan.v2` 并尝试兼容 `org.ukui.menu`，创建 Win 键监听与开始按钮覆盖层。其他已注册该总线名称的菜单程序可能冲突。

回退：退出 V2，移除自己建立的启动项，恢复原菜单启动方式。当前 XRecord 线程退出仍有已知挂起情况；可使用进程管理器结束确认属于 V2 的进程。不要泛用 killall 终止整个桌面。

## 系统任务栏液态主题

完成默认 `apps/ukui-panel-liquid/build` 构建后：

```sh
python3 apps/ukui-panel-liquid/scripts/install.py
```

脚本备份现有用户配置/覆盖文件到该模块的 `releases/<时间>/`，安装 Qt 插件和独立登录检查入口。不会替换 `/usr/bin/ukui-panel`，不会立即停止现有面板。下一次登录由 session-start 检查原面板是否已加载插件并在需要时切换；OEM 会话接口不同可能需要调整。

恢复原系统启动方式：

```sh
~/.local/bin/ukui-panel-liquid-restore --restart
```

恢复脚本撤销本项目标记的自启动覆盖并启动原面板；它不自动还原安装前的所有自定义用户覆盖。需要原有定制文件时从 `releases/` 备份恢复。只想关闭效果可在面板菜单关闭液态主题。

## 可选 FTG340 策略

独立参见 `extras/ukui-desktop-performance/README.md`。仅适用于匹配的 FTG340 和 simple_ondemand 驱动，涉及管理员权限、电源策略和可能的空闲功耗增加，不随主应用自动安装。

# 第二轮核验问题修复（2026-10-01）

对应 Fable 5.1 报告 `ukui_review_round2.md` 的三项已确认缺陷，源码基线为 `f6018cf`。Peony 文档中的直接脚本入口已核验通过，本轮不改其导入方式。

## 回收站撤回对应准确删除记录

旧实现只记原始路径，撤回时选择最近的同路径 trashinfo。若外部程序后来删除了同路径的新文件，Fences 会把外部代次当作自己的撤回结果。

`FileClipboard::trashFilesAsync()` 现在在后台取得删除前的文件身份及回收站元信息快照；成功删除后，结合原始位置、新增/改变的 trashinfo 和实体设备号/inode 确认对应记录。撤回栈保存准确实体、元信息路径，以及设备号/inode/ctime 和元信息摘要。恢复时核对本次记录，不按时间排序寻找替代文件。桌面、分区、单图标及拖入回收站的入口都传递完整删除结果。

采用 XDG 数据目录及源文件所在卷的 `.Trash/<uid>`、`.Trash-<uid>` 候选位置，并处理规范中的相对 Path。记录无法可靠取得时明确提示手动恢复；记录或实体被清理/替换/修改时拒绝撤回。原位置有同名文件（含悬空符号链接）时另取唯一名。部分撤回成功后只保留失败项的准确记录供重试。

回归使用真实 GIO、独立 HOME/XDG 目录和真实 Fence → DesktopCanvas → Ctrl+Z 链路，覆盖外部再次删除同路径、正常 LIFO、冲突不覆盖、中文及百分号路径、目录、悬空链接、实体缺失、元信息替换、实体修改、部分成功及重试。符合规范的相对元信息和无记录返回另用隔离替身覆盖。

本机 OEM GIO 在 `/dev/shm` 测试中拒绝跨文件系统 trash（reflink/clone 错误）；确认原文件保留且没有加入撤回记录。不能把这个环境结果称作真实卷回收站恢复成功；卷位置选择有实现，相对路径解析已由隔离元信息测试覆盖。

规范参考：[Trash Specification](https://specifications.freedesktop.org/trash/latest/)。

## 开始菜单当前目标的来源查询自动完成

来源查询与卸载影响预检查使用独立状态。查询最多一个活动后台任务及一个最新菜单请求；A 查询期间关闭 A、打开 B 后再打开 C，会丢弃尚未开始的 B 请求，自动完成 C 的查询。已销毁 QAction 不接收旧结果；同一应用关闭再打开也会完成新菜单的请求。

原有最终文字宽度预留保持有效。真实 Qt 菜单测试检查目标标签、软件包信息、启用状态、已关闭接收者、卸载预检查状态及查询前后几何尺寸。另以正式应用二进制在 Xvfb 下连续采样原生窗口，确认 100%、150%、200% 缩放下都只有一个位置/尺寸。

## 面板安装异常恢复原状态

安装前读取全部插件/脚本、系统 desktop 模板及系统二进制，缺任何输入都会在目标替换前失败。备份包含六个受管理目标及面板配置，恢复脚本也进入清单。为每个目标建立独立暂存文件，全部准备完成后逐项替换；捕获异常时按逆序恢复原内容、权限及符号链接，清理新建目标、暂存文件和本次创建的空目录。

同一仓库的安装由文件锁串行化，备份目录使用时间与随机后缀。模板中的 Exec 引用路径，成功后才写 installed.json。若存储故障继续阻止回滚，错误明确列出未恢复目标和备份位置。多文件替换并非整体原子操作；该恢复保证针对脚本能够捕获的异常。

八个隔离单测覆盖全部输入缺失、备份失败、暂存失败、每一处替换失败、首次安装清理、完成清单写入失败、回滚自身失败报告，以及成功安装的六目标清单、路径引用和系统二进制保持不变。实际系统模板及构建产物也通过只读预检。

Exec 引用规则参考：[Desktop Entry Specification](https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html)。

## 验证

- Fences 相关 CTest：9/9，包括文件撤回、剪贴板、拖选、异步交互及三档菜单快捷键布局。
- 开始菜单 CTest：14/14，含三档新增来源查询回归及共享组件测试。
- 面板 CTest：19/19，含主题/圆角/共享组件和安装、登录脚本回归；新增最后两项安装测试后另重跑安装与登录入口，2/2 通过（安装脚本单测共 8 项）。
- 正式开始菜单二进制原生几何采样：1× 84 次、1.5× 85 次、2× 83 次，三个实例各只有一种几何尺寸。
- `git diff --check` 通过。

相关命令（CTest 从各构建目录执行）：

```sh
# apps/ukui-fences/build
ctest --output-on-failure -j1 -R 'desktop_(trash_undo|folder_drop_undo|clipboard_async|selection_drag|menu-shortcut|review_async)'
# apps/ukui-kaishicaidan-v2/build-v2
ctest --output-on-failure -j2
# apps/ukui-panel-liquid/build
ctest --output-on-failure -j2
```

原生窗口回归入口：`apps/ukui-kaishicaidan-v2/tests/context-menu-stability-test.py`，使用 Xvfb、独立 D-Bus 及 `QT_SCALE_FACTOR=1/1.5/2`。

## 本机更新

已正常退出并重载 Fences 与开始菜单；面板插件本轮没有修改或重载。安装脚本源码更新对下次安装生效。

备份：`releases/20261001-002059-review-round2`。已比较小组件位置/尺寸/可见性、监控位置/缩放/皮肤、开始按钮几何，以及面板和固定应用配置；保持一致。已安装二进制与运行中 `/proc/<pid>/exe` 均与构建 SHA-256 一致。

- `/home/zhouzhang/.local/bin/ukui-fences`：`17438875ec588e4785b15a2b3688ea22c671749c183d5fb980853378c0110eb3`
- `/home/zhouzhang/.local/bin/ukui-kaishicaidan-v2`：`956302c291b8e812dca6c70e4621783c6a78bebdaabd0c6ba6f2475ef124f1dd`

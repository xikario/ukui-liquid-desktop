> 可选且仅限 FTG340：以下测量为历史实机记录。仓库上传不安装或修改电源策略；安装可在仓库根执行 `sudo python3 extras/ukui-desktop-performance/install.py`。

# FTG340 首次桌面动画响应修复

2026-09-28，UKUI/X11 + 飞腾 FTG340。保留系统 Magic Lamp（魔壶）效果。

## 对比结果与证据边界

- 调频负载检查间隔 150ms → 30ms，用户确认没有明显改善；已恢复 150ms。
- 临时最低频率 200000 → 800000，用户确认首次最小化明显减轻。
- 再测试最低 600000，用户确认仍明显改善并同意使用。
- 因此采用接电最低 600000、最高仍 800000；电池供电恢复保存的原始最低值（本机 200000）。频率数值沿用驱动 sysfs 单位。
- 结论来自受控配置变化与用户视觉反馈，不是 GPU 帧时间测量，不保证消除所有卡顿，也未证明纹理缓存没有影响。

## 实现

源码 `ftg340-responsiveness.py` 安装为 `/usr/local/libexec/ukui-ftg340-responsiveness`，仅对 FTG340 和 simple_ondemand 生效。
配置 `/etc/ukui-ftg340-responsiveness.json`。
原始最低频率保存于 `/var/lib/ukui-ftg340-responsiveness/original.json`。
开机由 `ukui-ftg340-responsiveness.service` 一次性应用；电源接入/拔出由 udev Mains change 事件重新应用，无常驻轮询进程。
保留原 governor、最高频率和 150ms 检查间隔。接电空闲的功耗及温度可能增加，尚未量化。
关闭服务时恢复原始值；关闭后 udev 事件不会重新启用。

## 验证

`python3 -m unittest discover -s . -v`：2 项通过，覆盖接电/电池、最高档限制、停止恢复及禁用后不被电源事件重启。
`verify-installed.py` 通过管理员授权在真实设备验证：停止恢复 200000，启动回到 600000，触发真实 AC udev change 事件后仍为 600000；governor 和检查间隔保留。
服务已设开机启用，未为验证专门重启整机。拔电恢复在隔离测试中验证，未主动拔除用户电源。
魔壶运行时确认仍已加载。

## 停用或卸载

立即停用并取消开机应用（自动恢复原值）：

```sh
sudo systemctl disable --now ukui-ftg340-responsiveness.service
```

重新启用：

```sh
sudo systemctl enable --now ukui-ftg340-responsiveness.service
```

完整卸载：

```sh
sudo python3 extras/ukui-desktop-performance/install.py --remove
```

临时对比脚本和频率记录保存在 `~/.local/state/ukui-effects/first-animation-test/`。

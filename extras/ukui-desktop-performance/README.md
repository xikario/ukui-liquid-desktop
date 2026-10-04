# 可选 FTG340 接电最低频率策略

针对麒麟 V10 / UKUI X11 上匹配的 FTG340 驱动，接电时提高最低频率，电池供电或停用时恢复保存的原值。它是**需要管理员权限的独立系统扩展**，不随桌面主题安装，不适用于其他显卡，也不是通用性能优化。

[项目主页](../../README.md) · [权限与模块关系](../../docs/ARCHITECTURE.md)

## 适用条件与实现

策略面向 `/sys/class/devfreq/PHYT0048:00`，校验 FTG340 和 simple_ondemand，保留 governor、最高频率及检查间隔。首次安装要求基线最低值为 200000，避免把临时试验值记录成原始值。频率数字沿用驱动 sysfs 单位，不写成 MHz/Hz 的通用承诺。

源码使用 Python 3.8+。系统服务启动时一次应用，udev Mains change 事件重新应用电源状态，没有常驻轮询进程。接电提高最低频率可能增加空闲功耗和温度，历史验证未量化这两项。

| 文件 | 作用 |
| --- | --- |
| `/usr/local/libexec/ukui-ftg340-responsiveness` | 策略程序 |
| `/etc/ukui-ftg340-responsiveness.json` | 用户选择的接电最低频率 |
| `/etc/systemd/system/ukui-ftg340-responsiveness.service` | 启动/停止策略 |
| `/etc/udev/rules.d/90-ukui-ftg340-responsiveness.rules` | 电源事件触发 |
| `/var/lib/ukui-ftg340-responsiveness/original.json` | 保存原始最低值 |

## 安装、停用与卸载

从**仓库根目录**执行，必须明确选择 `600000` 或 `800000`，没有无参数安装模式：

```sh
sudo python3 extras/ukui-desktop-performance/install.py 600000
```

这会写系统文件、启用服务并立即应用策略。未检测到目标设备、基线不匹配或有不受管的旧文件时会拒绝安装。主应用和测试都不会替用户执行此命令。

停用并取消登录后的策略，恢复保存的原始最低值：

```sh
sudo systemctl disable --now ukui-ftg340-responsiveness.service
```

重新启用：

```sh
sudo systemctl enable --now ukui-ftg340-responsiveness.service
```

完整撤销已安装的程序、服务、规则与配置：

```sh
sudo python3 extras/ukui-desktop-performance/install.py --remove
```

卸载先停止服务以恢复原值，不承诺清除 `/var/lib` 中的历史基线记录。禁用后的电源事件不会自行重新启用服务。

## 隔离测试与历史实机结果

```sh
(cd extras/ukui-desktop-performance && python3 -m unittest discover -v)
```

测试使用替身目录，覆盖接电/电池、最高档限制、停止恢复和禁用后的电源事件，不写真实 sysfs。`verify-installed.py` 则会操作真实服务与设备，只用于用户已安装并决定验证的目标机，不作为普通单元测试。

2026-09-28 的目标机对比中，将负载检查间隔从 150ms 改为 30ms 没有明显改善，恢复原值；最低频率从 200000 提到 800000、再试 600000，用户观察首次最小化减轻，因此采用接电 600000。实机检查覆盖停止恢复、启动应用与接电事件；没有主动拔电或专门重启整机。

该结论来自配置对比和视觉反馈，不是帧时间基准，不保证消除所有卡顿，也不代表其他硬件收益。

## 临时对照最低频率

`trial-frequency-floor.py` 用于用户明确要求的保频 A/B 测试，需要在本机终端使用 sudo 认证。它读取首次安装时保存的 OEM 最低值，在限定时间内临时恢复该值，不改变服务启用状态、持久配置、governor、最高频率或轮询间隔。

```sh
sudo python3 extras/ukui-desktop-performance/trial-frequency-floor.py --seconds 480
```

480 秒后或 Ctrl+C 时恢复测试前的频率下限；若检测到其他电源事件改变下限则结束测试并保留该事件的新值。请保持供电条件不变。强制杀进程、断电等无法执行清理的情况不在脚本的正常恢复保证内，现有服务重启或电源事件会重新应用持久策略。

此脚本不自动关闭其他应用。温度/风扇有滞后，比较时应保持屏幕、工作负载和室温尽可能一致；降低下限并不等于降低播放中的实际频率，也不保证仍能稳定 60 fps，必须检查实际播放和丢帧。脚本恢复逻辑由 `test_trial_floor.py` 使用替身节点验证，不会在单元测试中写真实 sysfs。

2026-10-04 已完成保持 4K60 的 200000/600000 下限对照：两种下限播放同一视频时均升至 800000，未观察到明确降温收益，因此当前目标机继续保留 600000 接电下限。此结论不推广到其他片源/硬件，也不将扩展变成默认安装项。详细数据与限制见 [最终策略](../../docs/VIDEO_THERMAL_PREVIEW_20261004.md)。

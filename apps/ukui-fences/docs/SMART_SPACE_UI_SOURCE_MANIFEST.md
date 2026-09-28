> 历史开发/设计记录，保留用于追溯。部分功能、路径、构建与部署状态已变化；当前版本以仓库根 README 和 docs/VALIDATION.md 为准，截图、配置和备份不随源码发布。

# 智能空间 UI 源码包

本压缩包包含智能空间界面、桌面集成、主题菜单、图标资源和 UI 验证脚本。

## 核心界面

- `src/SmartSpaceWidget.h`
- `src/SmartSpaceWidget.cpp`

## 桌面与文件交互依赖

- `src/DesktopCanvas.h`
- `src/DesktopCanvas.cpp`
- `src/DesktopItem.h`
- `src/MenuStyle.h`

## 资源与集成

- `resources.qrc`
- `assets/smart-space-icon.png`
- `assets/smart-space-edge-reveal.png`
- `assets/smart-space-menu-icons/`
- `packaging/ukui-fences.desktop`
- `packaging/ukui-fences-autostart.desktop`
- `packaging/ukui-fences.svg`
- `CMakeLists.txt`

## 设计说明与 UI 验证

- `docs/SMART_SPACE.md`
- `docs/SMART_SPACE_TEST_REPORT.md`
- `tests/run_smart_space_ui_smoke.sh`
- `tests/run_smart_space_new_features_smoke.sh`
- `tests/run_smart_space_policy_smoke.sh`
- `tests/run_smart_space_resource_benchmark.sh`

索引器属于数据处理后端，不在本 UI 源码包内。

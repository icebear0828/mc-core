# mc-core（Minecraft 玩法叠加到 Sekiro / Wukong / Elden Ring）

**做艾尔登法环适配器的工作，先读 `docs/ELDENRING_HANDOFF.md`**（环境、部署、钩子表、已验证事实、待办、踩过的坑，都在那里）。证据与审计在 `docs/ELDENRING_REVERSE.md`，逐项验证状态在 `docs/ELDENRING_VERIFY_CHECKLIST.md`。项目记忆在 `~/.claude/projects/-Users-c-mc-core/memory/`（先读 `MEMORY.md`）。

## 必须遵守
- 代码和 commit 英文，和用户中文交流，简洁直接。commit 格式 `<type>: <description>`。
- **改完不得说"改好了"**，先贴验证命令和完整输出。先 trace 根因再改，不猜。
- TDD：新增/修改功能先写测试；Python 一律 `uv run`；不要在 mac 上假设 `loader.cpp` 等 Windows 文件被编译了——它们只在 win 上编译（`/W4 /WX`）。
- 代码只走 git（win 访问 GitHub 要 `-c http.proxy=http://127.0.0.1:7897`，TLS 常断，循环重试并核对两边 `git rev-parse --short HEAD` 一致）。
- **真实 Mojang 资产（皮肤、图集、声音）绝不提交**，只放游戏目录 `mods\mc_adapter\`，由 `tools/extract_mc_*.py` 本地生成。
- **严禁混淆占位图集与真实烘焙图集（防打包/部署灾难）**：
  - 源码树中的 `mc_hud_atlas.png` 或 `mc_hud_atlas.authentic.png` 是防侵权的占位/半成品，**其中的 `entity_shield` 等 3D 实体贴图区域通常为纯透明空像素**。
  - **严禁直接拷贝仓库中的图集到游戏目录或打包发布！**
  - 构建、打包或实机部署前，**必须**从官方 `client.jar`（`D:\game\sekiro\build\mc_client_1.21.8.jar`）使用 `python tools/extract_mc_assets.py --client-jar <path> --export-hud-atlas` 重新提取完整 78 UV 图集。
  - 部署/打包必须运行 `python tools/inspect_atlas_regions.py` 严格断言：`entity_shield` 实心像素数必须达到 716，`item_shield` 实心像素必须 ≥ 140，若检测到关键区域全透明立即终止打包。
- 部署前确认游戏已关：进程名是 **`start_protected_game.exe`**（不是 `eldenring.exe`）；部署后用 `certutil -hashfile` 核对哈希。
- 涉及代理路由、IP 出口、网络配置的改动先问用户。
- 逆向方（另一个 agent）的报告要**核对字节再采信**，已被证伪的清单见 handoff 第 8 节。

## 模式 / 输入 / 相机改动
- **先读 `docs/CONTROL_POLICY.md`。** 新增模式写成 `ControlRequest`，不要在
  `hotkey_dispatcher.cpp` 里加按键 if-chain，也不要在 `player_locomotion.cpp` 里直接判断 `first_person`。
- core 仲裁在 `include/mc/control_policy.hpp`；Elden Ring 的模式策略在
  `adapters/eldenring/include/eldenring_control_policy.hpp`，测试在 `tests/test_eldenring_control_policy.cpp`。
- 跨线程只读 `RuntimeContext::mode().control_authority_bits`，用 `modOwns` / `blocksNative` 查询。

## 构建变体（别部署错）
三个模式是三个独立的构建轴：`ENABLE_MC_MODE` / `ENABLE_CS2_VIEWMODEL` / `ENABLE_OW_VIEWMODEL`。
- `build/`：当前三者都 ON 的组合构建（可在运行时切换 MC / CS / OW）。
- `build_pure_mc/`：`ENABLE_CS2_VIEWMODEL=OFF`；若要连 OW 代码也排除，配置时加 `-DENABLE_OW_VIEWMODEL=OFF`。
- `build_modes/<combo>/`：`tools/deploy_modes.py` 为所选组合生成的独立构建目录（如 `mc+ow`、`cs`、`ow`）。
- **优先用统一部署工具**：`uv run python tools/deploy_modes.py --modes mc|cs|ow|mc,ow|all --deploy`。它按组合构建 DLL、只部署该组合的资产，并用 `mods/mc_adapter/mode_manifest.json` 清掉上一次部署里已取消模式的资产。
- 手工部署前先确认三个开关：`Select-String ENABLE_MC_MODE,ENABLE_CS2_VIEWMODEL,ENABLE_OW_VIEWMODEL <build>\CMakeCache.txt`。所有变体的 DLL 都叫 `dinput8.dll`。

## 常用命令
- mac 测试：`cmake --build build -j8 && ./build/bin/mc_tests`（当前 487 个应全过）；Python：`uv run --with pillow --with pytest python -m pytest tests/test_extract_assets.py tests/test_adapter_standards.py`。
- win 编译、部署、日志读取的完整命令见 `docs/ELDENRING_HANDOFF.md` 第 2 节。

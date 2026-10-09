# mc-core（Minecraft 玩法叠加到 Sekiro / Wukong / Elden Ring）

**做艾尔登法环适配器的工作，先读 `docs/ELDENRING_HANDOFF.md`**（环境、部署、钩子表、已验证事实、待办、踩过的坑，都在那里）。证据与审计在 `docs/ELDENRING_REVERSE.md`，逐项验证状态在 `docs/ELDENRING_VERIFY_CHECKLIST.md`。项目记忆在 `~/.claude/projects/-Users-c-mc-core/memory/`（先读 `MEMORY.md`）。

## 必须遵守
- 代码和 commit 英文，和用户中文交流，简洁直接。commit 格式 `<type>: <description>`。
- **改完不得说"改好了"**，先贴验证命令和完整输出。先 trace 根因再改，不猜。
- TDD：新增/修改功能先写测试；Python 一律 `uv run`；不要在 mac 上假设 `loader.cpp` 等 Windows 文件被编译了——它们只在 win 上编译（`/W4 /WX`）。
- 代码只走 git（win 访问 GitHub 要 `-c http.proxy=http://127.0.0.1:7897`，TLS 常断，循环重试并核对两边 `git rev-parse --short HEAD` 一致）。
- **真实 Mojang 资产（皮肤、图集、声音）绝不提交**，只放游戏目录 `mods\mc_adapter\`，由 `tools/extract_mc_*.py` 本地生成。
- 部署前确认游戏已关：进程名是 **`start_protected_game.exe`**（不是 `eldenring.exe`）；部署后用 `certutil -hashfile` 核对哈希。
- 涉及代理路由、IP 出口、网络配置的改动先问用户。
- 逆向方（另一个 agent）的报告要**核对字节再采信**，已被证伪的清单见 handoff 第 8 节。

## 常用命令
- mac 测试：`cmake --build build -j8 && ./build/bin/mc_tests`（当前 487 个应全过）；Python：`uv run --with pillow --with pytest python -m pytest tests/test_extract_assets.py tests/test_adapter_standards.py`。
- win 编译、部署、日志读取的完整命令见 `docs/ELDENRING_HANDOFF.md` 第 2 节。

# 贡献指南与规范 (Contributing & Conventions)

## 1. Git 提交规范 (Conventional Commits)

Commit message 统一使用英文，遵循 Conventional Commits 格式：

```
<type>(<scope>): <short description>

[optional body]

[optional footer]
```

### Type 类型清单：
- `feat`: 新增功能（如新增方块类型、新增三叉戟引雷逻辑）
- `fix`: 修复缺陷（如修复抛物线阻力衰减计算错误）
- `test`: 新增或修改测试用例（如新增网格吸附单元测试）
- `refactor`: 重构代码（不改变对外行为与接口）
- `docs`: 文档变动
- `chore`: 构建配置、依赖、辅助工具变动
- `perf`: 性能优化

### 示例：
- `feat(animator): implement steve walking and arm swing mathematical model`
- `test(voxel): add 100cm grid snapping and boundary test cases`
- `fix(ballistics): correct air resistance integration step`

---

## 2. 代码与工程规范 (Code Conventions)

### 2.1 C++ 核心准则
- **标准**：现代 C++20 (`-std=c++20`)。
- **内存安全**：禁止使用裸裸指针管理生命周期；所有权转移使用 `std::unique_ptr`，共享使用 `std::shared_ptr`，弱引用使用 `std::weak_ptr`；非所有权借用使用引用 `const T&` 或 `std::string_view` / `std::span`。
- **命名规范**：
  - 类型名（类、结构体、枚举）：`PascalCase`（如 `VoxelWorld`, `SteveAnimator`）。
  - 函数与方法：`camelCase` 或 `snake_case`（项目中统一使用 `camelCase`，如 `raycastWorld`, `updateTransforms`）。
  - 变量与参数：`snake_case`（如 `block_id`, `hit_result`）。
  - 成员变量：私有成员加下划线后缀 `name_` 或 `m_name`（项目中统一使用 `name_`）。
  - 常量与枚举项：`PascalCase` 或 `kConstant`（如 `BlockId::Stone`）。
- **零警告策略**：开启 `-Wall -Wextra -Wpedantic`，编译过程中警告视同错误（`-Werror`）。

### 2.2 测试驱动开发 (TDD)
- **无测试 = 未完成**。
- 新增任何核心算法（网格吸附、抛物线、生命值百分比计算、缓动曲线），必须先在 `tests/` 下编写对应 GoogleTest 单元测试，验证通过方可提交。

### 2.3 跨平台与跨引擎隔离
- `include/mc/` 下的所有头文件和核心类库严禁包含任何特定引擎（DirectX, RAGE, REDengine, RE Engine 等）专有头文件。
- 依赖倒置：所有与宿主游戏交互的行为必须通过 `mc::contracts` 纯虚接口进行委托。

### 2.4 宿主适配器开发铁律 (Adapter Anti-Pitfalls)
所有新游戏适配器（`adapters/*`）必须严格遵守 [**《适配器工程设计与避坑规范》(docs/ADAPTER_SPECIFICATION.md)**](docs/ADAPTER_SPECIFICATION.md)：
1. **图形生命周期**：商业游戏（Flip Model）必须每帧动态获取后备缓冲区 RTV；必须拦截 `ResizeBuffers` 防止主菜单与分辨率切换崩溃。
2. **拒绝空指针假调用**：禁止硬编码 `nullptr` 初始化；必须具备 AOB 特征扫描或真实指针动态解析，且激活时原子化隐身原生角色模型。
3. **高保真材质资产**：严禁使用 `<3`、`()`、`"Sword"` 等文本或粗劣线框糊弄 HUD；必须通过 `extract_mc_assets.py` 自动化图集与 D3D 纹理精确 UV 映射。
4. **自动化准入测试**：提交前必须通过 `pytest tests/test_adapter_standards.py` 校验。


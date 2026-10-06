# 目标游戏与私有引擎矩阵对照表 (Engine Recon Matrix)

在为特定游戏接入 `mc-core` 时，查阅本表获取其引擎特征、社区加载器、原主角隐形机制与物理碰撞 API。

---

## 1. 常见私有引擎特征清单

| 游戏名称 | 底层引擎 | 主流 Mod 加载器 / SDK | 原生主角隐身 API | 物理射线与碰撞 API | 伤害与击退分发 API |
|---|---|---|---|---|---|
| **《GTA V》** | RAGE (Rockstar Advanced Game Engine) | **ScriptHookV** (C++) | `ENTITY::SET_ENTITY_VISIBLE(playerPed, false, 0)` | `SHAPE_TEST::START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE` / `OBJECT::CREATE_OBJECT` | `MISC::SHOOT_SINGLE_BULLET` 或 `PED::APPLY_DAMAGE_TO_PED` + `PED::SET_PED_TO_RAGDOLL` |
| **《赛博朋克2077》** | REDengine 4 | **CET** (C++/Lua) + **Redscript** | `puppet:GetMeshComponent():SetVisible(false)` | `SpatialQueriesSystem:SyncRaycast` / 动态创建 static physics actor | `GameInstance.GetDamageSystem():QueueHitEvent` |
| **《生化危机》系列 (RE2/3/4R, 7/8)** | RE Engine | **REFramework** (C++ / Lua) | 遍历 Mesh 材质并设置 Alpha 为 0 或调用 `app.HumanCharacter:set_Draw` | `app.collision.CollisionSystem:raycast` / 动态生成 Prefab Object | `app.HitController:addDamage` |
| **《艾尔登法环》** | Dantelion (FromSoftware) | **ModEngine2** (C++ DLL Hook) | 隐藏角色部件 Parts (`partsbnd` alpha 或内存标记) | Havok Physics 内存射线探针 / SpEffect 触发 | 注入 `SpEffect` 或调用内部 `ApplyDamage` 虚函数 |
| **虚幻5游戏 (UE5 / UE4)** | Unreal Engine 5 / 4 | **UE4SS** (C++ / Lua) 或原生 DLL Hook | `Character->GetMesh()->SetVisibility(false)` | `UKismetSystemLibrary::LineTraceSingle` / `UBoxComponent` | `UGameplayStatics::ApplyDamage` 或原生战斗接口 |

---

## 2. 各引擎核心适配实现要点

### 2.1 GTA V (RAGE / ScriptHookV)
- **工程产物**：编译为 `.asi` (实质为 Windows DLL)。
- **线程模型**：在 `scriptRegister(hInstance, ScriptMain)` 注册的独立纤程中运行，每帧调用 `WAIT(0)`。
- **12 部位挂载**：
  - 预先用 OpenIV 打包 12 个 `.ydr` (如 `mc_steve_head.ydr` 等)。
  - 在运行时调用 `OBJECT::CREATE_OBJECT` 生成 12 个实体，然后调用 `ENTITY::ATTACH_ENTITY_TO_ENTITY` 挂在 `playerPed` 上。
  - 每帧读取 `mc::SteveAnimator` 输出的相对旋转，调用 `ENTITY::SET_ENTITY_ROTATION` 更新每个方块。
- **方块放置**：
  - 调用 `OBJECT::CREATE_OBJECT(stone_hash, pos.x, pos.y, pos.z, true, true, true)`。
  - 开启碰撞 `ENTITY::SET_ENTITY_COLLISION(blockObj, true, true)`，原版载具、NPC 和主角天然能站在上面。

### 2.2 赛博朋克 2077 (REDengine 4 / CET)
- **工程产物**：CET C++ 插件动态链接库。
- **生命周期**：挂钩 `OnUpdate` 或 `ExecuteScript` 周期。
- **12 部位挂载**：
  - 使用 WolvenKit 生成自定义 `.mesh` 部件包。
  - 将各个部位绑定到 `PlayerPuppet` 根骨骼，每 Tick 写入局部旋转变换矩阵。
- **射线检测**：
  - 使用 `IWorld::Raycast` 或 CET 暴露的 `SpatialQueriesSystem`。

### 2.3 生化危机系列 (RE Engine / REFramework)
- **工程产物**：REFramework 插件 DLL（放置于游戏根目录运行）。
- **生命周期**：注册 `reframework::register_update(OnFrameUpdate)`。
- **射线与物理**：
  - 调用 `via.physics.Raycast` 探测世界几何。
  - 角色受击通过拦截 `app.HitController` 实现硬直反馈。

### 2.4 虚幻 5 游戏 (Unreal Engine 5 / UE4SS 或原生 Hook)
- **工程产物**：UE4SS C++ Mod DLL 或通用 Native Hook DLL。
- **生命周期**：挂钩 `UWorld::Tick` 或自定义 Actor Component Tick。
- **12 部位挂载**：
  - 调用 `Character->GetMesh()->SetVisibility(false)` 隐藏原主角 Mesh。
  - 动态生成 12 个 `UStaticMeshComponent` (立方体) 挂在 RootComponent。
  - 每帧将 `mc::SteveAnimator` 输出更新到 `SetRelativeLocationAndRotation`。
- **方块与射线**：
  - 射线检测直接调用 `UKismetSystemLibrary::LineTraceSingle`。
  - 生成 `AActor` + `UBoxComponent` (49x49x49 cm) 作为物理方块。

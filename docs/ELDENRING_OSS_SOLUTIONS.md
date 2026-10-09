# 艾尔登法环 待办事项开源项目调研与可用方案

> 本文档对应 [`docs/ELDENRING_VERIFY_CHECKLIST.md`](file:///Users/c/mc-core/docs/ELDENRING_VERIFY_CHECKLIST.md) 中的所有待办事项（A1~A12, B1~B34, C1~C74, D1~D4）。
> 通过对当前主流开源项目（`fromsoftware-rs`、`libER`、`Elden-Ring-CT-TGA`、`erfps2`、`me3` 等）的源码逆向研究，整理出**已在开源项目中验证可用、可直接复用或关键参考**的技术方案与代码片段，供逐项 Review。

---

## 一、 主要开源项目来源汇总

| 项目名 | 作者 / 组织 | 仓库地址 | 协议 | 适用范围 / 核心贡献 |
|---|---|---|---|---|
| **fromsoftware-rs** | vswarte & Dasaav | [vswarte/fromsoftware-rs](https://github.com/vswarte/fromsoftware-rs) | MIT / Apache-2.0 | **完整支持 2.7.1.0**。全套引擎结构体定义（`CSMenuMan`, `CSFade`, `CSCamera`, `ChrIns`, `CSChrPhysicsModule`, `CSBulletManager` 等）、精确 RTTI 扫描器、Arxan 脱壳模式、2.7.1.0 RVA 表。 |
| **libER** | Dasaav-dsv | [Dasaav-dsv/libER](https://github.com/Dasaav-dsv/libER) | Apache-2.0 | 原生 C++20 API。提供 `from::GXBS::GXDrawTask` 原生 D3D12 渲染任务挂载、完整的 `CSTaskGroup` 引擎帧阶段枚举（PadStep, DmgMan, GraphicsStep 等）、220 个全单例符号表。 |
| **Elden-Ring-CT-TGA** | The Grand Archives | [The-Grand-Archives/Elden-Ring-CT-TGA](https://github.com/The-Grand-Archives/Elden-Ring-CT-TGA) | GPL / Open | TGA 官方 Cheat Table。提供通用 FD4 单例自动化查找模式（断言特征码）、RTTI 符号反修饰、输入禁用（`GameMan+0xBC4`）、NoHit/NoDamage 标志位、受击 NPC 钩子。 |
| **erfps2** | Dasaav-dsv | [Dasaav-dsv/erfps2](https://github.com/Dasaav-dsv/erfps2) | MIT / Apache-2.0 | 第一人称 Mod。相机矩阵控制、头部/身体透明化驱动、输入重定向、无穿模骨骼跟踪。 |
| **me3 (Mod Engine 3)** | garyttierney | [garyttierney/me3](https://github.com/garyttierney/me3) | GPL-3.0 | 现代 Mod 加载框架。无 EAC 安全离线加载、DLL 外部无损注入。 |

---

## 二、 逐项调研与可用代码方案

### A 组：读取层在真机上跑通

#### 【A1 & A2 & D1】单例与函数签名 / 跨版本自动定位
- **清单目标**：定位 `kWorldChrMan`、`kCSMenuMan`、`kCSNowLoadingHelper`、`kCSFade`、射线函数、阵营判定等。
- **开源来源 1**：[`The-Grand-Archives/Elden-Ring-CT-TGA: table_files/include/tga/fd4_singleton.h`](https://github.com/The-Grand-Archives/Elden-Ring-CT-TGA)
- **可用方案**：
  TGA 发现 FromSoftware 引擎在访问未初始化的 FD4 单例时，有一段统一的断言报错逻辑（引用字符串 `"未初期化のシングルトンにアクセスしました"`，源码文件 `"FD4Singleton.h"`）。利用单一特征码即可动态解析出**所有单例的静态地址与官方全名**：
  ```c
  // TGA 核心特征模式：
  pattern = "48 8b ? ? ? ? ? "  // MOV REG, [MEM] (目标单例指针)
            "48 85 ? "          // TEST REG, REG
            "75 2e "            // JNZ +2e
            "48 8d 0d ? ? ? ? " // LEA RCX, [runtime_class_metadata]
            "e8 ? ? ? ? "       // CALL get_singleton_name
            "4c 8b c8 "         // MOV R9, RAX
            "4c 8d 05 ? ? ? ? " // LEA R8, [%s:未初期化のシングルトンにアクセスしました]
            "ba ? ? 00 00 "     // MOV EDX, 0x0000????
            "48 8d 0d ? ? ? ? " // LEA RCX, [file_path]
            "e8 ? ? ? ?";       // CALL log_thunk
  ```
  通过解析 `candidate + 7 + *(int32_t*)(candidate + 3)` 可直接取得该单例在 `.data` 的绝对指针，调用 `get_singleton_name` 即可获得其实际名称（`WorldChrMan`, `CSMenuMan` 等），**无需针对每个版本硬编码 4 个不同的短特征码**。
- **开源来源 2**：[`fromsoftware-rs: crates/eldenring/src/rva/rva_ww.rs`](https://github.com/vswarte/fromsoftware-rs)
  对于 `2.7.1.0`（`Ww2710`），官方验证 RVA：
  - `cs_phys_world_cast_ray`: `0xC71E00`（底层核心）；包装函数：`0xC71D70`
  - `chr_cam_vmt`: `0x2A2AA08`
  - `chr_ins_vmt`: `0x2A310C8`
  - `chr_set_vmt`: `0x2A41348`

---

#### 【A3 & A4 & D1】RTTI 精确全名校验
- **清单目标**：获取并确认 `EnemyIns`、`PlayerIns`、`CSChrDataModule`、`CSChrPhysicsModule`、`CSEnemyDamageModule`、`CSPlayerDamageModule` 的真实 RTTI 名。
- **开源来源 1**：[`vswarte/fromsoftware-rs: crates/eldenring/mapper-profile.toml`](https://github.com/vswarte/fromsoftware-rs) & [`crates/shared/src/rtti.rs`](https://github.com/vswarte/fromsoftware-rs)
- **开源来源 2**：[`The-Grand-Archives/Elden-Ring-CT-TGA: table_files/include/tga/rtti.h`](https://github.com/The-Grand-Archives/Elden-Ring-CT-TGA)
- **确认的 RTTI 类名规范**：
  - 核心基类均位于 `CS::` 命名空间下或全局：
    - `CS::ChrIns`（虚表 `0x2A310C8`）
    - `CS::PlayerIns`
    - `CS::EnemyIns`
    - `CS::CSChrDataModule`
    - `CS::CSChrPhysicsModule`
    - `CS::ChrCam`（虚表 `0x2A2AA08`）
    - `CS::CSChrModelIns`（虚表 `0x2B35C68`）
  - TGA 的 `get_rtti_class_name_vmt` 提供了标准 MSVC x64 `RTTICompleteObjectLocator` 读取流程，使用 Windows API `UnDecorateSymbolName(descriptor->name + 1, out_name, out_name_cb, 14338)` 反修饰，可在运行时直接取得 demangled 名称。

---

#### 【A5 & B18】玩家血量、精力、专注值（FP）字段角色
- **清单目标**：`readPlayerVitals` 对照；确认 FP 三个字段 `+0x148/+0x14C/+0x150` 的语义。
- **开源来源**：[`fromsoftware-rs: crates/eldenring/src/cs/player_game_data.rs`](https://github.com/vswarte/fromsoftware-rs) & [`cs/chr_ins/module/data.rs`](https://github.com/vswarte/fromsoftware-rs)
- **已证实定义**：
  在 `CSChrDataModule` 中，血量/专注/精力采用完全一致的三元组布局（4字节 uint32）：
  - **HP**：
    - `+0x138`: `current_hp`（当前血量）
    - `+0x13C`: `current_max_hp`（有效最大血量，含红琥珀链坠/Buff 加成）
    - `+0x144`: `base_max_hp`（人物基础最大血量）
  - **FP（专注）**：
    - `+0x148`: `current_fp`（当前专注）
    - `+0x14C`: `current_max_fp`（有效最大专注，含饰品/Buff）
    - `+0x150`: `base_max_fp`（人物基础最大专注）
  - **Stamina（精力）**：
    - `+0x154`: `current_stamina`
    - `+0x158`: `current_max_stamina`
    - `+0x15C`: `base_max_stamina`
  - **无死/无伤调试位**：
    - `CSChrDataModule + 0x19B`, bit 0: `NoDead`（锁血不死）
    - `CSChrDataModule + 0x19B`, bit 1: `NoDamage`（完全免疫伤害判定）

---

#### 【A6 ~ A9 & B19 ~ B21】敌人枚举、坐标系统与防尖峰保护
- **清单目标**：`enumerateEnemies`、team 判定、ChrSet 遍历、跨原点坐标跳变。
- **开源来源**：[`fromsoftware-rs: crates/eldenring/src/cs/chr_ins.rs`](https://github.com/vswarte/fromsoftware-rs)
- **可用字段布局**：
  ```rust
  pub struct ChrIns {
      pub vftable: VPtr<dyn ChrInsVmt, Self>,        // +0x00
      pub field_ins_handle: FieldInsHandle,          // +0x08 (实例句柄)
      pub chr_set_entry: NonNull<ChrSetEntry<Self>>, // +0x10
      ...
      pub block_origin: BlockId,                     // +0x40~0x48 地图块原点
      pub chr_model_ins: OwnedPtr<CSChrModelIns>,    // +0x50 模型实例
      pub npc_param_id: i32,                         // +0x58
      pub npc_id: i32,                               // +0x5C
      pub chr_type: ChrType,                         // +0x60
      pub team_type: u8,                             // +0x64 (原始阵营)
      pub p2p_entity_handle: P2PEntityHandle,        // +0x68
      ...
      pub chunk_position: F32Vector4,                // +0x80 (大地图块绝对坐标)
      pub initial_position: HavokPosition,           // +0x90 (生成点 Havok 坐标)
      pub initial_orientation_euler: F32Vector4,     // +0xA0
      ...
      pub lock_on_target_position: F32Vector4,       // +0xD0 (锁定准星坐标)
      ...
      pub modules: OwnedPtr<ChrInsModuleContainer>,  // +0x190 (64个模块指针槽)
  }
  ```
- **关键结论**：
  1. **B22 解锁**：`ChrIns + 0x80` 是 `chunk_position`，`+0x90` 是 `initial_position`，`+0xD0` 是 `lock_on_target_position`。玩家和敌人之所以不同，是因为玩家跟随大地图 chunk 实时流动，而敌人的 `+0x90` 记录的是刷怪初始出生点！
  2. **实体坐标标准读法**：必须从 `modules[0x0D]`（`CSChrPhysicsModule`）中读取：
     - `CSChrPhysicsModule + 0x70`: 当前实时 `HavokPosition`（X, Y, Z, W）
     - `CSChrPhysicsModule + 0x80`: 上一帧 `last_update_position`
     两者差值即物理移动增量。

---

#### 【A10 & B25 & B26】相机系统与投影矩阵
- **清单目标**：4 个 `CSPersCam` 行为、活动相机判断、TAA 抖动。
- **开源来源**：[`fromsoftware-rs: crates/eldenring/src/cs/camera.rs`](https://github.com/vswarte/fromsoftware-rs)
- **核心数据结构**：
  ```rust
  #[shared::singleton("CSCamera")]
  pub struct CSCamera {
      vftable: usize,
      pub pers_cam_1: OwnedPtr<CSPersCam>, // +0x08
      pub pers_cam_2: OwnedPtr<CSPersCam>, // +0x10
      pub pers_cam_3: OwnedPtr<CSPersCam>, // +0x18
      pub pers_cam_4: OwnedPtr<CSPersCam>, // +0x20
      pub camera_mask: u32,                // +0x28 (活动相机位掩码)
      ...
  }
  ```
- **B25 解密**：
  - `camera_mask` 位掩码（`+0x28`）控制当前哪一个相机正在拷贝到主渲染相机 `pers_cam_1`：
    - `0b00000001` 或 `0b00001000`: `pers_cam_2` -> `pers_cam_1`（常规探索/战斗视角）
    - `0b00010000`: `pers_cam_3` -> `pers_cam_1`（锁定/瞄准视角）
    - `0b00000010` / `0b00000100` / `0b00100000`: `pers_cam_4` -> `pers_cam_1`（过场/事件/死亡镜头）
  - 因此直接读取 `pers_cam_1` 的 ViewMatrix 与 ProjMatrix 即可获取最终合成的视口矩阵。

---

#### 【A11】EAC 离线与自检
- **清单目标**：自检缺少 `steam_appid.txt` 或加载了 EAC 模块。
- **开源来源**：[`me3: Mod Engine 3`](https://github.com/garyttierney/me3)
- **可用实践**：
  - 检查当前可执行目录下是否存在 `steam_appid.txt` 且内容为 `1245620`。
  - 调用 `GetModuleHandleA("easyanticheat_x64.dll")` 及 `GetModuleHandleA("easyanticheat.dll")`，若非空则说明处于在线保护环境，必须主动调用 `abort()` 或拒绝挂载。

---

### B 组：只读研究（菜单、加载、状态、物理）

#### 【B1 & B2 & B7 & B8 & B13 & B14】菜单与 UI 状态管理
- **清单目标**：`CSMenuMan+0x1A`、`+0x798` 弹窗、`ui_states` 槽位与焦点掩码。
- **开源来源**：[`fromsoftware-rs: crates/eldenring/src/cs/menu_man.rs`](https://github.com/vswarte/fromsoftware-rs) & [`crates/eldenring/src/cs/menu_type.rs`](https://github.com/vswarte/fromsoftware-rs)
- **精准结构布局**：
  ```rust
  #[shared::singleton("CSMenuMan")]
  pub struct CSMenuManImp {
      vftable: usize,
      menu_data: usize,
      player_status_calculator: usize,
      unk18: [u8; 2],
      pub disable_mouse_cursor: bool,               // +0x1A: 控制是否释放/显示光标
      unk1b: [u8; 0x65],
      pub popup_menu: Option<NonNull<CSPopupMenu>>, // +0x798: 当前弹窗指针 (非空表示有模态弹窗)
      window_job: usize,
      pub ui_states: [UIState; 0x46],               // +0x80: 70个UI子系统状态位
      ...
      pub disable_save_menu: u32,                   // +0x138: 禁用存档菜单/自动存档
  }
  ```
- **字段含义实测解析**：
  - `+0x1A`（`disable_mouse_cursor`）：当用户进入 3D 操作时为 `true`（锁定光标在窗口中心并隐藏）；打开任意菜单/地图/装备时变为 `false`（显示系统光标）。
  - `+0x798`（`popup_menu`）：指向当前活动的模态确认框（如传送到赐福点确认、退出游戏确认、使用骨灰确认等）。
  - `ui_states`：长度为 0x46（70项）的 `UIState` 数组，每个字节包含状态标志（bit 0: 驻留 Active, bit 1: 动画过渡 InTransition, bit 2: 拥有焦点 HasFocus）。
  - `B12` YOU DIED 判定：`CSMenuManImp::display_status_message(message_id)` 中 `STATUS_MESSAGE_YOU_DIED = 5`，或监听 `CSMenuMan` 下的死亡消息队列。

---

#### 【B3 & B4】读盘判断 `CSNowLoadingHelper`
- **清单目标**：黑屏时长、过场是否误判。
- **开源来源**：[`fromsoftware-rs: crates/eldenring/src/cs/now_loading.rs`](https://github.com/vswarte/fromsoftware-rs)
- **解析**：
  - `CSNowLoadingHelper`（单例）持有当前加载进度的 task 与随机背景图列表。
  - `+0xE0` 标志在进入真实区域加载（传送、地牢切换、死亡重载）时非空；在引擎事件过场（Event Camera）中保持 0。

---

#### 【B5 & B6】淡入淡出 `CSFade`
- **清单目标**：`Plate 7/8` alpha 变化、`+0x58`/`+0x5C` 含义。
- **开源来源**：[`fromsoftware-rs: crates/eldenring/src/cs/fade.rs`](https://github.com/vswarte/fromsoftware-rs)
- **结构体定义**：
  ```rust
  #[shared::singleton("CSFade")]
  pub struct CSFade {
      vftable: usize,
      pub fade_system: OwnedPtr<CSFD4FadeSystem>,
      pub fade_plates: [OwnedPtr<CSFD4FadePlate>; 9], // +0x10 ~ +0x58 (9个 Plate 指针)
      pub unk58: u32,                                 // +0x58: 淡入淡出状态码/类型
      pub fade_rate: f32,                             // +0x5C: 淡入淡出速率 (如 10.0f)
  }

  pub struct CSFD4FadePlate {
      vftable: usize,
      pub reference_count: u32,
      _padc: u32,
      pub current_color: [f32; 4], // +0x10: RGBA 浮点颜色，+0x1C 即为 Alpha 通道!
  }
  ```
- **结论**：
  - `+0x58` 为当前全局 Fade 状态码，`+0x5C` 为淡入淡出过渡速率。
  - 每个 Plate（如 Plate 7 传送黑屏，Plate 8 死亡灰度遮罩）在 `+0x1C` 处是 `float alpha`（0.0f 为全透，1.0f 为全黑）。

---

#### 【B16 & B17 & C58 & C59】物理、着地标志与方块碰撞
- **清单目标**：着地标志 `+0x92/+0x93`、线速度 `+0x120`、方块碰撞 `PhysicsModule+0x91`。
- **开源来源**：[`fromsoftware-rs: crates/eldenring/src/cs/chr_ins/module/physics.rs`](https://github.com/vswarte/fromsoftware-rs)
- **精准物理结构映射**：
  ```rust
  pub struct CSChrPhysicsModule {
      vftable: usize,
      pub owner: NonNull<ChrIns>,
      ...
      pub position: HavokPosition,                    // +0x70 (当前 Havok 绝对位置)
      pub last_update_position: HavokPosition,        // +0x80 (上一帧位置)
      unk90: bool,                                    // +0x90
      pub chr_proxy_pos_update_requested: bool,       // +0x91 ! (重要: Havok Proxy 同步标志)
      pub standing_on_solid_ground: bool,             // +0x92 ! (站立在坚实地面)
      pub touching_solid_ground: bool,                // +0x93 ! (触地标志)
      unk94: [u8; 4],
      pub chr_proxy: usize,                           // +0x98 (Havok Character Proxy 指针)
      ...
      pub hk_collision_shape: usize,                  // +0xB0 (Havok 碰撞体)
      ...
      pub root_motion: F32Vector4,                    // +0xD0
      ...
      pub chr_push_up_factor: f32,                    // +0x104
      pub ground_offset: f32,                         // +0x108
      pub gravity_vector: F32Vector4,                 // +0x120 (重力与线速度向量)
  }
  ```
- **核心答复**：
  - **B16**：着地标志不在 `ChrIns` 顶层，而是在 `CSChrPhysicsModule`：`+0x92` 为 `standing_on_solid_ground`，`+0x93` 为 `touching_solid_ground`！起跳与在空中时两者均为 `false`，落地为 `true`。
  - **B17**：`PhysicsModule + 0x120` 确为由重力加速度与移动驱动合成的线速度向量。
  - **C58**：`PhysicsModule + 0x91` 为 `chr_proxy_pos_update_requested`！当修改角色位置或方块碰撞推开角色时，必须将此标志置为 `true`，否则 Havok 物理引擎在下个 tick 会直接用内部缓存的 Proxy 位置覆写覆盖外部改动。

---

### C 组：注入与写内存项

#### 【C1 ~ C17】伤害注入流水线与帧阶段调度
- **清单目标**：主线程安全排空 `DamageQueue`、调用 `vfunc[7]`、避免重复扣血、击杀卢恩结算、`HitContext` 结构。
- **开源来源 1**：[`libER: include/coresystem/taskgroups.inl`](https://github.com/Dasaav-dsv/libER)
- **开源来源 2**：[`fromsoftware-rs: crates/eldenring/src/rva/rva_ww.rs`](https://github.com/vswarte/fromsoftware-rs)
- **开源来源 3**：[`The-Grand-Archives/Elden-Ring-CT-TGA: Last Hit Npc Info/.cea`](https://github.com/The-Grand-Archives/Elden-Ring-CT-TGA)
- **工程解法**：
  1. **执行时机（C1 / C50）**：
     通过 `libER` 的 `CSTaskGroup` 枚举可知，游戏帧推进顺序为：
     `PadStep` -> `ChrIns_PrePhysics` -> `HavokWorldUpdate` -> `ChrIns_PostPhysics` -> **`DmgMan_Pre`** -> **`DmgMan_ShapeCast`** -> **`DmgMan_Post`** -> `MenuMan` -> `GraphicsStep`。
     - **排空 DamageQueue 的最佳安全时机**：不要在随机线程或外部时钟直接调，而是在 `DmgMan_Pre` 阶段，或者注册一个 `CSEzTask` 到主线程的 `DmgMan_Pre` 钩子点，只在 `GetCurrentThreadId() == g_main_thread_tid` 时触发！
  2. **受击与击杀结算（C3 ~ C5）**：
     - 调用受害者 `CSChrDamageModule` 的 `vfunc[7]`（`0x1404455C0`），攻击者传入玩家 `PlayerIns` 指针。
     - 击杀后，游戏原生的 `DmgMan_Post` 会自动根据 `attacker` 指针为玩家分发卢恩并播放死亡音频，无须手动改写卢恩数值。
  3. **Arxan 还原补丁防护（C10）**：
     - 开源库 [`fromsoftware-rs: crates/shared/src/arxan.rs`](https://github.com/vswarte/fromsoftware-rs) 提供了 GuardIT/Arxan 的解除方案：
       使用签名 `B9 ? ? ? ? E8 ? ? ? ? F3 0F 11 05 ? ? ? ? [0-128] ' 72 ? 48 8D ? ? ? ? ?` 扫出 Arxan 的 code restoration 还原例程并 NOP/Patch，即可彻底杜绝读盘或长跑后钩子被还原的隐患。

---

#### 【C20 ~ C31】D3D12 渲染体系（原生 GXDrawTask 革命性方案）
- **清单目标**：命令队列捕获、交换链拦截、深度缓冲 DSV 格式、反向 Z、ImGui 覆盖层。
- **开源来源**：[`Dasaav-dsv/libER: include/graphics/draw.hpp & source/graphics/draw.cpp`](https://github.com/Dasaav-dsv/libER)
- **突破性发现与方案**：
  在以往方案中，我们需要 Hook `IDXGISwapChain::Present` 或 Hook `ID3D12CommandQueue::ExecuteCommandLists`。
  但 `libER` 逆向出了 **FromSoftware 引擎的原生 D3D12 DrawTask 任务机制**：
  ```cpp
  class MyOverlayTask : public from::GXBS::GXDrawTask {
  public:
      void draw() override {
          ID3D12Device& device = this->get_device();
          ID3D12CommandQueue& queue = this->get_command_queue();
          D3D12_CPU_DESCRIPTOR_HANDLE& rtv = this->get_render_target_view();
          D3D12_CPU_DESCRIPTOR_HANDLE& dsv = this->get_depth_stencil_view(); // 深度缓冲句柄!
          const D3D12_VIEWPORT& vp = this->get_viewport();
          const D3D12_RECT& scissor = this->get_scissor_rect();

          // 可以在此处直接用原生 Direct3D12 队列绘制 ImGui 或 Steve 3D 模型!
      }
  };

  // 注册任务：
  auto task = from::make_refcounted<MyOverlayTask>();
  task->set_scene(from::GXBS::GXDrawTask::UI_SCENE); // UI_SCENE 在原生HUD之上; HDR_SCENE 在世界内(带深度)
  task->register_task(); // 自动进入 CSTaskGroup::GraphicsStep 循环执行
  ```
- **核心数据对应**：
  - **C20 / C21 队列**：引擎全局单例 `GLOBAL_GXDrawBase`（RVA `0x47EF360`）偏移 `+0x08` 直达 `d3d12_env->command_queue`，偏移 `+0x128` 直达 `GXSwapChain`。
  - **C24 深度缓冲 DSV 格式与反向 Z**：
    - 深度缓冲格式：**`DXGI_FORMAT_D32_FLOAT`**（32位浮点）。
    - **反向 Z 确认**：近平面为 **1.0**，远平面为 **0.0**！
    - 深度测试函数必须设置为 `D3D12_COMPARISON_FUNC_GREATER` 或 `GREATER_EQUAL`，Clear 值为 `0.0f`。
  - **C29 F7 截图格式转换**：
    - 交换链主缓冲为 `DXGI_FORMAT_R10G10B10A2_UNORM`。
    - 像素转 RGBA8 算法：
      `R = ((pixel & 0x3FF) * 255 + 511) / 1023`
      `G = (((pixel >> 10) & 0x3FF) * 255 + 511) / 1023`
      `B = (((pixel >> 20) & 0x3FF) * 255 + 511) / 1023`
      `A = 255`

---

#### 【C40 ~ C44】输入拦截与屏蔽
- **清单目标**：屏蔽攻击/挥刀/格挡，保留菜单鼠标键盘，手柄同步。
- **开源来源 1**：[`The-Grand-Archives/Elden-Ring-CT-TGA: Statistics/Misc/Disable Controls.xml`](https://github.com/The-Grand-Archives/Elden-Ring-CT-TGA)
- **开源来源 2**：[`fromsoftware-rs: crates/eldenring/src/cs/pad.rs & fd4/base_pad.rs`](https://github.com/vswarte/fromsoftware-rs)
- **双重现成方案**：
  1. **引擎顶层控制开关（极简方案）**：
     `GameMan + 0xBC4`: `Disable Controls`（1字节 bool）。
     - 当开启 Minecraft 交互模式时，将 `[GameMan + 0xBC4] = 1`，角色直接禁止所有普通攻击、翻滚与移动输入，而系统菜单与 ESC 键仍然由 `CSMenuMan` 响应。
  2. **手柄与输入设备轮询开关（底层方案）**：
     通过 `FD4PadManager` 单例获取 `CSInGamePad`：
     - `CSInGamePad` 基类为 `FD4BasePad`，其偏移 `+0x20` 即为 **`allow_polling: bool`**！
     - 当需要拦截战斗操作时，置 `allow_polling = false`，游戏底层输入轮询立刻返回 false，角色完全无法挥刀或格挡。

---

#### 【C50 ~ C54】地形射线实调
- **清单目标**：线程时机、墙面法线、`0x5D` 过滤器。
- **开源来源**：[`fromsoftware-rs: crates/eldenring/src/cs/havok_man.rs`](https://github.com/vswarte/fromsoftware-rs)
- **现成函数签名与调用约定**：
  ```rust
  // 单例 "CSHavokMan"
  // +0x98: phys_world 指针
  type FnCastRay = extern "C" fn(
      phys_world: *const CSPhysWorld, // CSHavokMan + 0x98
      filter_flags: u32,              // 0x5D (仅地形与环境几何体)
      ray_start: *const HavokPosition,// 16字节对齐 (alignas(16))
      ray_end: *const HavokPosition,  // 16字节对齐 (alignas(16))
      hit_out: *mut HavokPosition,    // 16字节对齐 (输出击中点与法线)
      ignore_entity: *const PlayerIns // 忽略自身 (主玩家指针)
  ) -> bool;
  ```
- **时机保障**：
  将射线调用放入 `CSTaskGroup::ChrIns_PostPhysics` 或 `LocationStep`（主线程更新阶段），即可保证物理世界树不处于写入锁中，避免多线程冲突崩溃。

---

#### 【C55 & C56】隐藏玩家模型（第一人称）
- **清单目标**：全身/头部部件隐藏、换装与翻滚稳定性。
- **开源来源 1**：[`fromsoftware-rs: crates/eldenring/src/cs/chr_ins.rs`](https://github.com/vswarte/fromsoftware-rs)
- **开源来源 2**：[`Dasaav-dsv/erfps2`](https://github.com/Dasaav-dsv/erfps2)
- **方案实现**：
  - `erfps2` 中控制透明度与视角穿模的核心字段位于 `ChrIns + 0x254`（`base_transparency: f32`）与 `+0x240`（`opacity_keyframes_multiplier`）。
  - 将 `base_transparency` 设为 `0.0f` 可直接将玩家身体完全隐藏（透明），同时手持武器在第一人称下仍正常渲染。

---

#### 【C70 ~ C74】Steve 第三人称 3D 渲染与场景遮挡
- **清单目标**：将 3D 模型画入世界并正确深度遮挡。
- **开源解法结合**：
  1. 使用 `libER` 的 `from::GXBS::GXDrawTask`，将 scene 设为 `HDR_SCENE`（3D 场景层）。
  2. 获取上下文传递的 `get_depth_stencil_view()`（DSV）。
  3. 开启深度测试 `D3D12_DEPTH_WRITE_MASK_ALL`，深度比较函数设置为 `D3D12_COMPARISON_FUNC_GREATER_EQUAL`（适应反向 Z）。
  4. 使用 `CSCamera` 的 `pers_cam_1` 视口矩阵进行 MVP 变换。
  5. 当 Steve 走到建筑物后方时，由于反向 Z 深度值小于场景已有深度，自动被墙壁遮挡，完美达成 C70 与 C71。

---

## 三、 Review 建议与推进步骤

建议分三批进行 Review 与落地接入：

1. **第一批（只读与单例基础，低风险）**：
   - 采纳 TGA 的 `find_fd4_singletons` 模式，替换硬编码的单例扫描，一次性解决 A1~A4 与 D1。
   - 采纳 `fromsoftware-rs` 的 `CSMenuManImp`（`+0x1A`, `+0x798`）、`CSFade`（`Plate+0x1C`）与 `CSChrPhysicsModule`（`+0x92/+0x93` 着地标志），补齐 B 组状态定义。
2. **第二批（输入与伤害流水线调度）**：
   - 输入拦截采纳 `GameMan+0xBC4`（`Disable Controls`）或 `CSInGamePad+0x20`（`allow_polling`），直接验证 C40~C44。
   - 伤害与射线调度采纳 `CSTaskGroup` 阶段绑定（`DmgMan_Pre` 与 `ChrIns_PostPhysics`），解决 C1 与 C50 的线程安全。
3. **第三批（D3D12 渲染与覆盖层）**：
   - 采纳 `libER` 的 `GXDrawTask` 方案，评估是否直接复用其 `UI_SCENE` / `HDR_SCENE` 任务挂载机制，无需自己手写复杂的 SwapChain Hook。

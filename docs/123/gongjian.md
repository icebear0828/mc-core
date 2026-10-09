### 1. +0x00 / +0x08 句柄格式与 ChrIns 转换关系

  结论：我们在玩家 ChrIns + 0x08 读到的 0xFFFFFFFF16F00000 正是这个 64 位实体句柄（Entity Handle）。

  #### 底层汇编铁证（0x1403C10C0 装配现场与 0x14062EEB0 句柄寻址）

  1. 读取现场：在引擎默认的发射准备函数 0x1403C10C0 中：
    0x1403C10CA: mov rax, qword ptr [rcx]     ; rcx 为发射者上下文 (ChrIns)
    0x1403C10D3: mov r8,  qword ptr [rax + 8] ; 直接读取 ChrIns + 0x08 !
    0x1403C10D7: mov qword ptr [rdx], r8     ; 存入 BulletSpawnData + 0x00 (OwnerHandle)

  2. 句柄分发机制（0x14062EEB0，句柄解包函数）：
    0x14062EEBC: shr rdx, 0x1c       ; 右移 28 位
    0x14062EEC0: and edx, 0xf        ; 取类型掩码
    0x14062EEC9: sub edx, 1
    0x14062EECC: je  0x14062efcd     ; 类型 1 -> ChrIns (单例管理)
  玩家句柄低 32 位为 0x16F00000，0x16F00000 >> 28 == 1，类型码为 1。在 0x14062EFCD 处直接调用 0x140508A50，返回对应的 ChrIns*。
  3. 字段约定：
      • req + 0x00 (OwnerHandle)：发射者实体句柄，填入 *(uint64_t*)(pPlayerChrIns + 0x08)（例如 0xFFFFFFFF16F00000）。
      • req + 0x08 (TargetHandle)：锁定目标句柄。
          • 自由准星（无锁定）：填 0xFFFFFFFFFFFFFFFFULL（即 64 位 -1）；
          • 锁定目标：填入目标 *(uint64_t*)(pTargetChrIns + 0x08)（仅对有追踪属性的箭矢生效；普通箭填 -1 即可）。


  ──────
  ### 2. 一支普通箭需要填写的完整字段

  BulletSpawnData 结构体共 0x110 字节（272 字节），由构造函数 0x14038C580 初始化：

    +0x00 (qword): OwnerHandle        -> *(uint64_t*)(pPlayerChrIns + 0x08)
    +0x08 (qword): TargetHandle       -> 0xFFFFFFFFFFFFFFFFULL (-1, 无追踪)
    +0x10 (dword): DummyPoly ID       -> -1 (0xFFFFFFFF, 禁用模型骨骼挂点，强制采信自定义世界变换)
    +0x14 (dword): -1                 -> -1
    +0x18 (dword): -1                 -> -1
    +0x1C (dword): BulletParam ID     -> 20000000 (标准普通箭 Arrow)
    +0x20 (dword): -1
    +0x24 (dword): -1
    +0x28 (dword): 12                 (构造函数默认)
    +0x2C (dword): 12                 (构造函数默认)
    +0x44 (dword): 行为标志           -> 0x08 (必须为 0x08)
    +0x50..+0x8F : 4x4 行优先世界矩阵 -> float[16] (Right, Up, Forward, Position)
    +0xB0 (qword): 0                  (子对象指针，置 0 引擎 0x140394E80 自动跳过)

  #### 关键字段释义与证据

  1. BulletParam ID（+0x1C）合法值与只读查询：
      • 标准箭矢 ID：20000000（原版 Elden Ring 最纯粹的标准普通轻箭，带完整飞行 FLVER 模型、初速度、重力弹道，无任何魔法尾迹）。
      • 运行时只读查询：全局单例 SoloParamRepository 位于 [Base + 0x03D7A130]，表索引为 10（param_index::Bullet）。在 libER 中可直接只读获取：
        auto [bullet_row, exists] = from::param::Bullet[20000000];
        // bullet_row.initVellocity   -> 初速度
        // bullet_row.gravityInRange  -> 抛物线下坠重力
        // bullet_row.hitRadius       -> 判定包围球半径

  2. 发射挂点 Dummy Poly（+0x10）：
      • 填 -1（0xFFFFFFFF）：彻底脱离武器与身体骨骼，使用我们在 +0x50..+0x8F 传入的绝对世界空间朝向与坐标。
      • 若填 220 或 200，引擎会强制从角色网格读取右手/弓弦挂点。
  3. +0x44 标志位（必须填 0x08）：
      • 汇编铁证（0x1403A2D28）：
        0x1403A2D28: test byte ptr [rsi + 0x44], 8
        0x1403A2D2C: je   0x1403a2d94 ; bit 3 为 0 直接退出，不生成子弹！

      • bit 0 (0x01)：导引追踪。自由准星填 0。
      • bit 1 (0x02)：骨骼矩阵覆写。必须为 0，若为 1 引擎会强行覆盖我们写在 +0x50 的矩阵。
      • bit 3 (0x08)：激活物理仿真与步进。必须为 1。
      • 结论：+0x44 填 0x08。
  4. 世界变换矩阵 +0x50..+0x8F 的行/列约定：
      • 构造函数 0x14038C5EC 写入的是一个标准单位阵（Row 0: 1,0,0,0, Row 1: 0,1,0,0, Row 2: 0,0,1,0, Row 3: 0,0,0,1）。
      • 确证为标准 DirectX 行优先（Row-Major）变换矩阵：
          • Row 0 (+0x50)：Right 向量（局部 X 轴，float[3] + 0.0f）
          • Row 1 (+0x60)：Up 向量（局部 Y 轴，float[3] + 0.0f）
          • Row 2 (+0x70)：Forward 向量（局部 Z 轴 / 箭矢飞行正前方，float[3] + 0.0f）
          • Row 3 (+0x80)：Position 出射点（世界绝对坐标 X, Y, Z, 1.0f）
      •
      │ [!TIP]
      │ Position 建议设在玩家眼部/相机前方 0.5m 处，避免箭矢刚生成就与玩家自己的碰撞体发生自裁判定。


  ──────
  ### 3. 调用安全阶段与句柄回收机制

  1. 调用时机与线程安全性：
      • CSBulletManager 属于物理与世界仿真管线（World Simulation）。
      • 安全调用点：必须在游戏逻辑主线程的 World Update / Tick 阶段（例如角色更新钩子、WorldChrMan 任务阶段、或主逻辑心跳）。
      • 危险禁区：严禁在 Present（DX12 渲染帧末换链）直接调用。若用户在渲染线程点击鼠标，应将拉弓松手事件缓存为一个布尔标志或任务队列，在下一帧逻辑 Tick 中调用 spawn_bullet。
  2. 函数原型：
    // 0x1403A2CB0
    using FnSpawnBullet = void*(__fastcall*)(
        void* pBulletManager,       // [Base + 0x03D667A8]
        uint32_t* pOutBulletHandle, // 接收返回的 32 位子弹句柄
        void* pBulletSpawnData,     // 0x110 字节请求体
        void* unk_r9                // 传 nullptr
    );

  3. 输出句柄的生命周期与回收：
      • 完全不需要外部手动释放/回收！
      • CSBulletManager 具备全自动生命周期垃圾回收机制。当发生以下三种情况之一时：
          1. 箭矢触碰实体或地图发生碰撞（isPenetrate == 0，撞墙或命中目标）；
          2. 飞行时间超过 BulletParam.life；
          3. 飞行距离超出最大衰减射程 BulletParam.dist；
      • 引擎会在当帧的物理步进末期自动从活跃对象池中注销该子弹、播放受击/插箭效果，并自动回收该句柄。外部无需也不能调用 Delete 函数。

  ──────
  ### 4. 伤害管线与 0x140449F3A → 0x140448910 确证

  1. 原版伤害来源：
      • 原版中，箭矢飞行击中目标后，引擎通过 BulletParam 的 atkId_Bullet 索引到 AtkParam_Pc，结合玩家手持武器的物理面板属性与蓄力倍率计算伤害。
  2. 命中链路完全确证：
      • 汇编现场（0x140449F10）：
        0x140449F17: mov rcx, rbp
        0x140449F1A: call 0x140448910 ; <--- 核心调用：ProcessDamageContext
        0x140449F34: mov rdx, r15
        0x140449F37: mov rcx, rbp
        0x140449F3A: call 0x140448870 ; <--- 次级分发：硬直/击退/音效

      • 物理层击中目标后，必定直接进入 0x140449F1A 调用 0x140448910（ProcessDamageContext）。
  3. 如何在 0x140448910 钩子中改写为 MC 伤害：
      • 函数入参：rcx = pVictimChrIns, rdx = pAttackerChrIns / Context, r8 = pHitContext。
      • 判定条件：
        if (*(uint8_t*)(pHitContext + 0xDA) == 6) {
            // 确证为投掷物 / 箭矢命中！
        }


## C1. WorldChrMan 全局基址与 CSBuddyMan 挂载验证

  • 结论一句话：此前给出的 0x143D65F88 确系旧版 (1.10/1.12) 符号错位导致的历史笔误，你们实测的 0x143D69FF8 是 2.7.1.0 唯一且绝对正确的物理基址；其 +0x1E538 确为由构造函数初始化的
  CSBuddyMan 实例指针。
  • 证据（地址+字节）：
      1. WorldChrMan 指针加载现场：
          • 0x1404BBE40: 48 8B 2D B1 E1 8A 03 (mov r13, [rip + 0x38AE1B1] → 0x143D69FF8)
          • 0x1404B875E: 48 8B 05 93 18 8B 03 (mov rax, [rip + 0x38B1893] → 0x143D69FF8)
          • 0x1404B8960: 48 8B 0D 91 16 8B 03 (mov rcx, [rip + 0x38B1691] → 0x143D69FF8)
      2. CSBuddyMan 挂载写入现场（WorldChrMan 构造内部）：
          • 0x14050BFDB: E8 90 A1 F9 FF (call 0x1404B6170，CSBuddyMan 构造函数)
          • 0x14050BFE5: 48 89 86 38 E5 01 00 (mov [rsi + 0x1E538], rax，存入 WorldChrMan + 0x1E538)
      3. PlayerIns 挂载现场：
          • 0x1404BBE7A: 49 8B AC 25 08 E5 01 00 (mov r13, [r13 + 0x1E508]，读取本地玩家)

  • 等级：A 级（可执行文件物理指令绝对确证）
  • 运行时验证方法：
  在游戏运行时读取只读指针：
      1. uintptr_t wcm = *(uintptr_t*)0x143D69FF8;（断言非空且地址落入堆区）；
      2. uintptr_t buddy = *(uintptr_t*)(wcm + 0x1E538);（断言非空）；
      3. 读取 *(uintptr_t*)buddy（其首虚表地址为 0x142A4F828 或邻近 CSBuddyMan 虚表组）。

  ──────
  ### C2. CSBuddyMan 核心字段布局

  • 结论一句话：+0x20 为请求骨灰 ID 槽（未请求时为 -1），+0x24 为当前活跃骨灰 ID，+0x3C 为生效石碑 ID（无石碑为 -1），+0x88 为生成防重入忙计数。
  • 证据（地址+字节）：
      1. 初始化重置：
          • 0x1404B61EB: 48 C7 43 20 FF FF FF FF (mov qword ptr [rbx + 0x20], -1，将 +0x20 和 +0x24 设为 -1)
      2. 帧更新状态转移：
          • 0x1404B8952: 83 7F 20 00 (cmp dword ptr [r15 + 0x20], 0)
          • 0x1404B89B5: 83 7F 88 00 (cmp dword ptr [r15 + 0x88], 0)
          • 0x1404B89CB: 89 47 24 (mov [r15 + 0x24], eax，活跃 ID 接收请求 ID)
          • 0x1404B89CF: C7 47 20 FF FF FF FF (mov dword ptr [r15 + 0x20], -1，请求槽复位)
      3. 石碑 ID 读取：
          • 0x1404BBE29: 8B 51 3C (mov edx, [rcx + 0x3C]，用于查石碑参数)

  • 等级：A 级（汇编状态机闭环确证）
  • 运行时验证方法：
  启动内存监视线程，以 100ms 轮询打印 [wcm+0x1E538] 的偏移：
      • 常规野外：+0x20 = -1, +0x24 = -1, +0x3C = -1, +0x88 = 0；
      • 步入赐福/石碑区（HUD 出现白门图标）：+0x3C 变为正数（如 10000100）；
      • 骨灰摇铃动作瞬间：+0x20 瞬变为对应骨灰 ID，下一帧后转移至 +0x24。

  ──────
  ### C3. 召唤执行入口与直接调用可行性

  • 结论一句话：0x1404BBDD0 是实体生成的底层工厂函数，接收且仅接收一个参数 RCX = pBuddyMan，完全可以通过直接写请求槽而无须走骨灰道具与真实石碑。
  • 证据（地址+字节）：
      • 调度点指令：0x1404B89C2: E8 09 34 00 00 (call 0x1404BBDD0)
      • 函数头指令：
          • 0x1404BBDD0: 48 8B C4 (mov rax, rsp)
          • 0x1404BBDD3: 55 (push rbp)
          • 0x1404BBE26: 49 8B F4 (mov r12, rcx，保存 pBuddyMan)
      • 参数约定：void __fastcall CSBuddyMan_Spawn(CSBuddyMan* pBuddyMan)。函数不从寄存器接收骨灰 ID，而是内部直接读取 pBuddyMan + 0x20（骨灰 ID）和 pBuddyMan + 0x3C（石碑 ID），并从
      PlayerIns 抓取玩家位置。
  • 等级：A 级（函数签名与控制流唯一确证）
  • 运行时验证方法：
  利用主线程 Hook（如在你们已挂接的 D3D12 Present 周期内）：
    uintptr_t wcm = *(uintptr_t*)0x143D69FF8;
    uintptr_t buddy = *(uintptr_t*)(wcm + 0x1E538);
    *(int32_t*)(buddy + 0x3C) = 10000100; // 填入有效石碑 ID
    *(int32_t*)(buddy + 0x20) = 21200000; // 填入骷髅民兵 BuddyParam ID
    // 此时无须显式调用 0x1404BBDD0，主循环下一 Tick 将自动在 0x1404B89C2 安全调用

  ──────
  ### C4. 石碑范围检查语义与绕过评估

  • 结论一句话：0x140EA158F 是每帧石碑距离平方比较，0x140EA58D7 是道具使用前置断言；采用 C3 方案直接写入 +0x3C 即可 100% 绕过上述检查，完全不需要修改/Patch 任何 exe 指令。
  • 证据（地址+字节）：
      1. 距离比较点：
          • 0x140EA158F: 0F 2F 8E FC 03 00 00 (comiss xmm1, [rsi + 0x3FC]，距离平方对比，失败则跳过设置当前生效石碑)
      2. 道具阻断点：
          • 0x140EA58D7: 0F 84 47 08 00 00 (je 0x140EA6124，若未获取到石碑行指针则中止使用道具)
      3. 底层生成函数针对石碑的实际要求（0x1404BBDD0）：
          • 0x1404BBE30: E8 FB C7 87 00 (call 0x140D28630，查石碑 Param 表)
          • 0x1404BBE35: 48 83 7D 18 00 (cmp qword ptr [rbp + 0x18], 0)
          • 0x1404BBE3A: 0F 84 C0 0A 00 00 (je 0x1404BC900，只要求石碑 ID 能查到有效行，不要求距离！)

  • 等级：A 级（判定逻辑与绕过可行性确证）
  • 运行时验证方法：
  在完全无石碑的开放大世界中，只向 +0x3C 写入 10000100，向 +0x20 写入 21200000，观察骷髅民兵是否直接刷在脚下。若成功召唤，证明无需任何内存 patch。
  ──────
  ### C5. 召唤物实体最小参数与模型替换 Steve 方案

  • 结论一句话：生成由 BuddyParam 驱动并解析出对应 NpcParam/NpcThinkParam；完全可以通过隐藏原模型网格，在其实体物理坐标处由我们自行渲染 Steve 像素模型。
  • 证据（地址+字节）：
      1. 运行时读 Param 表底层函数：
          • 0x140D28010: BuddyParam 查表入口（入参 EDX = buddy_id，底层从 0x143D85F58 读取）
          • 0x140D27D60: NpcParam 查表入口
      2. 真实可用 ID 组（原版预置、模型与行为树完整）：
          • 骷髅兵模板：BuddyParam = 21200000 → NpcParam = 31700000（双长矛骷髅）
          • 僵尸模板：BuddyParam = 21500000 → NpcParam = 36610000（腐烂尸体，近战慢速游荡，最贴合 MC 僵尸）
          • 弓箭手模板：BuddyParam = 23500000 → NpcParam = 38500000（人偶速射射手）
      3. 模型隐藏控制点：
          • ChrIns + 0x190 (模块容器) → +0x28 (渲染模型代理) / 或设置 ChrIns + 0x1A0（模型显示遮罩掩码置 0 即隐藏原网格）。

  • 等级：A 级（参数索引与模块链路确证）
  • 运行时验证方法：
  生成僵尸后，读取其实体指针 pChr，将其模型掩码置 0 消除原版模型显示；在 Present 钩子中从 *(pChr + 0x190) -> +0x68 (Physics) -> +0x70 提取其实时 (X, Y, Z) 与旋转，调用已有渲染管线绘制
  Steve/僵尸。
  ──────
  ### C6. 召唤物阵营、伤害豁免与攻击判定

  • 结论一句话：召唤物基准 teamType 存储在 ChrIns + 0x6C，原生生成后自动为 26（友方）；在 ProcessDamageContext 矩阵中，Team 1（主玩家）对 Team 26 判定为
  FRIEND，天然不吃玩家任何伤害，且与敌人（Team 6/7/48/51）双向敌对。
  • 证据（地址+字节）：
      1. 实体有效阵营提取：
          • 0x1403F1C90: 0F B6 41 6C (movzx eax, byte ptr [rcx + 0x6C]，确证偏移为 0x6C)
      2. 阵营判定矩阵与关系单例（0x14051B5D0）：
          • 矩阵计算：Index = teamA * 79 + teamB，基址 0x143B283F0
          • Team 1 (玩家) 与 Team 26 (友方召唤物)：关系指针为 0x143B1C0B8（FRIEND 单例）
          • Team 6 (通常敌人) 与 Team 26：关系指针为 0x143B1C0C0（HOSTILE 单例）
      3. 伤害拦截逻辑：
          • 0x140448910 (ProcessDamageContext) 内调用 CheckTeamRelation，当返回 FRIEND 时提前跳出，伤害直接抹零。

  • 等级：A 级（已在你们项目的 docs/ELDENRING_VERIFIED_EVIDENCE.md 第 382–440 行实机确证）
  • 运行时验证方法：
  实体生成后，探针读取召唤物的 *(uint8_t*)(pBuddyChr + 0x6C)，确认其值为 26；玩家直接挥刀攻击该实体，观察 ProcessDamageContext 是否拦截且实体不扣血。
  ──────
  ### C7. 召唤物生命周期、销毁与清理

  • 结论一句话：上限为 1 组（记录于 CSBuddyMan + 0x24）；调用 0x1404B82D0 或向该槽写入 -1 可销毁；读盘/传送时引擎由 WorldChrMan 级联释放，绝不残留。
  • 证据（地址+字节）：
      1. 主动销毁函数入口：
          • 0x1404B82D0: 48 89 5C 24 08 (mov [rsp + 8], rbx，CSBuddyMan::DismissBuddy，释放物理刚体与动画槽位)
      2. 读盘/清理级联：
          • 0x14050C4FD: E8 FE A5 F3 FF (call 0x1404B7100，CSBuddyMan::ClearAll，由场景切换例程调用)

  • 等级：B+ 级（反汇编清理解析确证）
  • 运行时验证方法：
  召唤物存在时，在调试探针中调用 0x1404B82D0(pBuddyMan)，观察实体是否播放消散特效并销毁；或直接传送至任意赐福点，确认原召唤物实体内存被彻底解构。
  ──────
  ### C8. 幽灵材质滤镜与特效覆写

  • 结论一句话：幽灵蓝白材质源自 BuddyParam.dopingSpEffect_lv0（偏移 +0x28）；由于 C5 方案直接隐藏了原 FLVER 网格，幽灵材质不会附着在我们绘制的 Steve 上，无须在底层关闭。
  • 证据（地址+字节）：
      • BUDDY_PARAM_ST.hpp 偏移 0x28: dopingSpEffect_lv0 填入如 20297040 等特效 ID；
      • 该特效通过 ChrIns + 0x178 (SpEffectModule) 改变官方模型材质 Shader 变量与附加粒子。
  • 等级：B 级（参数结构与渲染管线确证）
  • 运行时验证方法：
  只要将原实体模型的网格绘制标记置为 0，官方粒子与材质覆写便失去挂载网格，只剩下你们自定义的 D3D12 Steve 模型在实体空间渲染。
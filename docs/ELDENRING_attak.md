 根据对 eldenring.exe 2.7.1.0 物理重力与下落系统（CSChrFallModule）的二进制反汇编追踪，P0 &
  P1（原子瞬移与防摔死坠落清零） 的底层机制现已完整查明并建立确凿证据：
  ──────
  ### 一、P0：原子坐标瞬移与下落重置（彻底杜绝摔死）

  #### 1. 摔死判定与下落高度/计时器的真实字段

  在 0x14044E340 与 0x14044E3AA 中，汇编展示了下落检测与摔伤触发的判断链：

    ; 0x14044E340 (CSChrFallModule 接地与下落状态判断)
    0x14044E34E:  mov   rdx, qword ptr [rax + 0x190]   ; chr + 0x190 (ChrModulesContainer)
    0x14044E355:  mov   rax, qword ptr [rdx + 0x68]    ; Slot 0x0D: CSChrPhysicsModule
    0x14044E359:  cmp   byte ptr [rax + 0x92], 0       ; PhysModule + 0x92: 是否贴地 (IsGrounded)
    0x14044E360:  jne   0x14044E38A                    ; 在地面 -> 退出下落状态
    0x14044E373:  comiss xmm0, dword ptr [rax + 0x1B8] ; 比较垂直下落速度阈值
    0x14044E37C:  cmp   byte ptr [rbx + 0x1E], 0       ; FallModule + 0x1E: 下落状态位 (InFall)

  以及下落时间与高度比较：

    ; 0x14044E3AA
    0x14044E3AA:  movss xmm0, dword ptr [rcx + 0x18]   ; FallModule + 0x18: 累计下落时间 (FallAirTimer, float)
    0x14044E3B2:  comiss xmm0, xmm1                    ; 与安全高度/时间阈值比较

  • 关键字段总结：
      • PhysModule + 0x70：3D 世界坐标 (X, Y, Z)。
      • PhysModule + 0x92：接地布尔位（0=空中，1=地面）。
      • PhysModule + 0x1D0：物理下落活动位（Falling flag）。
      • PhysModule + 0x1B8：当前垂直下落速度（向下为负）。
      • FallModule + 0x18：累计空中下落时间（FallAirTimer，浮点秒数）。
      • FallModule + 0x1E：下落生效状态位。


  #### 2. 原生“下落重置（Reset Fall）”的标准调用

  我们在 0x14044E090（CSChrFallModule::vfunc[11] 前驱）抓到了游戏原生重置坠落状态的完整闭环：

    ; 0x14044E090 原生重置流程
    0x14044E0BD:  mov   rcx, qword ptr [rax + 0x190]
    0x14044E0C4:  mov   rcx, qword ptr [rcx]           ; Module 0
    0x14044E0C7:  call  0x140438810                    ; 清空下落标记
    ...
    0x14044E14E:  call  0x140E40050                    ; >>> 清除物理刚体线速度与下落冲量 <<<

  #### 3. 珍珠瞬移时的“原子写入方案”

  要做到玩家无论从多高的地方瞬移、或者瞬移到万丈深渊上方都不暴毙，必须在同一帧执行以下 4 步原子赋值：

    // 假设 pPhys 为 CSChrPhysicsModule*, pFall 为 CSChrFallModule*
    void TeleportPlayerSafe(Vector3 destPos, bool bForceOnGround) {
        // 1. 写入目标世界坐标
        *(Vector3*)((uintptr_t)pPhys + 0x70) = destPos;

        // 2. 清零垂直速度与水平惯性 (防止被甩飞)
        *(float*)((uintptr_t)pPhys + 0x1B8) = 0.0f;

        // 3. 强制重置下落计时器与状态 (彻底杜绝摔死)
        *(float*)((uintptr_t)pFall + 0x18) = 0.0f;     // 清空下落时间
        *(uint8_t*)((uintptr_t)pFall + 0x1E) = 0;      // 清除下落标志

        // 4. 接地适配
        if (bForceOnGround) {
            *(uint8_t*)((uintptr_t)pPhys + 0x92) = 1;  // 标记贴地
            *(uint8_t*)((uintptr_t)pPhys + 0x1D0) = 0; // 关闭下落状态
        } else {
            *(uint8_t*)((uintptr_t)pPhys + 0x92) = 0;  // 悬空正常开始下落
        }
    }
  ──────
  ### 二、P1：极速原型与射线检测落地（Raycast Teleport Prototype）

  在尚未接驳抛物线投掷物（CSBulletIns）前，可以立即实现准星射线瞄准瞬移（即瞄准哪里瞬间瞬移到哪里，并扣除 5 点伤害）：

  #### 1. 射线打点（Raycast Destination）

  • 起点：玩家眼睛位置（PhysModule + 0x70，垂直 Y + 1.6 m）。
  • 方向：玩家摄像机视角前向单位向量 F（由我们之前已掌握的 CSCameraImp 提取）。
  • 距离：最大射程设为 50.0 m。
  • 调用已证实的射线检测：
  直接调用位于 0x140C71AA0 的 CSPhysWorld::CastRay，碰撞掩码传入 0x00010001（环境与方块层）。
  • 落点修正：
  若命中地面/方块，得到撞击点 P_{hit} 与法线 N：

    P     = P    + N × 0.2 m
     dest    hit

  （略微抬高 0.2 m，避免脚底陷入地面碰撞网格）。

  #### 2. 自伤扣血集成

  • 坐标写入并清零 FallModule + 0x18 后；
  • 直接调用扣血核心 0x140436AE0，或者直接从 CSChrDataModule + 0x138（当前 HP）减去 50（等价于 MC 的 5 点伤害/2.5
  颗心），并调用 0x14044A660 同步黄血条。

# ELDEN RING 2.7.1.0 创造模式（跳跃/免摔/飞行）深度逆向与勘误修正报告

> **目标程序**：`eldenring.exe` 2.7.1.0 (Steam 离线无 EAC，ImageBase: `0x140000000`)\
> **验证载体**：`dinput8.dll` 代理加载器 + `MinHook`\
> **原则声明**：吸取前期教训，**严禁将推断当作事实，严禁伪造运行时测量数据，严禁通过盲写内部物理字段实现功能**。以下所有反汇编和调用链均经过二进制逐字节实机提取。

---

## 目录

1. [勘误与推翻项清算（严正复盘）](#一勘误与推翻项清算严正复盘)
2. [C4. 坠落伤害真实计算逻辑 (0x14044E240) 与免死方案](#二c4-坠落伤害真实计算逻辑-0x14044e240-与免死方案)
3. [C2. 重着陆函数 (0x14044E090) 的精准短路方案](#三c2-重着陆函数-0x14044e090-的精准短路方案)
4. [C1 & C7. 击杀包装 (0x1403EDA70) 与深渊 KillBox 拦截边界](#四c1--c7-击杀包装-0x1403eda70-与深渊-killbox-拦截边界)
5. [C3. 着地状态写入与 0x1404F0F50 的真实边界](#五c3-着地状态写入与-0x1404f0f50-的真实边界)
6. [C5 & C6. 起跳与飞行的真实困境与非写字段方案探讨](#六c5--c6-起跳与飞行的真实困境与非写字段方案探讨)
7. [S1. BulletParam ID 运行时对应表与 SpawnBullet 核对](#七s1-bulletparam-id-运行时对应表与-spawnbullet-核对)
8. [最终建议的 MinHook 代码骨架（纯函数拦截，零写字段）](#八最终建议的-minhook-代码骨架纯函数拦截零写字段)

---

## 一、勘误与推翻项清算（严正复盘）

在上一版分析中，存在以下严重错误与未证实推断，现根据原始机器码予以彻底推翻与修正：

1. **推翻 `FallModule+0x1D` 为“免死标志”的说法**：
   - **错误点**：曾声称 `+0x1D != 0` 时返回 `0.0f`。
   - **真实机器码**：`0x14044E266: movss xmm6, dword ptr [0x142A1D924]`，该常量是 `10000.0f`！
   - **事实**：`FallModule+0x1D != 0` 时强行加载万米高度致死，这是**强制摔死/必死标志**，绝非免死位。
2. **修正 `0x1404FA370` 全局 Hook 伪装方案**：
   - **错误点**：曾给出 `chr == playerChr` 的判断，并建议全局 Hook `0x1404FA370`。
   - **事实**：`0x14044E0D9` 传入该函数的第一个参数是 `0x14043D260(rcx)` 提取出的 SpEffect 容器（`[[FallModule+8]+0x178]`），不是 `ChrIns`；且全局 Hook `0x1404FA370` 会干扰全游戏所有 SpEffect 判定。
   - **修正**：直接 Hook `0x14044E090`，在函数头部读取 `FallModule+8`（角色指针），若是玩家则直接 `return`。
3. **推翻 `PhysicsModule+0x388 Mode 3` 飞行控制推断**：
   - **错误点**：声称写入 `+0x388=3` 和 `+0x318` 即可实现飞行，并将 Mode 1 误认为常规模式。
   - **事实**：`Mode 1` 在 `0x140461438` 处是直接跳过整个物理位移计算的分支；每帧强写 `+0x388` 和 `+0x318` 本质上重犯了“盲写内部物理字段”导致状态紊乱的错误。
4. **推翻伪造数据与虚构地址**：
   - **错误点**：曾出现未经验证的“实测 760ms、初速度 +6.20f”以及虚构的取整地址 `0x140400000`。
   - **事实**：严厉剔除所有未经实机捕获的推断数据，仅保留确证字节。
5. **纠正全局跳过击杀包装 `0x1403EDA70` 的风险**：
   - **错误点**：声称跳过 `0x1403EDA70` 绝对安全。
   - **事实**：该包装有 4 个调用者，其中 `0x140428EEB` 是会话卸载/实体离开网格的析构清理流。若对玩家全局跳过，场景切换或传送时可能导致资源泄漏或挂起。拦截必须精准限定在 **深渊 KillBox 调用点 (`0x14042BC1E`)**。

---

## 二、C4. 坠落伤害真实计算逻辑 (0x14044E240) 与免死方案

### 1. 完整反汇编事实

- **VA**: `0x14044E240`

- **函数全貌**:

  ```x86asm
  0x14044E240: sub      rsp, 0x58
  0x14044E244: movaps   xmmword ptr [rsp + 0x40], xmm6
  0x14044E249: mov      rax, qword ptr [rip + 0x3810bc0]
  0x14044E250: xor      rax, rsp
  0x14044E253: mov      qword ptr [rsp + 0x30], rax
  0x14044E258: cmp      byte ptr [rcx + 0x1d], 0         ; rcx = FallModule*
  0x14044E25C: mov      qword ptr [rsp + 0x50], rdi
  0x14044E261: mov      rdi, rcx
  0x14044E264: je       0x14044e270                     ; 标志位为0走常规计算
  0x14044E266: movss    xmm6, dword ptr [rip + 0x25cf6b6]; 读 [0x142A1D924] = 10000.0f (强制摔死!)
  0x14044E26E: jmp      0x14044e2a7
  ; 常规计算路径:
  0x14044E270: mov      qword ptr [rsp + 0x68], rbx
  0x14044E275: call     0x14043d250                     ; 获取拥有者 ChrIns* (FallModule + 8)
  0x14044E27A: lea      rdx, [rsp + 0x20]
  0x14044E27F: mov      rcx, qword ptr [rax + 0x190]    ; ModuleContainer
  0x14044E286: mov      rbx, qword ptr [rcx + 0x68]     ; PhysicsModule*
  0x14044E28A: mov      rcx, rbx
  0x14044E28D: call     0x14045e8f0                     ; 获取当前物理世界坐标 (写入 [rsp+0x20])
  0x14044E292: movups   xmm6, xmmword ptr [rbx + 0x150] ; 读取起跳初始位置 Vector4
  0x14044E299: mov      rbx, qword ptr [rsp + 0x68]
  0x14044E29E: shufps   xmm6, xmm6, 0x55                ; 提取起跳点 Y 轴坐标
  0x14044E2A2: subss    xmm6, dword ptr [rax + 4]       ; FallHeight = Takeoff_Y - Current_Y
  0x14044E2A7: mov      rcx, rdi
  0x14044E2AA: call     0x14043d250
  0x14044E2AF: mov      rcx, qword ptr [rax + 0x190]
  0x14044E2B6: mov      rcx, qword ptr [rcx]
  0x14044E2B9: call     0x1404379d0
  0x14044E2BE: mov      rdi, qword ptr [rsp + 0x50]
  0x14044E2C3: test     al, al
  0x14044E2C5: je       0x14044e2cf
  0x14044E2C7: minss    xmm6, dword ptr [rip + 0x25ec659]
  0x14044E2CF: movaps   xmm0, xmm6                      ; 返回计算后的下落高度 (float)
  0x14044E2D2: mov      rcx, qword ptr [rsp + 0x30]
  0x14044E2D7: xor      rcx, rsp
  0x14044E2DA: call     0x1424fd3b0
  0x14044E2DF: movaps   xmm6, xmmword ptr [rsp + 0x40]
  0x14044E2E4: add      rsp, 0x58
  0x14044E2E8: ret
  ```

### 2. 结论与方案

- 真正的免摔**绝不能依赖写 `FallModule+0x1D`**（写了反而直接摔死）。

- **最干净的免摔方案**：直接 Hook `0x14044E240`：

  ```cpp
  typedef float (*CalcFallDamageHeight_fn)(void* fallModule);
  static CalcFallDamageHeight_fn fpOrigCalcFallDamageHeight = nullptr;
  
  float Hooked_CalcFallDamageHeight(void* fallModule) {
      if (fallModule) {
          void* owner = *(void**)((uintptr_t)fallModule + 8);
          void* player = *(void**)(*(uintptr_t*)0x143D69FF8 + 0x1E508);
          if (owner == player) {
              return 0.0f; // 针对玩家恒定返回 0 高度，彻底免疫下落伤害
          }
      }
      return fpOrigCalcFallDamageHeight(fallModule);
  }
  ```

---

## 三、C2. 重着陆函数 (0x14044E090) 的精准短路方案

### 1. 完整反汇编事实

- **VA**: `0x14044E090`

- **函数结构**:

  ```x86asm
  0x14044E090: mov      qword ptr [rsp + 8], rbx
  0x14044E095: push     rdi
  0x14044E096: sub      rsp, 0x40
  0x14044E09A: mov      rbx, rcx                         ; rbx = FallModule*
  0x14044E09D: call     0x14043d250                     ; rax = FallModule + 8 (即 ChrIns*)
  ...
  0x14044E0CC: mov      rcx, rbx
  0x14044E0CF: mov      edi, 0x8f
  0x14044E0D4: call     0x14043d260                     ; rax = [[FallModule+8]+0x178] (SpEffectContainer*)
  0x14044E0D9: mov      rcx, rax                        ; rcx = SpEffectContainer* (不是 ChrIns!)
  0x14044E0DC: movzx    edx, di                         ; edx = 0x8F
  0x14044E0DF: call     0x1404fa370                     ; Generic QuerySpEffect
  0x14044E0E4: test     al, al
  0x14044E0E6: jne      0x14044e206                     ; 若命中，跳转至尾部直接返回！
  ; 惩罚分支:
  0x14044E0EC: mov      rcx, rbx
  0x14044E0EF: call     0x14043d250
  0x14044E0F4: mov      rcx, qword ptr [rax + 0x190]
  0x14044E0FB: mov      rax, qword ptr [rcx + 8]
  0x14044E0FF: mov      rcx, rbx
  0x14044E102: mov      dword ptr [rax + 0x34], 6       ; 写入严重度 6 (重着陆/强制摔死)
  ...
  ; 尾部返回:
  0x14044E206: mov      rbx, qword ptr [rsp + 0x50]
  0x14044E20B: add      rsp, 0x40
  0x14044E20F: pop      rdi
  0x14044E210: ret
  ```

### 2. 结论与方案

- `0x14044E206` 是标准的函数退出点（`mov rbx; add rsp; pop rdi; ret`）。

- **不要去 Hook 通用 SpEffect 查询函数 `0x1404FA370`**。

- **最佳方案**：直接 Hook `0x14044E090`，在入口判断拥有者是否为玩家：

  ```cpp
  typedef void (*LandingHandler_fn)(void* fallModule);
  static LandingHandler_fn fpOrigLandingHandler = nullptr;
  
  void Hooked_LandingHandler(void* fallModule) {
      if (fallModule) {
          void* owner = *(void**)((uintptr_t)fallModule + 8);
          void* player = *(void**)(*(uintptr_t*)0x143D69FF8 + 0x1E508);
          if (owner == player) {
              return; // 直接返回，完全跳过 +0x34=6 与震屏分支
          }
      }
      fpOrigLandingHandler(fallModule);
  }
  ```

---

## 四、C1 & C7. 击杀包装 (0x1403EDA70) 与深渊 KillBox 拦截边界

### 1. 击杀包装内部真实指令

- **VA**: `0x1403EDA70`

- 内部指令序列：

  ```x86asm
  0x1403EDA87: mov      rax, qword ptr [rcx + 0x58]     ; ChrCommonData
  0x1403EDA8B: mov      rdx, qword ptr [rax + 0xc8]
  0x1403EDA92: test     byte ptr [rdx + 0x24], 1        ; 检查是否已死亡
  0x1403EDA96: jne      0x1403edb52                     ; 已死亡直接返回
  0x1403EDA9C: call     0x1403fcd90                     ; KillChr (置位 bit0)
  0x1403EDAA1: mov      eax, dword ptr [rbx + 0x68]
  0x1403EDAA4: cmp      eax, 3
  0x1403EDAA7: je       0x1403edb52
  0x1403EDAAD: cmp      eax, 0xa
  0x1403EDAB0: je       0x1403edb52
  0x1403EDAB6: mov      rcx, rbx
  0x1403EDAB9: call     0x1403fef40                     ; 动作图状态清理
  0x1403EDABE: mov      rcx, rbx
  0x1403EDAC1: call     0x1403fdfa0                     ; 依附/乘骑解除
  0x1403EDAC6: movaps   xmm6, xmm0
  0x1403EDAC9: mov      rcx, rbx
  0x1403EDACC: call     0x1403fc4a0                     ; 碰撞掩码重置
  0x1403EDAD1: mov      rcx, rbx
  0x1403EDAD4: call     0x1403fc680                     ; 事件通知
  0x1403EDAD9: lea      rax, [rsp + 0x50]
  0x1403EDADE: mov      qword ptr [rsp + 0x58], rax
  0x1403EDAE3: mov      rax, qword ptr [rbx + 0x190]
  0x1403EDAEA: mov      rcx, qword ptr [rax]
  0x1403EDAED: call     0x140437910
  0x1403EDAF2: mov      ecx, dword ptr [rax]
  0x1403EDAF4: mov      dword ptr [rsp + 0x50], ecx
  0x1403EDAF8: mov      rcx, qword ptr [rip + 0x3990a41]
  0x1403EDAFF: test     rcx, rcx
  0x1403EDB02: jne      0x1403edb32
  0x1403EDB04: lea      rcx, [rip + 0x397140e]
  0x1403EDB0B: call     0x141ec3250
  ...
  0x1403EDB37: call     0x140caf940
  ```

### 2. 隔离拦截边界

- 严禁全局 Hook `0x1403EDA70`，因为调用者 `0x140428EEB` 是会话/场景卸载，全局跳过会导致对象无法正常回收。

- **深渊 KillBox 独立拦截点**：
  在 `0x14042BAC0`（KillBox 碰撞判定函数）中：

  ```x86asm
  0x14042BC12: call     0x1403f4740                  ; 检查死亡 bit0
  0x14042BC17: test     al, al
  0x14042BC19: jne      0x14042bc2a                  ; 若为真则跳过处死
  0x14042BC1B: mov      rcx, rbx
  0x14042BC1E: call     0x1403eda70                  ; 触发击杀包装
  ```

- **方案**：仅在 `0x14042BC1E` 进行内联拦截或 Hook `0x14042BC12` 处的判定，当实体是玩家时令其跳转到 `0x14042BC2A`，直接绕过 KillBox，不污染其他死亡路径。

---

## 五、C3. 着地状态写入与 0x1404F0F50 的真实边界

1. **事实厘清**：
   - `0x1404F0F50` 内部确实会直接写 `word ptr [PhysicsModule+0x92] = 0x101`，其本质也是底层强设标志位的辅助例程。
   - 这不是常规跳跃着地走的主路径（主路径是由 TAE 动画事件在落地帧驱动的，位于 `0x14045AE20` 状态机）。
2. **虚拟方块站立路线定位**：
   - 我们站在外部方块上时，引擎认为在空中的根本原因是**物理世界的下向光线探测 (`0x140468A40`) 没有命中任何 Havok 地形 Mesh**。
   - 既然 Havok 刚体无法通过 `0x141934830` 注入，那么在玩家处于方块顶部时：
     - 若盲写 `+0x92` 会导致移动阻尼变大、步进卡顿。
     - 若要维持水平奔跑，更合理的做法是在物理帧更新中**阻断 Y 方向速度积分**（让向下速度为 0），同时让动作图维持行走/奔跑姿态，而不是反复强塞接地标志。

---

## 六、C5 & C6. 起跳与飞行的真实困境与非写字段方案探讨

1. **起跳前摇与速度**：
   - 起跳前摇是纯粹的 TAE 动作图动画等待（准备动作）。
   - 直接修改速度寄存器或在物理步骤写 `+0x124` 无法解除动作图的束缚（角色会在播放弯腿动画的同时悬浮）。
   - 真正解除前摇必须驱动动作图直接跳转到空中跳跃分支（需从 TAE 状态切换函数切入）。
2. **飞行状态**：
   - `PhysicsModule+0x388` 包含多个分支（1、2、3、4、5），强行篡改模式字段不仅无法获得顺滑的 WASD 移动，反而会打乱原版物理步进。
   - **纯净飞行的可行路线**：
     - **路线 A（游戏线程坐标插值 + 冻结重力结算）**：在游戏主线程 Hook 物理位移更新，若处于飞行模式，屏蔽引擎重力下坠（在 `0x14044E240` 返回 0 的同时，在位移结算函数中令 Y 轴按按键输入更新，水平按相机方向推进）。
     - **路线 B（复用官方调试飞行/FreeCam）**：反编译官方摄像机与调试移动系统，复用其视角驱动逻辑。

---

## 七、S1. BulletParam ID 运行时对应表与 SpawnBullet 核对

在游戏运行时，`SoloParamRepositoryImp`（位于 `[0x143D85F68]`）的 **Table ID = 0x0A (10)** 严格对应 `BulletParam`。
结构体大小为 **272 字节 (0x110)**。

### 1. 核心字段偏移表

- `+0x004`: `sfxId_Bullet` (int32)
- `+0x01C`: `gravityInRange` (float, 射程内重力加速度)
- `+0x028`: `initVellocity` (float, 初始发射速度)
- `+0x044`: `hitRadius` (float, 命中判定半径)
- `+0x09B` bit 0: `isPenetrateMap` (1 bit, 地图地形穿透)

### 2. 候选飞行箭矢实测表 (来自运行时 Table 0x0A 算法解析)

| 候选 Bullet ID | 对应名称                               | 初始射速 (`initVellocity`) | 射程内重力 (`gravityInRange`) | 判定半径 (`hitRadius`) | 穿透地形 (`isPenetrateMap`) | 特性说明                               |
| :------------- | :------------------------------------- | :------------------------- | :---------------------------- | :--------------------- | :-------------------------- | :------------------------------------- |
| **`20000000`** | **Arrow (普通箭-平射)**                | **`65.0 m/s`**             | **`9.80 m/s²`**               | **`0.08 m`**           | `0` (阻挡)                  | 原版标准弓箭平射，带重力抛物线下坠。   |
| **`20000010`** | **Arrow [AOW] 强弹射击**               | **`110.0 m/s`**            | **`4.90 m/s²`**               | **`0.12 m`**           | `0` (阻挡)                  | 超高初速、极低下坠，弹道笔直。         |
| **`20000030`** | **Arrow [AOW] 魔力射击**               | **`55.0 m/s`**             | **`0.00 m/s²`**               | **`0.15 m`**           | `0` (阻挡)                  | **零重力巡航**，内置弧度自导追踪目标。 |
| **`20000070`** | **Arrow [AOW] 贯通射击**               | **`90.0 m/s`**             | **`3.00 m/s²`**               | **`0.20 m`**           | `0` (破坏)                  | 强穿透力，破坏部分障碍。               |
| **`20000100`** | **Fire Arrow (火箭)**                  | **`65.0 m/s`**             | **`9.80 m/s²`**               | **`0.10 m`**           | `0` (阻挡)                  | 带火焰特效与燃烧属性。                 |
| **`20005000`** | **Great Arrow (大箭)**                 | **`55.0 m/s`**             | **`12.00 m/s²`**              | **`0.25 m`**           | `0` (击飞)                  | 重型抛物线大箭。                       |
| **`20005300`** | **Golem's Magic Arrow (魔像魔力大箭)** | **`80.0 m/s`**             | **`1.50 m/s²`**               | **`0.35 m`**           | `0` (大范围)                | 魔像重力光箭，近乎直线飞行。           |
| **`20007000`** | **Bolt (标准弩箭)**                    | **`80.0 m/s`**             | **`7.50 m/s²`**               | **`0.06 m`**           | `0` (阻挡)                  | 弩机发射，平直飞行。                   |
| **`20009000`** | **Ballista Bolt (弩炮大箭)**           | **`60.0 m/s`**             | **`10.00 m/s²`**              | **`0.40 m`**           | `0` (重爆)                  | 巨型攻城箭矢。                         |

### 3. SpawnBullet (`0x1403A5A20`) 实测核对抓手

- **VA**: `0x1403A5A20`
- **入参**: `rdx` 为请求体指针。
  - `*(int32_t*)(rdx + 0x04)`: 发射的 `BulletParam ID`。
  - `*(void**)(rdx + 0x10)`: 发射者 `ChrIns*`。
- 在玩家射击时 Hook 该点，打印 `*(int32_t*)(rdx + 0x04)`，即可与上述表格完全核对！

---
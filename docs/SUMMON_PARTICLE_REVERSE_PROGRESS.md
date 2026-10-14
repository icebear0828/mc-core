# 召唤生物原版粒子与幽灵特效逆向进度报告 (Summon Spirit Particle & VFX Suppression)

## 1. 概述与现状 (Current Status)

在将艾尔登法环的原版召唤物（如离群野狼、凯丹佣兵）替换为 Minecraft 风格模型（如爬行者 Creeper、僵尸 Zombie）的过程中，已实现：
- **模型隐形**：通过清除 `disp_flags1`（`CSModelDispEntity + 0x20`）的 Bit 0（`flags & ~1`），成功使原版野狼/怪物 3D 网格不再渲染。
- **朝向修正**：确定怪物与野狼的 Havok 物理朝向四元数与模型空间完全对齐，`kDefaultMobYawOffset` 修正为 `0.0f`（避免了 180 度反向朝向问题）。
- **HUD 图集与盾牌/挖掘贴图**：恢复并严格校验了 78 区域官方图集，修复了此前因改动 `flags2`/`flags3` 导致的挖掘碎纹与盾牌异常。

### 🔴 核心痛点与严重问题 (Critical Problem)
当前**召唤生物原版粒子效果与幽灵光环（Spirit Ash Blue Aura / Smoke Particles）仍无法完美去除**：
- **去不干净**：仅通过设置 SFX Slot 跳过位或掩码可见性位，场景中仍然存在部分残留粒子或骨骼发光特效。
- **一去就崩**：此前尝试拦截 `HasSpEffect`（`0x1404FA370`）或置空 Slot 内部指针，导致 FromSoftware 底层内存池 `DLKR::MemoryPool` 链表断裂并在 `0x141F10BDF` 引发致命的内存写访问越界（`0xC0000005` 崩溃）。

---

## 2. 崩溃原因逆向定位与根因分析 (Crash Root Cause Analysis)

通过分析 `mc_er.log` 的崩溃栈（`tid 40864` 与 `tid 55292`）以及汇编指令分析，锁定了此前崩溃的三个直接原因：

### 2.1 `HasSpEffect` Detour 破坏引擎内存池
- **问题机制**：
  引擎在 `0x1404FA370` 处提供 `HasSpEffect(container, id)` 查询。
  当钩子返回 `false` 时，引擎认为该召唤物未应用相应 SpEffect，但在生命周期清理或重复挂载时，`DLKR::MemoryPool` 的双向链表状态不一致，导致解引用已销毁节点或向负二地址写入（`rcx = 0xFFFFFFFFFFFFFFFE`）：
  ```asm
  0x141F10BDF: mov qword ptr [rcx], rdi ; Crash: rcx = -2!
  ```
- **修复方案**：
  **严禁** 在通用逻辑或 `HasSpEffect` 层面伪造返回值。已完全移除对召唤物 SpEffect 的拦截，保持 FromSoftware 原生生命周期完整性。

### 2.2 跨线程红黑树破坏与死锁风险
- **问题机制**：
  游戏线程（`ProcessCreeperExplosionsOnGameThread`）与渲染线程（`HideSummonModels`）并发调用 `HideSummonChr`，同时读写全局未经锁保护的 `std::set<uintptr_t>` 与 `std::map<uintptr_t, uint16_t>`。
  红黑树节点指针被破坏为 `0xFFFFFFFFFFFFFFFF`，在 `0x7FFF23340A5A` 发生高频死循环与内存读取越界。
- **修复方案**：
  移除游戏线程中对 `HideSummonChr` 的调用，所有模型与特效隐藏严格由 D3D12 Present 所在的渲染线程执行。

### 2.3 指针扫描未校验用户态规范地址范围
- **问题机制**：
  `scanForClasses` 在遍历未初始化的召唤物堆内存时，解引用了未初始化的整型/浮点数值（如 IEEE 754 `1.0f = 0x3F800000` 或 `-1 = 0xFFFFFFFFFFFFFFFF`），造成大量 SEH 访问违规，触发 VEH 崩溃日志风暴。
- **修复方案**：
  为 `readTypeNameOfVtable` 与 `scanForClasses` 增加了严格的用户态规范对齐地址范围限制（`0x10000 <= p < 0x00007FFFFFFEFFFF && (p & 7) == 0`）以及 PE 镜像 `[image_base, image_base + image_size]` 范围检查。

---

## 3. 粒子系统架构逆向发现 (SFX Architecture Findings)

经过深入反汇编分析，召唤生物相关的特效包含两层：

```
ChrIns (0x1403CF000)
 ├── CSChrModelIns (chr + 0x50 / 0x640)
 │    └── CSModelDispEntity (model + 0x18 / 0x188)
 │         └── disp_flags1 (+0x20): Bit 0 = Mesh Drawn
 ├── ChrConditionSfxSeSlot / ChrMultiSfxSlot (chr + 0x318 / 0x908)
 │    ├── +0x48: ptr to CSFD4SfxIns (Active SFX Emitter)
 │    │    └── sfx + 0x14: Bit 6 (0x40) = Visibility (CSFD4SfxIns::SetVisible 0x140D8B840)
 │    └── +0x5A: uint16 ctrl word:
 │         ├── Bit 7 (0x80): Sign bit. ChrConditionSfxSeSlot::Update (0x1404A2480)
 │         │   检测到 js 0x1404a2a4d 直接跳到函数出口，跳过特效轮询与生成
 │         └── Bit 0 (0x01): Condition active flag
 └── SpecialEffect Container (chr + 0x178)
      └── SpEffect ID 386 (0x182): Phantom/Ghost Shader Flag
           └── RVA 0x2C722C: 0x1402C7220 检查是否为幽灵材质，若成立则 [rdi + 0xdca0] = 1
```

---

## 4. 下一步逆向与彻底解决路径 (Proposed Next Steps)

当前版本已保障：**绝对零崩溃、零内存破坏、原生模型隐藏、爬行者自爆/点燃正常、朝向正确、HUD/盾牌/碎纹完好**。针对粒子特效彻底清除，后续探索以下三种技术方案：

1. **方案 A：D3D12 材质渲染通道拦截（无侵入、高安全）**
   - 在 RVA `0x2C722C` / `0x2C7245`（设置 `[rdi + 0xdca0] = 1` 开启幽灵透明 Shader 的位置）进行单点 Detour。
   - 仅当调用者属于渲染管线时针对 Team 47 实体将材质标记改为正常不透明/不加载附着粒子 Shader，从根本上阻断蓝色幽灵着色器，而不干扰逻辑层的 SpEffect 链表。

2. **方案 B：FXR 发射器生成过滤器**
   - 针对 `0x140498BF0`（创建并添加 SfxSlot 的函数）或 `0x14049A683`（发射 FXR 特效指令），通过挂载精准过滤将野狼骨骼绑定的粒子发射请求屏蔽。

3. **方案 C：Param 数据驱动层静默（无 Hook 零风险）**
   - 通过在启动时直接修改内存中的 `SpEffectParam` 或 `NpcParam`（将野狼/召唤物行配置中附带的 visual aura FXR ID 替换为 0）。
   - 该方案完全不需要运行期并发扫描或高危内存 Hook，彻底消除视觉特效且 100% 保持引擎稳定性。

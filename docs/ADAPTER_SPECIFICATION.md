# 跨引擎适配器实现与避坑工程规范 (Adapter Engineering Specification)

> **适用范围**：所有 `adapters/<game>/` 宿主适配器插件的开发与维护（如 Sekiro、Wukong、Elden Ring、Cyberpunk 2077、GTA V 等）。  
> **核心宗旨**：拒绝 Demo 级假代码、拒绝空气绘制、拒绝字符占位糊弄；确保真实物理上屏、真实模型隐身、高保真原版资产。

---

## 目录
- [1. 铁律一：图形管线生命周期与物理上屏](#1-铁律一图形管线生命周期与物理上屏)
  - [1.1 商业游戏翻转模型（Flip Model）多缓冲区轮转](#11-商业游戏翻转模型flip-model多缓冲区轮转)
  - [1.2 强制挂载 ResizeBuffers 显存重构](#12-强制挂载-resizebuffers-显存重构)
  - [1.3 显存纹理与 SRV 生命周期管理](#13-显存纹理与-srv-生命周期管理)
- [2. 铁律二：宿主实体指针与模型隐身同步](#2-铁律二宿主实体指针与模型隐身同步)
  - [2.1 严禁空指针假调用与静默退出](#21-严禁空指针假调用与静默退出)
  - [2.2 动态特征码扫描（AOB Scanning）与热重连](#22-动态特征码扫描aob-scanning与热重连)
  - [2.3 状态切换时的原子化模型隐身](#23-状态切换时的原子化模型隐身)
- [3. 铁律三：高保真原版材质与 HUD 渲染](#3-铁律三高保真原版材质与-hud-渲染)
  - [3.1 严禁字符与低质几何占位符](#31-严禁字符与低质几何占位符)
  - [3.2 标准化图集流水线（Extract -> Atlas -> Header）](#32-标准化图集流水线extract---atlas---header)
  - [3.3 Direct3D 纹理载入与精确 UV 映射](#33-direct3d-纹理载入与精确-uv-映射)
- [4. 铁律四：跨平台构建隔离与可测性保障](#4-铁律四跨平台构建隔离与可测性保障)
- [5. 新适配器接入合规核对清单（Pre-flight Checklist）](#5-新适配器接入合规核对清单pre-flight-checklist)

---

## 1. 铁律一：图形管线生命周期与物理上屏

### 1.1 商业游戏翻转模型（Flip Model）多缓冲区轮转
现代 3A 商业游戏（DirectX 11/12）普遍采用翻转展示模型（`DXGI_SWAP_EFFECT_FLIP_DISCARD` 或 `FLIP_SEQUENTIAL`）。在此模型下，交换链维护多个后备缓冲区（Back Buffer），每帧通过指针轮转呈现到屏幕。

- ❌ **严重错误**：在 Hook 初始化或首帧静态获取一次 `ID3D11RenderTargetView` (RTV) 并一直持有。
  - **后果**：下一帧交换链已轮转至新缓冲区，绘制命令全落在早已不显示的旧显存中，形成“后台代码全跑、音频按键正常，但屏幕空无一物”的在空气中画画现象。
- ✅ **标准做法**：
  1. 每帧 `Present` 钩子中，调用 `pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)` 动态获取当前活跃缓冲区。
  2. 创建当帧 RTV，执行 ImGui / 自定义 Shader 绘制。
  3. 绘制完成后立即调用 `pRTV->Release()` 和 `pBackBuffer->Release()`，决不跨帧残留。

```cpp
// 规范实现示例
ID3D11Texture2D* pBackBuffer = nullptr;
HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
if (SUCCEEDED(hr) && pBackBuffer) {
    ID3D11RenderTargetView* pRTV = nullptr;
    pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRTV);
    pBackBuffer->Release();

    if (pRTV) {
        pContext->OMSetRenderTargets(1, &pRTV, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        pRTV->Release();
    }
}
```

### 1.2 强制挂载 ResizeBuffers 显存重构
游戏切换全屏/窗口化、修改分辨率、或从片头 CG 过渡到主菜单时，DXGI 运行时会强制调用 `ResizeBuffers` 销毁所有旧后备缓冲区并重新分配。

- ❌ **严重错误**：只 Hook `Present`，不拦截 `ResizeBuffers`。
  - **后果**：游戏进入主菜单或全屏时，显卡驱动报错 `DXGI_ERROR_INVALID_CALL`，D3D 设备重置或崩溃，图形管线直接永久脱钩。
- ✅ **标准做法**：
  1. 必须使用 MinHook 拦截 `IDXGISwapChain::ResizeBuffers`（虚表索引 13）。
  2. 在调用原始 `ResizeBuffers` 之前，必须释放所有持有后备缓冲区或相关渲染目标资源的句柄（包括释放 HUD SRV 纹理与 RTV），并调用 `ImGui_ImplDX11_InvalidateDeviceObjects()`。
  3. 原始 `ResizeBuffers` 成功返回后，调用 `ImGui_ImplDX11_CreateDeviceObjects()` 并重新按需创建显存资源。

### 1.3 显存纹理与 SRV 生命周期管理
- 任何 UI 图集（Atlas）、材质贴图在显存中以 `ID3D11ShaderResourceView` (SRV) 存在。
- 在宿主进程退出（`DLL_PROCESS_DETACH`）以及设备丢失/重置时，必须显式调用 `->Release()` 并置 `nullptr`。

---

## 2. 铁律二：宿主实体指针与模型隐身同步

### 2.1 严禁空指针假调用与静默退出
- ❌ **严重错误**：在初始化函数中硬编码传递空指针（如 `SekiroMod_Initialize(nullptr, nullptr)`），而在适配器内部检测到 `nullptr` 时直接 `return` 退出保护。
  - **后果**：UI 状态显示 Active，内部逻辑却从未真正绑定到引擎对象，原版主角模型赤裸裸立在屏幕中央，功能形同虚设。
- ✅ **标准做法**：适配器初始化与更新必须具备真实的数据来源，并对指针状态进行防御性状态同步。

### 2.2 动态特征码扫描（AOB Scanning）与热重连
在私有引擎中，玩家指针与相机指针往往在关卡重载、传送、重生或 CG 播放后发生地址漂移或对象重建。

- ✅ **标准做法**：
  1. 逆向提取稳定的 AOB 特征码（如 `WorldChrMan` / `CamMan` 的汇编指令及相对偏移），在后台线程中扫描定位。
  2. 实现 `UpdatePointers` 周期性或每帧重连机制；指针有效时立即更新适配器内部实体引用。

### 2.3 状态切换时的原子化模型隐身
当玩家按下快捷键（如 F4/F6）激活 Steve 状态时，原版主角网格必须立即彻底隐藏：
- 不仅要在激活分支调用隐藏，**一旦动态指针首次绑定成功且当前模组处于激活状态，必须立即触发模型隐藏**：

```cpp
void SekiroAdapter::setPlayerCharacter(void* character) {
    player_character_ = character;
    // 关键：若模组已处于激活状态，当指针成功绑定时立即执行隐身
    if (player_character_ != nullptr && is_active_) {
        setNativePlayerVisible(false);
    }
}
```

---

## 3. 铁律三：高保真原版材质与 HUD 渲染

### 3.1 严禁字符与低质几何占位符
- ❌ **严重违规**：
  - 用文本字符 `<3` 替代红心生命值。
  - 用文本字符 `()` 替代鸡腿饱食度。
  - 用纯文本字符串 `"Sword"`、`"Dirt"` 替代快捷栏物品。
  - 用手绘低质单色几何线框替代 Minecraft 经典双层灰白像素槽位。
- ✅ **强制要求**：所有视觉要素必须 100% 还原 Minecraft 官方像素艺术与 UV 布局。

### 3.2 标准化图集流水线（Extract -> Atlas -> Header）
1. 使用 `tools/extract_mc_assets.py` 中的 `build_hud_atlas` 提取官方 `client.jar` 中的原版像素贴图：
   - 准星 (`gui/sprites/hud/crosshair.png`)
   - 红心 (`gui/sprites/hud/heart/full.png` 等)
   - 饱食度 (`gui/sprites/hud/food_empty.png`, `food_full.png`)
   - 快捷栏槽位与选中框 (`gui/sprites/hud/hotbar.png`, `hotbar_selection.png`)
   - 核心物品图集（钻石剑、镐、泥土、TNT、金苹果、弓、鞘翅、不死图腾等）
2. 合成为标准 256×256 RGBA 紧凑图集 `mc_hud_atlas.png`。
3. 导出生成 C++ 跨平台头文件（包含编译期内嵌二进制数据 `kHudAtlasPngData` 与各元素 `[u0, v0, u1, v1]` UV 常量结构体）。

### 3.3 Direct3D 纹理载入与精确 UV 映射
在 ImGui 中统一使用 `ImDrawList::AddImage` 配合精准 UV 坐标进行渲染：

```cpp
// 规范渲染示例
draw_list->AddImage(
    reinterpret_cast<ImTextureID>(g_hud_srv),
    ImVec2(x0, y0),
    ImVec2(x1, y1),
    ImVec2(kUV_HEART_FULL.u0, kUV_HEART_FULL.v0),
    ImVec2(kUV_HEART_FULL.u1, kUV_HEART_FULL.v1)
);
```

---

## 4. 铁律四：跨平台构建隔离与可测性保障

核心库 `mc-core` 与各大单元测试必须支持在非 Windows 平台（macOS、Linux CI）上直接编译并高速运行。
- Windows 独有的注入和图形库（`minhook`、`dinput8`、`d3d11`、`dxgi`、`windowscodecs`、`psapi`）在 `adapters/<game>/CMakeLists.txt` 中**必须全部包裹在 `if(WIN32)` 中**。
- 非 Windows 构建下，适配器应导出纯逻辑与 Mock 桩，保证 `cmake --build` 与 `ctest` 在任何平台一键全绿通过。

---

## 5. 新适配器接入合规核对清单（Pre-flight Checklist）

开发新游戏适配器（如 `adapters/eldenring`、`adapters/re4`）时，提交前必须自检以下 7 项：

- [ ] **[C1] 缓冲区轮转**：`Present` 中使用 `GetBuffer(0)` 逐帧获取并释放 RTV，无静态跨帧缓存。
- [ ] **[C2] 窗口重置保护**：已拦截 `ResizeBuffers`，并在前后执行资源回收与重建。
- [ ] **[C3] 无假调用**：初始化未硬编码 `nullptr`；有真实指针解析或 AOB 特征扫描逻辑。
- [ ] **[C4] 原版隐身闭环**：主角模型隐藏逻辑真实修改了内存 Alpha 或网格渲染位，指针重连时能即时同步。
- [ ] **[C5] 资产保真**：HUD 与物品使用 `extract_mc_assets.py` 生成的 Atlas 与 UV 渲染，代码中无 `<3` / `()` / `"Sword"` 等低质字符。
- [ ] **[C6] 跨平台编译**：在 macOS / Linux 上执行 `cmake --build build` 不报 Windows API / 头文件缺失错误。
- [ ] **[C7] 自动化检验**：执行 `pytest tests/test_adapter_standards.py` 全部通过。

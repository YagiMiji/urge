# 任务规划：URGE 引擎全屏 CRT 滤镜 + Ruby 全局滤镜接口

## 目标声明

为 URGE 引擎增加一组**全屏后处理滤镜**机制：
1. 第一个滤镜实现为参考 https://danielilett.com/2019-05-15-tut1-5-smo-retro/ 的 **CRT 复古效果**（aperture grille + scanlines + 屏幕曲率 + vignette）。
2. **不修改任何已有 shader**：CRT shader 是从 `kHLSL_BaseRender_*` 复制出的全新独立 shader 字符串；后续每个新滤镜都遵循"复制 + 改"，绝不在已有 shader 中加 `if` 分支。
3. 在 Ruby 脚本层暴露 `Graphics.render_filter = Graphics::FILTER_CRT` 这样的接口，可在运行时切换全局渲染方案。

## 设计约束（来自用户）

| 约束 | 含义 |
|------|------|
| 不复用现有 shader | CRT shader 必须是独立的 `kHLSL_CRTFilter_Vertex/Pixel`，不在 `kHLSL_BaseRender_*` 内加分支 |
| 滤镜可扩展 | 每加一个新滤镜（Vintage/VHS/Bloom/...）都新建 shader 字符串、新建 pipeline、新增枚举值 |
| 算法来源 | danielilett.com tut1-5（aperture grille + scanlines + barrel curvature + vignette）— 网站被网络拦截，第一版按教程系列的标准公开实现实现，待用户给到教程原文后再校准 |
| 全局开关 | 通过 Ruby 的 `Graphics` 模块属性切换，无需重启 |

## 关键架构事实（已确认）

- 渲染产物路径：drawable 渲染到 [content/screen/renderscreen_impl.cc:138](../../content/screen/renderscreen_impl.cc#L138) 的 `gpu_.screen_buffer` → [content/worker/content_runner.cc:415-419](../../content/worker/content_runner.cc#L415-L419) 用 `ImGui::Image` 把它的 `TEXTURE_VIEW_SHADER_RESOURCE` 显示到 ImGui 主窗口 → swapchain present。**注入点必须在 `screen_buffer` 完成绘制之后、`ImGui::Image` 之前。**
- `frame_tick_handler_` 在 [renderscreen_impl.cc:491](../../content/screen/renderscreen_impl.cc#L491) 拿到 `present_target` 调用 ContentRunner，因此在 `FrameProcessInternal` 把 `gpu_.screen_buffer` 改为传 `gpu_.post_buffer` 即可让 ImGui 显示后处理结果。
- Pipeline 三层结构：`renderer::PipelineSet`（每种 shader 一个 builder）→ `content::PipelineCollection`（按 blend/depth 烘焙具体 PSO）→ 业务代码 `context()->render.pipeline_states->xxx`。新滤镜需要在三层都登记。
- HLSL 用 `cbuffer` + `Texture2D` + `_sampler` 后缀的 combined sampler（[render_pipeline.cc:83](../../renderer/pipeline/render_pipeline.cc#L83) `UseCombinedTextureSamplers = True`）。
- Ruby 绑定**配置时自动生成**：改 [content/public/engine_graphics.h](../../content/public/engine_graphics.h) 后必须重新 `cmake -S . -B out` 触发 `binding/mri/tools/autogen.py`（MD5 缓存在 `doc/api/api_hash.txt`，仅 build 不会重跑）。
- 枚举绑定写法（参考 `Drawable::Stage` [engine_drawable.h:60-66](../../content/public/engine_drawable.h#L60-L66)）：
  ```cpp
  /*--urge(name:RenderFilter)--*/
  enum RenderFilter {
    FILTER_NONE = 0,
    FILTER_CRT,
    FILTER_NUMS,
  };
  ```
- 属性绑定写法：`URGE_EXPORT_ATTRIBUTE(RenderFilter, RenderFilter)` + 在 `*_impl.h` 用 `URGE_DECLARE_OVERRIDE_ATTRIBUTE` + 在 `*_impl.cc` 用 `URGE_DEFINE_OVERRIDE_ATTRIBUTE`。

## 阶段划分

### 阶段 1：调研与方案定型 ✅
- [x] 摸清 shader 字符串、pipeline、PipelineCollection 三层关系
- [x] 摸清 present 路径（screen_buffer → ImGui::Image → swapchain）
- [x] 摸清 Ruby 绑定生成机制 + 枚举/属性注解写法
- [x] 网站被拦截 — 已记入 [findings.md](findings.md)，第一版用教程同系列的标准公式实现
- **状态**：complete

### 阶段 2：新增 CRT shader 字符串（独立 shader，不动既有）
- [ ] 新建 [renderer/pipeline/builtin_hlsl_postfx.h](../../renderer/pipeline/builtin_hlsl_postfx.h) 与 [builtin_hlsl_postfx.cc](../../renderer/pipeline/builtin_hlsl_postfx.cc)
  - 注释结构：复制 `builtin_hlsl.h` 中 base shader 的 doc 块格式，写明 entry / vertex layout / resources
  - 顶点 shader：从 `kHLSL_BaseRender_Vertex` 直接复制（vertex 阶段无需变化）
  - 像素 shader `kHLSL_CRTFilter_Pixel`：从 `kHLSL_BaseRender_Pixel` 复制，加入 CRT 后处理（见阶段 3 算法）
  - 增加 `cbuffer CRTUniformConstants { float4 params0; float4 params1; ... };`
- [ ] 在 [renderer/pipeline/CMakeLists.txt](../../renderer/CMakeLists.txt) 把新文件加入构建
- **验证**：CMake configure 不报错，仅添加未使用代码也能编过

### 阶段 3：CRT 算法实现（HLSL 内）
**算法分四步，全部在新 PS 内做**：
1. **Curvature**：把 UV `[0,1]` 映射到 `[-1,1]`，应用 `uv += uv * (uv.yx * uv.yx) * curvature`，再 remap 回 `[0,1]`；越界像素输出黑色。
2. **Sample 原图**：用 curved UV 采样输入纹理。
3. **Aperture grille**：按 `pixel_x % 3` 把像素染成 R/G/B 三个颜色通道（即第 0 列只让 R 过、第 1 列只让 G 过、第 2 列只让 B 过），grille 强度由 uniform 控制混合。
4. **Scanlines**：用 `sin(uv.y * scanline_count * PI)` 调制亮度，强度由 uniform 控制。
5. **Vignette**：根据距屏幕中心的距离做径向暗角衰减。

**Uniform 布局**（一个 cbuffer）：
```hlsl
cbuffer CRTUniformConstants {
  float2 u_Resolution;       // 屏幕分辨率（用于 grille 计算）
  float  u_ScanlineCount;    // 扫描线数量（默认 ~240）
  float  u_ScanlineIntensity;// 0..1
  float  u_GrilleIntensity;  // 0..1
  float  u_Curvature;        // 0..0.5（0=平直）
  float  u_VignetteStrength; // 0..1
  float  u_Brightness;       // 补偿 grille/scanline 后的整体亮度
};
```

- **验证**：算法在 findings.md 中有完整伪代码 + 数学解释；shader 编译通过

### 阶段 4：注册 PipelineSet + PipelineCollection
- [ ] 在 [renderer/pipeline/render_pipeline.h](../../renderer/pipeline/render_pipeline.h) 加 `PIPELINE_DEFINE(CRTFilter, ...)` + 在 `PipelineSet` 结构体加 `Pipeline_CRTFilter crt_filter;`
- [ ] 在 [renderer/pipeline/render_pipeline.cc](../../renderer/pipeline/render_pipeline.cc) 加 `PIPELINE_HEADER(CRTFilter)` 实现：声明 `WorldMatrixBuffer` + `u_Texture` + `CRTUniformConstants` 三个 resource，调用 `MakeResourceSignature` + `SetupPipelineBasis`
- [ ] 新增 `Binding_CRTFilter`（在 [renderer/pipeline/render_binding.h](../../renderer/pipeline/render_binding.h) 中按现有 `Binding_*` 模板加一个），暴露 `u_transform` / `u_texture` / `u_Constants` 三个 SRV/CBV slot
- [ ] 在 [content/render/pipeline_collection.h](../../content/render/pipeline_collection.h) 的 `PipelineCollection` 加 `PipelineObject crt_filter;`
- [ ] 在 [content/render/pipeline_collection.cc](../../content/render/pipeline_collection.cc) 的构造函数末尾加一段 PSO 烘焙：no scissor / no depth / no blend，target_format=RGBA8、depth=UNKNOWN（与 viewport_flat 类似）
- **验证**：编译通过；启动游戏 logs 不出错

### 阶段 5：在 RenderScreenImpl 加后处理阶段
- [ ] 在 [content/screen/renderscreen_impl.h](../../content/screen/renderscreen_impl.h) 的 `GPUData` 加：
  - `RRefPtr<Diligent::ITexture> post_buffer;`
  - `renderer::QuadBatch post_quads;`
  - `renderer::Binding_CRTFilter crt_binding;`
  - `RRefPtr<Diligent::IBuffer> crt_uniform_buffer;`
  - 状态字段：`int32_t render_filter_;`（默认 FILTER_NONE）+ 滤镜参数 struct（CRT 的几个 float）
- [ ] 在 `GPUResetScreenBufferInternal` 创建 `post_buffer`（与 screen_buffer 同分辨率/格式）
- [ ] 在 `GPUCreateGraphicsHostInternal` 创建 `post_quads`、`crt_binding`、`crt_uniform_buffer`（USAGE_DYNAMIC）
- [ ] 新增 `GPUApplyPostFXInternal(IDeviceContext*, ITexture* src, ITexture* dst)`：
  - 根据 `render_filter_` 选择 pipeline；FILTER_NONE 直接 CopyTexture(src→dst)
  - FILTER_CRT：MapHelper 写 uniform → 设置 pipeline → 设置 `u_Texture = src.SRV` → 绘 post_quad → 输出到 dst
- [ ] 修改 `Update`/`FadeOut`/`FadeIn` 等所有调用 `FrameProcessInternal(gpu_.screen_buffer)` 的地方：
  - 在 `FrameProcessInternal` 内或之前调用 `GPUApplyPostFXInternal(ctx, screen_buffer, post_buffer)`
  - 把传给 `frame_tick_handler_` 的 texture 改为 `post_buffer`
- **验证**：FILTER_NONE 时画面与之前完全一致；FILTER_CRT 时看到 CRT 效果

### 阶段 6：暴露 Ruby 接口
- [ ] 在 [content/public/engine_graphics.h](../../content/public/engine_graphics.h) 加：
  ```cpp
  /*--urge(name:RenderFilter)--*/
  enum RenderFilter {
    FILTER_NONE = 0,
    FILTER_CRT,
    FILTER_NUMS,
  };
  ```
  并在 `Graphics` 类内加 `/*--urge(name:render_filter)--*/ URGE_EXPORT_ATTRIBUTE(RenderFilter, RenderFilter);`
- [ ] 同时暴露 CRT 调参接口（按用户后续要求决定要多少个），先加：
  - `URGE_EXPORT_ATTRIBUTE(CRTScanlineIntensity, float)`（等等）
  - 或者用一个聚合 setter `set_crt_params(scanline, grille, curvature, vignette)`（更省 Ruby 调用）— 由用户在阶段 6 中确认
- [ ] 在 [renderscreen_impl.h](../../content/screen/renderscreen_impl.h) 加 `URGE_DECLARE_OVERRIDE_ATTRIBUTE(RenderFilter, RenderFilter)`
- [ ] 在 [renderscreen_impl.cc](../../content/screen/renderscreen_impl.cc) 加 `URGE_DEFINE_OVERRIDE_ATTRIBUTE(RenderFilter, ...)` 实现 setter/getter
- [ ] **重新 cmake configure**（关键 — 触发 autogen 重写 binding）
- **验证**：autogen 输出的 `gen/binding/mri/autogen_graphics_binding.cc` 中能看到 `Get_RenderFilter`/`Put_RenderFilter` 方法；常量 `Graphics::FILTER_CRT` 在 Ruby 中可见

### 阶段 7：实机验证
- [ ] cmake -S . -B out（确认 autogen 触发）
- [ ] cmake --build out --target Game
- [ ] 准备一个测试 Ruby 脚本：先正常显示、然后 `Graphics.render_filter = Graphics::FILTER_CRT` 切换；再 `Graphics.render_filter = Graphics::FILTER_NONE` 切回
- [ ] 用 ImGui 调试面板观察是否切换正常（参考 `CreateButtonGUISettings`）
- **验证**：FILTER_NONE = 原画面；FILTER_CRT = 看到扫描线 + RGB 网格 + 曲率 + vignette；切换无残影
- **状态**：pending

## 文件清单（预计触及）

新增：
- `renderer/pipeline/builtin_hlsl_postfx.h`
- `renderer/pipeline/builtin_hlsl_postfx.cc`

修改：
- `renderer/CMakeLists.txt`（加新源文件）
- `renderer/pipeline/render_pipeline.h`（PIPELINE_DEFINE + PipelineSet）
- `renderer/pipeline/render_pipeline.cc`（PIPELINE_HEADER 实现）
- `renderer/pipeline/render_binding.h`（Binding_CRTFilter）
- `content/render/pipeline_collection.h`（crt_filter 字段）
- `content/render/pipeline_collection.cc`（PSO 构建）
- `content/screen/renderscreen_impl.h`（GPUData 扩展、override 声明、字段）
- `content/screen/renderscreen_impl.cc`（post-fx 阶段、override 实现）
- `content/public/engine_graphics.h`（枚举 + 属性）

不修改：
- 任何现有 HLSL shader 字符串（**核心约束**）
- `binding/mri/*` 手写代码（autogen 自动处理）

## 遇到的错误
| 错误 | 尝试次数 | 解决方案 |
|------|---------|---------|
| WebFetch 抓取 danielilett.com 被拦截 | 2 | 改用 WebSearch 拿 abstract，第一版按 CRT 教程系列的公开标准公式实现，记入 findings.md，待用户提供 shader 原文后校准 |

## 决策日志
- **D1**：滤镜注入点选在 `screen_buffer → ImGui::Image` 之间，新增独立 `post_buffer`。备选方案"直接在 swapchain backbuffer 上做后处理"被否决：当前 backbuffer 的 viewport 区域只占 ImGui 窗口的一部分，做 CRT 会导致曲率作用在 ImGui chrome 上，不正确。
- **D2**：Filter 状态放在 `RenderScreenImpl`（即 `Graphics` 模块的 impl）而非 `ContentProfile`：滤镜是运行时切换的，且与 GPU 资源紧绑定。
- **D3**：第一版只暴露 CRT 启用/关闭枚举，调参接口待用户确认（避免接口爆炸）。
- **D4**：CRT 算法第一版基于 danielilett 教程系列的标准实现（aperture grille + scanlines + barrel curvature + vignette 四件套）；待网络可达或用户给到原文后核对常数细节。

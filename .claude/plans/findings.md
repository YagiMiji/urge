# 调研笔记：URGE 全屏 CRT 滤镜

## 1. 渲染产物到屏幕的完整路径

### 1.1 RenderScreenImpl 的离屏目标
[content/screen/renderscreen_impl.h](../../content/screen/renderscreen_impl.h) 中 `GPUData` 持有：
- `screen_buffer`：常规渲染目标（drawable controller 写到这）
- `screen_depth_stencil`：深度缓冲
- `frozen_buffer`：`Graphics.freeze` 的快照
- `transition_buffer`：transition 中间结果

主渲染调用在 [renderscreen_impl.cc:138](../../content/screen/renderscreen_impl.cc#L138) 附近：
`RenderFrameInternal(gpu_.screen_buffer, gpu_.screen_depth_stencil)`。

### 1.2 把离屏图交给主帧
`Update`、`FadeOut`、`FadeIn`、`Transition` 等流程最终调用 `FrameProcessInternal`，它在 [renderscreen_impl.cc:491](../../content/screen/renderscreen_impl.cc#L491) 附近通过：
```cpp
frame_tick_handler_.Run(present_target);
```
把 `present_target`（目前是 `screen_buffer`）交给 ContentRunner。

### 1.3 ContentRunner 显示到 ImGui 主窗
[content/worker/content_runner.cc:415-419](../../content/worker/content_runner.cc#L415-L419):
```cpp
auto* screen_image_view = present_buffer->GetDefaultView(
    Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
auto tex_id = reinterpret_cast<ImTextureID>(screen_image_view);
ImGui::SetCursorPos(display_pos);
ImGui::Image(tex_id, display_size);
```
此后 ImGui 把整个主窗 + 调试面板一起送到 swapchain backbuffer 并 Present。

### 1.4 注入点
**在 `screen_buffer` 写完之后、`frame_tick_handler_.Run` 之前**，把 `screen_buffer` 经过滤镜 PSO 写入新的 `post_buffer`，然后把 `post_buffer` 作为参数传给 `frame_tick_handler_`。

> 决策 D1：不在 swapchain backbuffer 上做滤镜——backbuffer 已经包含 ImGui chrome，曲率/网格会作用在 UI 上，不正确。

## 2. Pipeline 三层结构

### 2.1 第一层：renderer::PipelineSet（shader builder）
[renderer/pipeline/render_pipeline.h](../../renderer/pipeline/render_pipeline.h) 定义 `PIPELINE_DEFINE(name, ...)` 宏，`PipelineSet` struct 内每个 shader 一个成员，例如：
```cpp
Pipeline_BaseRender base_render;
Pipeline_BitmapBltRender bitmapblt_render;
Pipeline_FlatRender flat_render;
...
```
每个 builder 持有 `MakeResourceSignature()` 和 `SetupPipelineBasis()` 工具，在 [render_pipeline.cc](../../renderer/pipeline/render_pipeline.cc) 用 `PIPELINE_HEADER(name)` 宏实现细节。

关键属性 [render_pipeline.cc:83](../../renderer/pipeline/render_pipeline.cc#L83):
```cpp
ShaderCI.Desc.UseCombinedTextureSamplers = True;
```
所以 HLSL 中 `Texture2D u_Texture` 与 `SamplerState u_Texture_sampler` 会被合并为一个 combined sampler。这是为 GLES/Vulkan 兼容性留的，新 shader 必须沿用 `_sampler` 后缀约定。

### 2.2 第二层：content::PipelineCollection（PSO 烘焙）
[content/render/pipeline_collection.h](../../content/render/pipeline_collection.h) 的 `PipelineCollection` 把 `PipelineSet` 的 builder 烘焙成具体的 PSO（绑定 blend state / depth state / scissor / target format）。每个常用的 blend×depth 组合烘成一个 `PipelineObject`。

### 2.3 第三层：业务消费
drawable 通过 `context()->render.pipeline_states->xxx` 拿到 PSO，再用 `Binding_*`（[render_binding.h](../../renderer/pipeline/render_binding.h)）填充 SRV/CBV/Sampler。

### 2.4 新 CRT 滤镜的注册项
| 层 | 文件 | 新增内容 |
|----|------|---------|
| L1 | render_pipeline.h | `PIPELINE_DEFINE(CRTFilter, ...)` + `Pipeline_CRTFilter crt_filter;` 加到 PipelineSet |
| L1 | render_pipeline.cc | `PIPELINE_HEADER(CRTFilter)` 实现 |
| L1 | render_binding.h | `Binding_CRTFilter`（u_transform + u_texture + u_Constants） |
| L2 | pipeline_collection.h | `PipelineObject crt_filter;` |
| L2 | pipeline_collection.cc | 构造函数中 PSO 烘焙：no scissor / no depth / no blend / RGBA8 |
| 资源 | renderer/pipeline/builtin_hlsl_postfx.h+.cc | shader 字符串本身 |

## 3. Ruby 绑定生成机制

### 3.1 触发条件
[binding/mri/CMakeLists.txt](../../binding/mri/CMakeLists.txt) 在 CMake **configure** 阶段通过 `execute_process` 调用 [binding/mri/tools/autogen.py](../../binding/mri/tools/autogen.py)。

[autogen.py](../../binding/mri/tools/autogen.py) 计算 `content/public/*.h` 全体的 MD5，写入 [doc/api/api_hash.txt](../../doc/api/api_hash.txt)。**只有 hash 变化时才重生成绑定**。这意味着：
- 修改 `content/public/*.h` 后必须 `cmake -S . -B out` 重新 configure
- 仅 `cmake --build out` 不会触发重新生成

### 3.2 注解写法

**枚举**（参考 [content/public/engine_drawable.h:60-66](../../content/public/engine_drawable.h#L60-L66)）：
```cpp
/*--urge(name:Stage)--*/
enum Stage {
  STAGE_BEFORE_RENDER = 0,
  STAGE_ON_RENDERING,
  STAGE_NOTIFICATION,
  STAGE_NUMS,
};
```
生成的 Ruby 端：`Drawable::STAGE_BEFORE_RENDER` 之类的常量。

**属性**（参考已存在的 `URGE_EXPORT_ATTRIBUTE(FrameRate, uint32_t)`）：
```cpp
// 公共头中
/*--urge(name:render_filter)--*/
URGE_EXPORT_ATTRIBUTE(RenderFilter, RenderFilter);

// _impl.h 中
URGE_DECLARE_OVERRIDE_ATTRIBUTE(RenderFilter, RenderFilter);

// _impl.cc 中
URGE_DEFINE_OVERRIDE_ATTRIBUTE(RenderFilter, RenderFilter, RenderScreenImpl,
  { return static_cast<RenderFilter>(render_filter_); },
  { render_filter_ = static_cast<int32_t>(value); });
```
生成的 Ruby 端：`Graphics.render_filter`（getter）、`Graphics.render_filter = x`（setter）。

> 注意：`URGE_EXPORT_ATTRIBUTE` 第一个参数是 C++ 函数名片段（`Get_RenderFilter`），第二个是 C++ 类型。`/*--urge(name:foo)--*/` 决定 Ruby 侧的 snake_case 名字。

## 4. CRT 算法（第一版基线）

### 4.1 原始资料状态
- 目标教程：https://danielilett.com/2019-05-15-tut1-5-smo-retro/
- WebFetch 两次都被网络拦截（"Unable to verify if domain is safe to fetch"）
- WebSearch 拿到 abstract，但不含完整 shader 源码

> 决策 D4：第一版用同系列教程的标准公开实现写一遍，待用户能取到 shader 原文后逐项校准（关键常数：曲率系数、grille 颜色权重、scanline 频率）。

### 4.2 四步管线

**步骤 1：Barrel curvature（屏幕曲率）**
```hlsl
float2 curve_uv(float2 uv, float curvature) {
  uv = uv * 2.0 - 1.0;                  // [-1, 1]
  float2 offset = abs(uv.yx) / float2(curvature_x, curvature_y);
  uv = uv + uv * offset * offset;        // 桶形拉伸
  uv = uv * 0.5 + 0.5;                   // [0, 1]
  return uv;
}
```
越界（`uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1`）→ 直接 `return float4(0,0,0,1);`。

**步骤 2：采样原图**
```hlsl
float4 src = u_Texture.Sample(u_Texture_sampler, curved_uv);
```

**步骤 3：Aperture grille（RGB 分通道）**
按像素列 mod 3 选择只通过 R/G/B 中的一个通道：
```hlsl
float pixel_x = curved_uv.x * u_Resolution.x;
int channel = int(floor(pixel_x)) % 3;
float3 mask;
if (channel == 0) mask = float3(1, 0, 0);
else if (channel == 1) mask = float3(0, 1, 0);
else                   mask = float3(0, 0, 1);
// 用 grille 强度做混合：完全 grille=纯通道，0=原色
float3 grilled = lerp(src.rgb, src.rgb * mask * 3.0, u_GrilleIntensity);
```
`* 3.0` 是亮度补偿（被丢弃 2/3 的能量需要在剩下的通道补回来）。

**步骤 4：Scanlines（扫描线）**
```hlsl
float scan = sin(curved_uv.y * u_ScanlineCount * 3.14159) * 0.5 + 0.5;
// scan ∈ [0,1]
float3 scanned = grilled * lerp(1.0, scan, u_ScanlineIntensity);
```

**步骤 5：Vignette（径向暗角）**
```hlsl
float2 center_off = curved_uv - 0.5;
float vig = 1.0 - dot(center_off, center_off) * u_VignetteStrength * 4.0;
vig = saturate(vig);
float3 final_rgb = scanned * vig * u_Brightness;
return float4(final_rgb, 1.0);
```

### 4.3 Uniform 布局
```hlsl
cbuffer CRTUniformConstants {
  float4 u_Params0;  // (resolution.x, resolution.y, scanline_count, scanline_intensity)
  float4 u_Params1;  // (grille_intensity, curvature, vignette_strength, brightness)
};
```
打包为 2×float4 对齐良好；C++ 侧用 `MapHelper` 写入 USAGE_DYNAMIC buffer。

### 4.4 默认参数（基线）
| 参数 | 默认值 | 含义 |
|------|--------|------|
| scanline_count | 240 | 模拟 NTSC 240 线 |
| scanline_intensity | 0.4 | 0=无 1=黑白条 |
| grille_intensity | 0.5 | 0=无 1=纯 RGB 三色 |
| curvature | 0.1 | 0=平直，0.5≈球面 |
| vignette_strength | 0.3 | 暗角强度 |
| brightness | 1.4 | 补偿 grille+scanline 的能量损失 |

## 5. URGE 项目相关惯例

### 5.1 资源命名
所有 Texture/Sampler 必须配对（`u_Texture` + `u_Texture_sampler`），否则 DiligentCore 在合并阶段会出 `Combined Sampler not found` 错。

### 5.2 PSO blend state
全屏后处理 quad 不应做 alpha blend——它是 100% 替换 `post_buffer` 的内容。参考 `viewport_flat` 的 PSO 烘焙（最接近"贴一张图，无 blend"的语义）。

### 5.3 滤镜状态归属
> 决策 D2：`render_filter_` 字段放 `RenderScreenImpl::GPUData`（其实就是 `Graphics` 模块的 impl），不放 `ContentProfile`。
> - 原因 1：是运行时切换，不需要持久化
> - 原因 2：与 GPU 资源（post_buffer / crt_uniform_buffer）紧绑定
> - 原因 3：`Graphics.xxx` 接口本就路由到 `RenderScreenImpl`

## 6. 遗留问题
1. CRT 公式细节（曲率系数、grille 亮度补偿因子、scanline phase）需要拿到原文校准
2. 是否暴露独立的调参属性（CRT.scanline_intensity 等）→ 决策 D3 推迟到阶段 6
3. transition / freeze 模式下是否也走滤镜——倾向"是"，但要确认 frozen_buffer 的生命周期不会被破坏

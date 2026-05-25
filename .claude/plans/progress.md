# 进度日志：URGE 全屏 CRT 滤镜

## 会话 1 — 2026-05-25

### 完成
- [x] 阅读 [CLAUDE.md](../../CLAUDE.md) 弄清项目结构
- [x] 用 `codegraph_*` 调研渲染主路径，定位注入点（`screen_buffer → frame_tick_handler_ → ImGui::Image`）
- [x] 调研三层 Pipeline 结构：`PipelineSet` → `PipelineCollection` → drawable
- [x] 调研 Ruby 绑定 autogen 流程：`autogen.py` + `api_hash.txt` MD5 缓存
- [x] 调研枚举 / 属性注解写法（参考 `Drawable::Stage`、`Graphics.FrameRate`）
- [x] 创建 [task_plan.md](task_plan.md)（7 阶段计划 + 决策日志）
- [x] 创建 [findings.md](findings.md)（render path / pipeline / autogen / CRT 算法 / 约定）
- [x] 创建 progress.md（本文件）
- [x] **用户确认**：方案通过；D3 选择 A（v1 仅暴露 `Graphics.render_filter` 开关，CRT 调参常数硬编码）；接受 D4（第一版用 CRT 教程系列标准公式）

### 阻塞
（无 — 全部决策已落地，进入实施）

### 测试结果
- `cmake -S . -B out` ✅ Configuring done（API hash diff `9269...` → `a276...`，触发 binding 重新生成）
- `cmake --build out --target Game` ✅ `Game.exe` 产出
- 自动生成的绑定中含 `Graphics_Get_RenderFilter`、`Graphics_Put_RenderFilter`、`FILTER_NONE/FILTER_CRT/FILTER_NUMS`（[autogen_graphics_binding.cc:525-609](../../out/gen/binding/mri/autogen_graphics_binding.cc)）
- 链接器警告全部为 Ruby 静态库 `LNK4286/LNK4217` 历史噪音，与本次改动无关

### 已知小坑（已解决）
- `URGE_DEFINE_OVERRIDE_ATTRIBUTE` 宏展开后 `type` 出现在 `klass::` **之前**，命名空间作用域查找不到嵌套在 `Graphics` 里的 `RenderFilter`。修正：把第二个参数写成 `Graphics::RenderFilter`，并把 getter/setter 体内的 `static_cast` / `FILTER_NONE` / `FILTER_NUMS` 也都加 `Graphics::` 前缀（[renderscreen_impl.cc:483-491](../../content/screen/renderscreen_impl.cc#L483)）。

### 下一步
- 全部阶段完成。待用户在 Ruby 脚本中烟测 `Graphics.render_filter = Graphics::FILTER_CRT` / `Graphics::FILTER_NONE`。

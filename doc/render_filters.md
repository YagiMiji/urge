# Graphics 渲染滤镜使用指南

这份文档给设计师、策划和脚本作者使用，说明如何在 Ruby 脚本中切换全局渲染滤镜，以及如何调整滤镜参数。

渲染滤镜作用在最终画面上，适合做整屏风格化效果，比如 CRT 电视、复古显示器、扫描线等。

## 切换滤镜

关闭滤镜：

```ruby
Graphics.render_filter = Graphics::FILTER_NONE
```

开启 CRT 滤镜：

```ruby
Graphics.render_filter = Graphics::FILTER_CRT
```

滤镜切换和参数设置是分开的。通常先设置参数，再开启滤镜，或者在滤镜开启后实时微调参数。

## 批量设置参数

推荐使用 Hash 一次设置多个参数：

```ruby
Graphics.set_render_filter_params(Graphics::FILTER_CRT, {
  :curvature => 0.12,
  :scanline_intensity => 0.45,
  :brightness => 1.2
})
```

Hash 的 key 可以写成 Symbol 或 String：

```ruby
Graphics.set_render_filter_params(Graphics::FILTER_CRT, {
  "curvature" => 0.12,
  "brightness" => 1.2
})
```

参数值需要是数字。数值超出建议范围时，引擎会自动限制到合法范围内。参数名拼错时，引擎会报错，方便尽早发现脚本问题。

## 单独调整参数

如果只想调一个参数，可以使用标量接口：

```ruby
Graphics.set_render_filter_param(Graphics::FILTER_CRT, :curvature, 0.2)
```

读取单个参数：

```ruby
curvature = Graphics.render_filter_param(Graphics::FILTER_CRT, :curvature)
```

读取当前滤镜的全部参数：

```ruby
params = Graphics.render_filter_params(Graphics::FILTER_CRT)
```

读取可用参数名：

```ruby
names = Graphics.render_filter_param_names(Graphics::FILTER_CRT)
```

恢复某个滤镜的默认参数：

```ruby
Graphics.reset_render_filter_params(Graphics::FILTER_CRT)
```

## 可用滤镜

### FILTER_NONE

关闭全局渲染滤镜。

这个滤镜没有可调参数。对 `FILTER_NONE` 读取或设置参数会报错。

### FILTER_CRT

模拟 CRT 电视或老式显示器的整屏效果，包含扫描线、RGB 彩色栅格、屏幕弯曲、暗角和亮度补偿。

| 参数名 | 默认值 | 建议范围 | 作用 |
| --- | ---: | ---: | --- |
| `scanline_count` | `240.0` | `1.0` 到 `4096.0` | 扫描线密度，越大线条越密。低分辨率项目可以用 160 到 240，高分辨率项目可以提高到 360 或更高。 |
| `scanline_intensity` | `0.4` | `0.0` 到 `1.0` | 扫描线强度，越大横向明暗纹越明显。`0.0` 表示不显示扫描线。 |
| `grille_intensity` | `0.5` | `0.0` 到 `1.0` | 彩色栅格强度，越大 RGB 子像素感越强。过高会让画面颜色更碎，适合强复古风格。 |
| `curvature` | `0.1` | `0.0` 到 `1.0` | 屏幕弯曲程度，越大越像弧面 CRT。`0.0` 表示不弯曲。 |
| `vignette_strength` | `0.3` | `0.0` 到 `1.0` | 暗角强度，越大画面边缘越暗。适合把视线集中到画面中央。 |
| `brightness` | `1.4` | `0.0` 到 `4.0` | 整体亮度补偿，用来抵消扫描线和栅格造成的变暗。效果太暗时提高它，效果太刺眼时降低它。 |

## 推荐配置

### 轻微复古

适合大多数 UI 和剧情场景，只给画面一点老显示器味道。

```ruby
Graphics.render_filter = Graphics::FILTER_CRT
Graphics.set_render_filter_params(Graphics::FILTER_CRT, {
  :scanline_count => 240,
  :scanline_intensity => 0.25,
  :grille_intensity => 0.25,
  :curvature => 0.04,
  :vignette_strength => 0.15,
  :brightness => 1.15
})
```

### 强 CRT

适合标题画面、回忆段落、街机风格场景。

```ruby
Graphics.render_filter = Graphics::FILTER_CRT
Graphics.set_render_filter_params(Graphics::FILTER_CRT, {
  :scanline_count => 240,
  :scanline_intensity => 0.65,
  :grille_intensity => 0.75,
  :curvature => 0.18,
  :vignette_strength => 0.45,
  :brightness => 1.55
})
```

### 只要扫描线

适合想保留原画面色彩，只增加横向扫描线的情况。

```ruby
Graphics.render_filter = Graphics::FILTER_CRT
Graphics.set_render_filter_params(Graphics::FILTER_CRT, {
  :scanline_count => 240,
  :scanline_intensity => 0.45,
  :grille_intensity => 0.0,
  :curvature => 0.0,
  :vignette_strength => 0.0,
  :brightness => 1.2
})
```

## 使用建议

滤镜参数是全局参数，切换地图、打开菜单或播放事件时会继续保留当前设置。需要临时效果时，请在事件结束后恢复默认值或切回 `FILTER_NONE`。

```ruby
Graphics.reset_render_filter_params(Graphics::FILTER_CRT)
Graphics.render_filter = Graphics::FILTER_NONE
```

如果要做渐变变化，可以在事件更新里逐帧调整单个参数：

```ruby
60.times do |i|
  strength = i / 60.0
  Graphics.set_render_filter_param(Graphics::FILTER_CRT, :vignette_strength, strength)
  Graphics.update
end
```

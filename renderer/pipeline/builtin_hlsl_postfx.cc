// Copyright 2018-2025 Admenri.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "renderer/pipeline/builtin_hlsl_postfx.h"

namespace renderer {

///
// type:
//   crt filter post-process shader
///
// entry:
//   vertex: VSMain
//   pixel: PSMain
///
// vertex:
//   <float4, float2, float4>
///
// resource:
//   { float4x4, float4x4 }      -- WorldMatrixBuffer
//   { Texture2D }               -- u_Texture (source frame)
//   { float4, float4 }          -- CRTUniformConstants
///

// Vertex shader: copied verbatim from kHLSL_BaseRender_Vertex.
// New filters must NOT modify the base shader; they copy it and extend the
// pixel stage in their own file.
const std::string kHLSL_CRTFilter_Vertex = R"(
struct WorldMatrix {
  float4x4 ProjMat;
  float4x4 TransMat;
};

cbuffer WorldMatrixBuffer {
  WorldMatrix u_Transform;
};

struct VSInput {
  float4 Pos : ATTRIB0;
  float2 UV : ATTRIB1;
  float4 Color : ATTRIB2;
};

struct PSInput {
  float4 Pos : SV_Position;
  float2 UV : TEXCOORD0;
  float4 Color : COLOR0;
};

void VSMain(in VSInput VSIn, out PSInput PSIn) {
  PSIn.Pos = mul(u_Transform.TransMat, VSIn.Pos);
  PSIn.Pos = mul(u_Transform.ProjMat, PSIn.Pos);

  PSIn.UV = VSIn.UV;
  PSIn.Color = VSIn.Color;
}
)";

// Pixel shader: based on kHLSL_BaseRender_Pixel with the CRT post-process
// pipeline (barrel curvature -> sample -> aperture grille -> scanlines ->
// vignette) appended.  See .claude/plans/findings.md §4 for derivation.
const std::string kHLSL_CRTFilter_Pixel = R"(
struct PSInput {
  float4 Pos : SV_Position;
  float2 UV : TEXCOORD0;
  float4 Color : COLOR0;
};

Texture2D u_Texture;
SamplerState u_Texture_sampler;

cbuffer CRTUniformConstants {
  // (resolution.x, resolution.y, scanline_count, scanline_intensity)
  float4 u_Params0;
  // (grille_intensity, curvature, vignette_strength, brightness)
  float4 u_Params1;
};

struct PSOutput {
  float4 Color : SV_TARGET;
};

float2 CurveUV(float2 uv, float curvature) {
  uv = uv * 2.0 - 1.0;
  float2 offset = abs(uv.yx) * curvature;
  uv = uv + uv * offset * offset;
  uv = uv * 0.5 + 0.5;
  return uv;
}

void PSMain(in PSInput PSIn, out PSOutput PSOut) {
  float2 resolution        = u_Params0.xy;
  float  scanline_count    = u_Params0.z;
  float  scanline_intensity= u_Params0.w;
  float  grille_intensity  = u_Params1.x;
  float  curvature         = u_Params1.y;
  float  vignette_strength = u_Params1.z;
  float  brightness        = u_Params1.w;

  // Step 1: barrel curvature
  float2 curved_uv = CurveUV(PSIn.UV, curvature);

  // Out-of-bounds pixels become bezel black
  if (curved_uv.x < 0.0 || curved_uv.x > 1.0 ||
      curved_uv.y < 0.0 || curved_uv.y > 1.0) {
    PSOut.Color = float4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  // Step 2: sample source frame at curved coordinate
  float4 src = u_Texture.Sample(u_Texture_sampler, curved_uv);

  // Step 3: aperture grille - column index mod 3 picks an RGB sub-pixel.
  // The * 3.0 compensates for the 2/3 of energy thrown away each column.
  float pixel_x = curved_uv.x * resolution.x;
  int   channel = ((int)floor(pixel_x)) % 3;
  float3 mask;
  if (channel == 0)      mask = float3(1.0, 0.0, 0.0);
  else if (channel == 1) mask = float3(0.0, 1.0, 0.0);
  else                   mask = float3(0.0, 0.0, 1.0);
  float3 grilled = lerp(src.rgb, src.rgb * mask * 3.0, grille_intensity);

  // Step 4: scanlines - sinusoidal vertical modulation
  float scan = sin(curved_uv.y * scanline_count * 3.14159265) * 0.5 + 0.5;
  float3 scanned = grilled * lerp(1.0, scan, scanline_intensity);

  // Step 5: vignette - radial darkening from screen center
  float2 center_off = curved_uv - 0.5;
  float vig = 1.0 - dot(center_off, center_off) * vignette_strength * 4.0;
  vig = saturate(vig);

  float3 final_rgb = scanned * vig * brightness;
  PSOut.Color = float4(final_rgb, src.a * PSIn.Color.a);
}
)";

}  // namespace renderer

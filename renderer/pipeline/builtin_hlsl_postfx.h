// Copyright 2018-2025 Admenri.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RENDERER_PIPELINE_BUILTIN_HLSL_POSTFX_H_
#define RENDERER_PIPELINE_BUILTIN_HLSL_POSTFX_H_

#include <string>

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
extern const std::string kHLSL_CRTFilter_Vertex;
extern const std::string kHLSL_CRTFilter_Pixel;

}  // namespace renderer

#endif  // !RENDERER_PIPELINE_BUILTIN_HLSL_POSTFX_H_

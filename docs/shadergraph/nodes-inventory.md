# Shader Graph 节点清单（自动生成）

> 由 `tools/sg_node_inventory.py` 从 Unity Shader Graph 源生成，请勿手工编辑。
> 源目录：`E:/Learn/Graphics/Packages/com.unity.shadergraph`
> 状态列由实现阶段回填：`TODO` → `IMPL` → `TEST` → `GOLDEN`。
> 状态映射：`docs/shadergraph/node-status.json`（key 为 `category|title`）。

## 汇总

| 批次 | 节点数 | 说明 |
|---|---:|---|
| A | 83 | 核心节点，SG-2 核心 + SG-6A 收尾 |
| B | 105 | 常用扩展，SG-6 前半 |
| C | 20 | 长尾/平台节点，SG-6 后半 |
| 合计 | 208 | 仅统计带 `[Title]` 的节点 |

分类统计：

| 分类 | 节点数 |
|---|---:|
| `Input/Texture` | 17 |
| `Utility/Logic` | 13 |
| `Math/Trigonometry` | 12 |
| `Input/Geometry` | 11 |
| `Math/Vector` | 11 |
| `Input/Basic` | 10 |
| `Math/Advanced` | 10 |
| `UV` | 10 |
| `UI` | 9 |
| `Input/Scene` | 8 |
| `Math/Range` | 8 |
| `Artistic/Adjustment` | 7 |
| `Artistic/Normal` | 6 |
| `Input/UI` | 6 |
| `Math/Basic` | 6 |
| `Math/Round` | 6 |
| `Channel` | 5 |
| `Procedural/Shape` | 5 |
| `Utility` | 5 |
| `Input/Lighting` | 4 |
| `Input/Matrix` | 4 |
| `Math/Matrix` | 4 |
| `Math/Wave` | 4 |
| `Input/Gradient` | 3 |
| `Math/Derivative` | 3 |
| `Math/Interpolation` | 3 |
| `Procedural/Noise` | 3 |
| `Artistic/Filter` | 2 |
| `Artistic/Mask` | 2 |
| `Input/Mesh Deformation` | 2 |
| `Input/PBR` | 2 |
| `Terrain` | 2 |
| `Artistic/Blend` | 1 |
| `Artistic/Utility` | 1 |
| `Custom Interpolators` | 1 |
| `Input` | 1 |
| `Procedural` | 1 |

状态统计：

| 状态 | 节点数 |
|---|---:|
| TEST | 208 |
| IMPL | 0 |
| GOLDEN | 0 |
| TODO | 0 |

## 批次 A（83）

| 状态 | 分类 | 节点 | Unity 源 | 计划 C++ |
|---|---|---|---|---|
| TEST | `Channel` | Append | `Editor/Data/Nodes/Channel/AppendVectorNode.cs` | `shadergraph/src/nodes/channel/append.cpp` |
| TEST | `Channel` | Combine | `Editor/Data/Nodes/Channel/CombineNode.cs` | `shadergraph/src/nodes/channel/combine.cpp` |
| TEST | `Channel` | Flip | `Editor/Data/Nodes/Channel/FlipNode.cs` | `shadergraph/src/nodes/channel/flip.cpp` |
| TEST | `Channel` | Split | `Editor/Data/Nodes/Channel/SplitNode.cs` | `shadergraph/src/nodes/channel/split.cpp` |
| TEST | `Channel` | Swizzle | `Editor/Data/Nodes/Channel/SwizzleNode.cs` | `shadergraph/src/nodes/channel/swizzle.cpp` |
| TEST | `Input` | Property | `Editor/Data/Nodes/Input/PropertyNode.cs` | `shadergraph/src/nodes/input/property.cpp` |
| TEST | `Input/Basic` | Boolean | `Editor/Data/Nodes/Input/Basic/BooleanNode.cs` | `shadergraph/src/nodes/input_basic/boolean.cpp` |
| TEST | `Input/Basic` | Color | `Editor/Data/Nodes/Input/Basic/ColorNode.cs` | `shadergraph/src/nodes/input_basic/color.cpp` |
| TEST | `Input/Basic` | Constant | `Editor/Data/Nodes/Input/Basic/ConstantNode.cs` | `shadergraph/src/nodes/input_basic/constant.cpp` |
| TEST | `Input/Basic` | Float | `Editor/Data/Nodes/Input/Basic/Vector1Node.cs` | `shadergraph/src/nodes/input_basic/float.cpp` |
| TEST | `Input/Basic` | Integer | `Editor/Data/Nodes/Input/Basic/IntegerNode.cs` | `shadergraph/src/nodes/input_basic/integer.cpp` |
| TEST | `Input/Basic` | Slider | `Editor/Data/Nodes/Input/Basic/SliderNode.cs` | `shadergraph/src/nodes/input_basic/slider.cpp` |
| TEST | `Input/Basic` | Time | `Editor/Data/Nodes/Input/Basic/TimeNode.cs` | `shadergraph/src/nodes/input_basic/time.cpp` |
| TEST | `Input/Basic` | Vector 2 | `Editor/Data/Nodes/Input/Basic/Vector2Node.cs` | `shadergraph/src/nodes/input_basic/vector_2.cpp` |
| TEST | `Input/Basic` | Vector 3 | `Editor/Data/Nodes/Input/Basic/Vector3Node.cs` | `shadergraph/src/nodes/input_basic/vector_3.cpp` |
| TEST | `Input/Basic` | Vector 4 | `Editor/Data/Nodes/Input/Basic/Vector4Node.cs` | `shadergraph/src/nodes/input_basic/vector_4.cpp` |
| TEST | `Input/Geometry` | Bitangent Vector | `Editor/Data/Nodes/Input/Geometry/BitangentVectorNode.cs` | `shadergraph/src/nodes/input_geometry/bitangent_vector.cpp` |
| TEST | `Input/Geometry` | Instance ID | `Editor/Data/Nodes/Input/Geometry/InstanceIDNode.cs` | `shadergraph/src/nodes/input_geometry/instance_id.cpp` |
| TEST | `Input/Geometry` | Normal Vector | `Editor/Data/Nodes/Input/Geometry/NormalVectorNode.cs` | `shadergraph/src/nodes/input_geometry/normal_vector.cpp` |
| TEST | `Input/Geometry` | Position | `Editor/Data/Nodes/Input/Geometry/PositionNode.cs` | `shadergraph/src/nodes/input_geometry/position.cpp` |
| TEST | `Input/Geometry` | Screen Position | `Editor/Data/Nodes/Input/Geometry/ScreenPositionNode.cs` | `shadergraph/src/nodes/input_geometry/screen_position.cpp` |
| TEST | `Input/Geometry` | Tangent Vector | `Editor/Data/Nodes/Input/Geometry/TangentVectorNode.cs` | `shadergraph/src/nodes/input_geometry/tangent_vector.cpp` |
| TEST | `Input/Geometry` | UV | `Editor/Data/Nodes/Input/Geometry/UVNode.cs` | `shadergraph/src/nodes/input_geometry/uv.cpp` |
| TEST | `Input/Geometry` | Vertex Color | `Editor/Data/Nodes/Input/Geometry/VertexColorNode.cs` | `shadergraph/src/nodes/input_geometry/vertex_color.cpp` |
| TEST | `Input/Geometry` | Vertex ID | `Editor/Data/Nodes/Input/Geometry/VertexIDNode.cs` | `shadergraph/src/nodes/input_geometry/vertex_id.cpp` |
| TEST | `Input/Geometry` | View Direction | `Editor/Data/Nodes/Input/Geometry/ViewDirectionNode.cs` | `shadergraph/src/nodes/input_geometry/view_direction.cpp` |
| TEST | `Input/Geometry` | View Vector | `Editor/Data/Nodes/Input/Geometry/ViewVectorNode.cs` | `shadergraph/src/nodes/input_geometry/view_vector.cpp` |
| TEST | `Input/Texture` | Sample Texture 2D | `Editor/Data/Nodes/Input/Texture/SampleTexture2DNode.cs` | `shadergraph/src/nodes/input_texture/sample_texture_2d.cpp` |
| TEST | `Input/Texture` | Sample Texture 2D LOD | `Editor/Data/Nodes/Input/Texture/SampleTexture2DLODNode.cs` | `shadergraph/src/nodes/input_texture/sample_texture_2d_lod.cpp` |
| TEST | `Input/Texture` | Sampler State | `Editor/Data/Nodes/Input/Texture/SamplerStateNode.cs` | `shadergraph/src/nodes/input_texture/sampler_state.cpp` |
| TEST | `Input/Texture` | Split Texture Transform | `Editor/Data/Nodes/Utility/SplitTextureTransformNode.cs` | `shadergraph/src/nodes/input_texture/split_texture_transform.cpp` |
| TEST | `Input/Texture` | Texture Size | `Editor/Data/Nodes/Input/Texture/TexelSizeNode.cs` | `shadergraph/src/nodes/input_texture/texture_size.cpp` |
| TEST | `Math/Advanced` | Absolute | `Editor/Data/Nodes/Math/Advanced/AbsoluteNode.cs` | `shadergraph/src/nodes/math_advanced/absolute.cpp` |
| TEST | `Math/Advanced` | Exponential | `Editor/Data/Nodes/Math/Advanced/ExponentialNode.cs` | `shadergraph/src/nodes/math_advanced/exponential.cpp` |
| TEST | `Math/Advanced` | Length | `Editor/Data/Nodes/Math/Advanced/LengthNode.cs` | `shadergraph/src/nodes/math_advanced/length.cpp` |
| TEST | `Math/Advanced` | Log | `Editor/Data/Nodes/Math/Advanced/LogNode.cs` | `shadergraph/src/nodes/math_advanced/log.cpp` |
| TEST | `Math/Advanced` | Modulo | `Editor/Data/Nodes/Math/Advanced/ModuloNode.cs` | `shadergraph/src/nodes/math_advanced/modulo.cpp` |
| TEST | `Math/Advanced` | Negate | `Editor/Data/Nodes/Math/Advanced/NegateNode.cs` | `shadergraph/src/nodes/math_advanced/negate.cpp` |
| TEST | `Math/Advanced` | Normalize | `Editor/Data/Nodes/Math/Advanced/NormalizeNode.cs` | `shadergraph/src/nodes/math_advanced/normalize.cpp` |
| TEST | `Math/Advanced` | Posterize | `Editor/Data/Nodes/Math/Advanced/PosterizeNode.cs` | `shadergraph/src/nodes/math_advanced/posterize.cpp` |
| TEST | `Math/Advanced` | Reciprocal | `Editor/Data/Nodes/Math/Advanced/ReciprocalNode.cs` | `shadergraph/src/nodes/math_advanced/reciprocal.cpp` |
| TEST | `Math/Advanced` | Reciprocal Square Root | `Editor/Data/Nodes/Math/Advanced/ReciprocalSquareRootNode.cs` | `shadergraph/src/nodes/math_advanced/reciprocal_square_root.cpp` |
| TEST | `Math/Basic` | Add | `Editor/Data/Nodes/Math/Basic/AddNode.cs` | `shadergraph/src/nodes/math_basic/add.cpp` |
| TEST | `Math/Basic` | Divide | `Editor/Data/Nodes/Math/Basic/DivideNode.cs` | `shadergraph/src/nodes/math_basic/divide.cpp` |
| TEST | `Math/Basic` | Multiply | `Editor/Data/Nodes/Math/Basic/MultiplyNode.cs` | `shadergraph/src/nodes/math_basic/multiply.cpp` |
| TEST | `Math/Basic` | Power | `Editor/Data/Nodes/Math/Basic/PowerNode.cs` | `shadergraph/src/nodes/math_basic/power.cpp` |
| TEST | `Math/Basic` | Square Root | `Editor/Data/Nodes/Math/Basic/SquareRootNode.cs` | `shadergraph/src/nodes/math_basic/square_root.cpp` |
| TEST | `Math/Basic` | Subtract | `Editor/Data/Nodes/Math/Basic/SubtractNode.cs` | `shadergraph/src/nodes/math_basic/subtract.cpp` |
| TEST | `Math/Interpolation` | Inverse Lerp | `Editor/Data/Nodes/Math/Interpolation/InverseLerpNode.cs` | `shadergraph/src/nodes/math_interpolation/inverse_lerp.cpp` |
| TEST | `Math/Interpolation` | Lerp | `Editor/Data/Nodes/Math/Interpolation/LerpNode.cs` | `shadergraph/src/nodes/math_interpolation/lerp.cpp` |
| TEST | `Math/Interpolation` | Smoothstep | `Editor/Data/Nodes/Math/Interpolation/SmoothstepNode.cs` | `shadergraph/src/nodes/math_interpolation/smoothstep.cpp` |
| TEST | `Math/Range` | Clamp | `Editor/Data/Nodes/Math/Range/ClampNode.cs` | `shadergraph/src/nodes/math_range/clamp.cpp` |
| TEST | `Math/Range` | Fraction | `Editor/Data/Nodes/Math/Range/FractionNode.cs` | `shadergraph/src/nodes/math_range/fraction.cpp` |
| TEST | `Math/Range` | Maximum | `Editor/Data/Nodes/Math/Range/MaximumNode.cs` | `shadergraph/src/nodes/math_range/maximum.cpp` |
| TEST | `Math/Range` | Minimum | `Editor/Data/Nodes/Math/Range/MinimumNode.cs` | `shadergraph/src/nodes/math_range/minimum.cpp` |
| TEST | `Math/Range` | One Minus | `Editor/Data/Nodes/Math/Range/OneMinusNode.cs` | `shadergraph/src/nodes/math_range/one_minus.cpp` |
| TEST | `Math/Range` | Random Range | `Editor/Data/Nodes/Math/Range/RandomRangeNode.cs` | `shadergraph/src/nodes/math_range/random_range.cpp` |
| TEST | `Math/Range` | Remap | `Editor/Data/Nodes/Math/Range/RemapNode.cs` | `shadergraph/src/nodes/math_range/remap.cpp` |
| TEST | `Math/Range` | Saturate | `Editor/Data/Nodes/Math/Range/SaturateNode.cs` | `shadergraph/src/nodes/math_range/saturate.cpp` |
| TEST | `Math/Round` | Ceiling | `Editor/Data/Nodes/Math/Round/CeilingNode.cs` | `shadergraph/src/nodes/math_round/ceiling.cpp` |
| TEST | `Math/Round` | Floor | `Editor/Data/Nodes/Math/Round/FloorNode.cs` | `shadergraph/src/nodes/math_round/floor.cpp` |
| TEST | `Math/Round` | Round | `Editor/Data/Nodes/Math/Round/RoundNode.cs` | `shadergraph/src/nodes/math_round/round.cpp` |
| TEST | `Math/Round` | Sign | `Editor/Data/Nodes/Math/Round/SignNode.cs` | `shadergraph/src/nodes/math_round/sign.cpp` |
| TEST | `Math/Round` | Step | `Editor/Data/Nodes/Math/Round/StepNode.cs` | `shadergraph/src/nodes/math_round/step.cpp` |
| TEST | `Math/Round` | Truncate | `Editor/Data/Nodes/Math/Round/TruncateNode.cs` | `shadergraph/src/nodes/math_round/truncate.cpp` |
| TEST | `Utility` | Custom Function | `Editor/Data/Nodes/Utility/CustomFunctionNode.cs` | `shadergraph/src/nodes/utility/custom_function.cpp` |
| TEST | `Utility` | Dropdown | `Editor/Data/Nodes/Utility/DropdownNode.cs` | `shadergraph/src/nodes/utility/dropdown.cpp` |
| TEST | `Utility` | Keyword | `Editor/Data/Nodes/Utility/KeywordNode.cs` | `shadergraph/src/nodes/utility/keyword.cpp` |
| TEST | `Utility` | Preview | `Editor/Data/Nodes/Utility/PreviewNode.cs` | `shadergraph/src/nodes/utility/preview.cpp` |
| TEST | `Utility` | Sub-graph | `Editor/Data/Nodes/Utility/SubGraphNode.cs` | `shadergraph/src/nodes/utility/sub_graph.cpp` |
| TEST | `Utility/Logic` | All | `Editor/Data/Nodes/Utility/Logic/AllNode.cs` | `shadergraph/src/nodes/utility_logic/all.cpp` |
| TEST | `Utility/Logic` | And | `Editor/Data/Nodes/Utility/Logic/AndNode.cs` | `shadergraph/src/nodes/utility_logic/and.cpp` |
| TEST | `Utility/Logic` | Any | `Editor/Data/Nodes/Utility/Logic/AnyNode.cs` | `shadergraph/src/nodes/utility_logic/any.cpp` |
| TEST | `Utility/Logic` | Branch | `Editor/Data/Nodes/Utility/Logic/BranchNode.cs` | `shadergraph/src/nodes/utility_logic/branch.cpp` |
| TEST | `Utility/Logic` | Branch On Input Connection | `Editor/Data/Nodes/Utility/Logic/BranchOnInputConnection.cs` | `shadergraph/src/nodes/utility_logic/branch_on_input_connection.cpp` |
| TEST | `Utility/Logic` | Comparison | `Editor/Data/Nodes/Utility/Logic/ComparisonNode.cs` | `shadergraph/src/nodes/utility_logic/comparison.cpp` |
| TEST | `Utility/Logic` | Is Front Face | `Editor/Data/Nodes/Utility/Logic/IsFrontFaceNode.cs` | `shadergraph/src/nodes/utility_logic/is_front_face.cpp` |
| TEST | `Utility/Logic` | Is Infinite | `Editor/Data/Nodes/Utility/Logic/IsInfiniteNode.cs` | `shadergraph/src/nodes/utility_logic/is_infinite.cpp` |
| TEST | `Utility/Logic` | Is NaN | `Editor/Data/Nodes/Utility/Logic/IsNanNode.cs` | `shadergraph/src/nodes/utility_logic/is_nan.cpp` |
| TEST | `Utility/Logic` | Nand | `Editor/Data/Nodes/Utility/Logic/NandNode.cs` | `shadergraph/src/nodes/utility_logic/nand.cpp` |
| TEST | `Utility/Logic` | Not | `Editor/Data/Nodes/Utility/Logic/NotNode.cs` | `shadergraph/src/nodes/utility_logic/not.cpp` |
| TEST | `Utility/Logic` | Or | `Editor/Data/Nodes/Utility/Logic/OrNode.cs` | `shadergraph/src/nodes/utility_logic/or.cpp` |
| TEST | `Utility/Logic` | Switch | `Editor/Data/Nodes/Utility/Logic/SwitchNode.cs` | `shadergraph/src/nodes/utility_logic/switch.cpp` |

## 批次 B（105）

| 状态 | 分类 | 节点 | Unity 源 | 计划 C++ |
|---|---|---|---|---|
| TEST | `Artistic/Adjustment` | Channel Mixer | `Editor/Data/Nodes/Artistic/Adjustment/ChannelMixerNode.cs` | `shadergraph/src/nodes/artistic_adjustment/channel_mixer.cpp` |
| TEST | `Artistic/Adjustment` | Contrast | `Editor/Data/Nodes/Artistic/Adjustment/ContrastNode.cs` | `shadergraph/src/nodes/artistic_adjustment/contrast.cpp` |
| TEST | `Artistic/Adjustment` | Hue | `Editor/Data/Nodes/Artistic/Adjustment/HueNode.cs` | `shadergraph/src/nodes/artistic_adjustment/hue.cpp` |
| TEST | `Artistic/Adjustment` | Invert Colors | `Editor/Data/Nodes/Artistic/Adjustment/InvertColorsNode.cs` | `shadergraph/src/nodes/artistic_adjustment/invert_colors.cpp` |
| TEST | `Artistic/Adjustment` | Replace Color | `Editor/Data/Nodes/Artistic/Adjustment/ReplaceColorNode.cs` | `shadergraph/src/nodes/artistic_adjustment/replace_color.cpp` |
| TEST | `Artistic/Adjustment` | Saturation | `Editor/Data/Nodes/Artistic/Adjustment/SaturationNode.cs` | `shadergraph/src/nodes/artistic_adjustment/saturation.cpp` |
| TEST | `Artistic/Adjustment` | White Balance | `Editor/Data/Nodes/Artistic/Adjustment/WhiteBalanceNode.cs` | `shadergraph/src/nodes/artistic_adjustment/white_balance.cpp` |
| TEST | `Artistic/Blend` | Blend | `Editor/Data/Nodes/Artistic/Blend/BlendNode.cs` | `shadergraph/src/nodes/artistic_blend/blend.cpp` |
| TEST | `Artistic/Filter` | Dither | `Editor/Data/Nodes/Artistic/Filter/DitherNode.cs` | `shadergraph/src/nodes/artistic_filter/dither.cpp` |
| TEST | `Artistic/Filter` | Fade Transition | `Editor/Data/Nodes/Artistic/Filter/FadeTransitionNode.cs` | `shadergraph/src/nodes/artistic_filter/fade_transition.cpp` |
| TEST | `Artistic/Mask` | Channel Mask | `Editor/Data/Nodes/Artistic/Mask/ChannelMaskNode.cs` | `shadergraph/src/nodes/artistic_mask/channel_mask.cpp` |
| TEST | `Artistic/Mask` | Color Mask | `Editor/Data/Nodes/Artistic/Mask/ColorMaskNode.cs` | `shadergraph/src/nodes/artistic_mask/color_mask.cpp` |
| TEST | `Artistic/Normal` | Normal Blend | `Editor/Data/Nodes/Artistic/Normal/NormalBlendNode.cs` | `shadergraph/src/nodes/artistic_normal/normal_blend.cpp` |
| TEST | `Artistic/Normal` | Normal From Height | `Editor/Data/Nodes/Artistic/Normal/NormalFromHeightNode.cs` | `shadergraph/src/nodes/artistic_normal/normal_from_height.cpp` |
| TEST | `Artistic/Normal` | Normal From Texture | `Editor/Data/Nodes/Artistic/Normal/NormalFromTextureNode.cs` | `shadergraph/src/nodes/artistic_normal/normal_from_texture.cpp` |
| TEST | `Artistic/Normal` | Normal Reconstruct Z | `Editor/Data/Nodes/Artistic/Normal/NormalReconstructZNode.cs` | `shadergraph/src/nodes/artistic_normal/normal_reconstruct_z.cpp` |
| TEST | `Artistic/Normal` | Normal Strength | `Editor/Data/Nodes/Artistic/Normal/NormalStrengthNode.cs` | `shadergraph/src/nodes/artistic_normal/normal_strength.cpp` |
| TEST | `Artistic/Normal` | Normal Unpack | `Editor/Data/Nodes/Artistic/Normal/NormalUnpackNode.cs` | `shadergraph/src/nodes/artistic_normal/normal_unpack.cpp` |
| TEST | `Artistic/Utility` | Colorspace Conversion | `Editor/Data/Nodes/Artistic/Utility/ColorspaceConversion.cs` | `shadergraph/src/nodes/artistic_utility/colorspace_conversion.cpp` |
| TEST | `Input/Gradient` | Blackbody | `Editor/Data/Nodes/Input/Gradient/BlackbodyNode.cs` | `shadergraph/src/nodes/input_gradient/blackbody.cpp` |
| TEST | `Input/Gradient` | Gradient | `Editor/Data/Nodes/Input/Gradient/GradientNode.cs` | `shadergraph/src/nodes/input_gradient/gradient.cpp` |
| TEST | `Input/Gradient` | Sample Gradient | `Editor/Data/Nodes/Input/Gradient/SampleGradientNode.cs` | `shadergraph/src/nodes/input_gradient/sample_gradient.cpp` |
| TEST | `Input/Lighting` | Ambient | `Editor/Data/Nodes/Input/Lighting/AmbientNode.cs` | `shadergraph/src/nodes/input_lighting/ambient.cpp` |
| TEST | `Input/Lighting` | Baked GI | `Editor/Data/Nodes/Input/Lighting/BakedGINode.cs` | `shadergraph/src/nodes/input_lighting/baked_gi.cpp` |
| TEST | `Input/Lighting` | Main Light Direction | `Editor/Data/Nodes/Input/Lighting/MainLightDirectionNode.cs` | `shadergraph/src/nodes/input_lighting/main_light_direction.cpp` |
| TEST | `Input/Lighting` | Reflection Probe | `Editor/Data/Nodes/Input/Lighting/ReflectionProbeNode.cs` | `shadergraph/src/nodes/input_lighting/reflection_probe.cpp` |
| TEST | `Input/Matrix` | Matrix 2x2 | `Editor/Data/Nodes/Input/Matrix/Matrix2Node.cs` | `shadergraph/src/nodes/input_matrix/matrix_2x2.cpp` |
| TEST | `Input/Matrix` | Matrix 3x3 | `Editor/Data/Nodes/Input/Matrix/Matrix3Node.cs` | `shadergraph/src/nodes/input_matrix/matrix_3x3.cpp` |
| TEST | `Input/Matrix` | Matrix 4x4 | `Editor/Data/Nodes/Input/Matrix/Matrix4Node.cs` | `shadergraph/src/nodes/input_matrix/matrix_4x4.cpp` |
| TEST | `Input/Matrix` | Transformation Matrix | `Editor/Data/Nodes/Input/Matrix/TransformationMatrixNode.cs` | `shadergraph/src/nodes/input_matrix/transformation_matrix.cpp` |
| TEST | `Input/PBR` | Dielectric Specular | `Editor/Data/Nodes/Input/PBR/DielectricSpecularNode.cs` | `shadergraph/src/nodes/input_pbr/dielectric_specular.cpp` |
| TEST | `Input/PBR` | Metal Reflectance | `Editor/Data/Nodes/Input/PBR/MetalReflectanceNode.cs` | `shadergraph/src/nodes/input_pbr/metal_reflectance.cpp` |
| TEST | `Input/Scene` | Camera | `Editor/Data/Nodes/Input/Scene/CameraNode.cs` | `shadergraph/src/nodes/input_scene/camera.cpp` |
| TEST | `Input/Scene` | Eye Index | `Editor/Data/Nodes/Input/Scene/EyeIndexNode.cs` | `shadergraph/src/nodes/input_scene/eye_index.cpp` |
| TEST | `Input/Scene` | Fog | `Editor/Data/Nodes/Input/Scene/FogNode.cs` | `shadergraph/src/nodes/input_scene/fog.cpp` |
| TEST | `Input/Scene` | Object | `Editor/Data/Nodes/Input/Scene/ObjectNode.cs` | `shadergraph/src/nodes/input_scene/object.cpp` |
| TEST | `Input/Scene` | Scene Color | `Editor/Data/Nodes/Input/Scene/SceneColorNode.cs` | `shadergraph/src/nodes/input_scene/scene_color.cpp` |
| TEST | `Input/Scene` | Scene Depth | `Editor/Data/Nodes/Input/Scene/SceneDepthNode.cs` | `shadergraph/src/nodes/input_scene/scene_depth.cpp` |
| TEST | `Input/Scene` | Scene Depth Difference | `Editor/Data/Nodes/Input/Scene/SceneDepthDifferenceNode.cs` | `shadergraph/src/nodes/input_scene/scene_depth_difference.cpp` |
| TEST | `Input/Scene` | Screen | `Editor/Data/Nodes/Input/Scene/ScreenNode.cs` | `shadergraph/src/nodes/input_scene/screen.cpp` |
| TEST | `Input/Texture` | Calculate Level Of Detail Texture 2D | `Editor/Data/Nodes/Input/Texture/CalculateLevelOfDetailTexture2DNode.cs` | `shadergraph/src/nodes/input_texture/calculate_level_of_detail_texture_2d.cpp` |
| TEST | `Input/Texture` | Cubemap Asset | `Editor/Data/Nodes/Input/Texture/CubemapAssetNode.cs` | `shadergraph/src/nodes/input_texture/cubemap_asset.cpp` |
| TEST | `Input/Texture` | Gather Texture 2D | `Editor/Data/Nodes/Input/Texture/GatherTexture2DNode.cs` | `shadergraph/src/nodes/input_texture/gather_texture_2d.cpp` |
| TEST | `Input/Texture` | Procedural Virtual Texture | `Editor/Data/Nodes/Input/Texture/ProceduralVirtualTextureNode.cs` | `shadergraph/src/nodes/input_texture/procedural_virtual_texture.cpp` |
| TEST | `Input/Texture` | Sample Cubemap | `Editor/Data/Nodes/Input/Texture/SampleRawCubemapNode.cs` | `shadergraph/src/nodes/input_texture/sample_cubemap.cpp` |
| TEST | `Input/Texture` | Sample Reflected Cubemap | `Editor/Data/Nodes/Input/Texture/SampleCubemapNode.cs` | `shadergraph/src/nodes/input_texture/sample_reflected_cubemap.cpp` |
| TEST | `Input/Texture` | Sample Texture 2D Array | `Editor/Data/Nodes/Input/Texture/SampleTexture2DArrayNode.cs` | `shadergraph/src/nodes/input_texture/sample_texture_2d_array.cpp` |
| TEST | `Input/Texture` | Sample Texture 3D | `Editor/Data/Nodes/Input/Texture/SampleTexture3DNode.cs` | `shadergraph/src/nodes/input_texture/sample_texture_3d.cpp` |
| TEST | `Input/Texture` | Sample Virtual Texture | `Editor/Data/Nodes/Input/Texture/SampleVirtualTextureNode.cs` | `shadergraph/src/nodes/input_texture/sample_virtual_texture.cpp` |
| TEST | `Input/Texture` | Texture 2D Array Asset | `Editor/Data/Nodes/Input/Texture/Texture2DArrayAssetNode.cs` | `shadergraph/src/nodes/input_texture/texture_2d_array_asset.cpp` |
| TEST | `Input/Texture` | Texture 2D Asset | `Editor/Data/Nodes/Input/Texture/Texture2DAssetNode.cs` | `shadergraph/src/nodes/input_texture/texture_2d_asset.cpp` |
| TEST | `Input/Texture` | Texture 3D Asset | `Editor/Data/Nodes/Input/Texture/Texture3DAssetNode.cs` | `shadergraph/src/nodes/input_texture/texture_3d_asset.cpp` |
| TEST | `Math/Derivative` | DDX | `Editor/Data/Nodes/Math/Derivative/DDXNode.cs` | `shadergraph/src/nodes/math_derivative/ddx.cpp` |
| TEST | `Math/Derivative` | DDXY | `Editor/Data/Nodes/Math/Derivative/DDXYNode.cs` | `shadergraph/src/nodes/math_derivative/ddxy.cpp` |
| TEST | `Math/Derivative` | DDY | `Editor/Data/Nodes/Math/Derivative/DDYNode.cs` | `shadergraph/src/nodes/math_derivative/ddy.cpp` |
| TEST | `Math/Matrix` | Matrix Construction | `Editor/Data/Nodes/Math/Matrix/MatrixConstructionNode.cs` | `shadergraph/src/nodes/math_matrix/matrix_construction.cpp` |
| TEST | `Math/Matrix` | Matrix Determinant | `Editor/Data/Nodes/Math/Matrix/MatrixDeterminantNode.cs` | `shadergraph/src/nodes/math_matrix/matrix_determinant.cpp` |
| TEST | `Math/Matrix` | Matrix Split | `Editor/Data/Nodes/Math/Matrix/MatrixSplitNode.cs` | `shadergraph/src/nodes/math_matrix/matrix_split.cpp` |
| TEST | `Math/Matrix` | Matrix Transpose | `Editor/Data/Nodes/Math/Matrix/MatrixTransposeNode.cs` | `shadergraph/src/nodes/math_matrix/matrix_transpose.cpp` |
| TEST | `Math/Trigonometry` | Arccosine | `Editor/Data/Nodes/Math/Trigonometry/ArccosineNode.cs` | `shadergraph/src/nodes/math_trigonometry/arccosine.cpp` |
| TEST | `Math/Trigonometry` | Arcsine | `Editor/Data/Nodes/Math/Trigonometry/ArcsineNode.cs` | `shadergraph/src/nodes/math_trigonometry/arcsine.cpp` |
| TEST | `Math/Trigonometry` | Arctangent | `Editor/Data/Nodes/Math/Trigonometry/ArctangentNode.cs` | `shadergraph/src/nodes/math_trigonometry/arctangent.cpp` |
| TEST | `Math/Trigonometry` | Arctangent2 | `Editor/Data/Nodes/Math/Trigonometry/Arctangent2Node.cs` | `shadergraph/src/nodes/math_trigonometry/arctangent2.cpp` |
| TEST | `Math/Trigonometry` | Cosine | `Editor/Data/Nodes/Math/Trigonometry/CosineNode.cs` | `shadergraph/src/nodes/math_trigonometry/cosine.cpp` |
| TEST | `Math/Trigonometry` | Degrees To Radians | `Editor/Data/Nodes/Math/Trigonometry/DegreesToRadiansNode.cs` | `shadergraph/src/nodes/math_trigonometry/degrees_to_radians.cpp` |
| TEST | `Math/Trigonometry` | Hyperbolic Cosine | `Editor/Data/Nodes/Math/Trigonometry/HyperbolicCosineNode.cs` | `shadergraph/src/nodes/math_trigonometry/hyperbolic_cosine.cpp` |
| TEST | `Math/Trigonometry` | Hyperbolic Sine | `Editor/Data/Nodes/Math/Trigonometry/HyperbolicSineNode.cs` | `shadergraph/src/nodes/math_trigonometry/hyperbolic_sine.cpp` |
| TEST | `Math/Trigonometry` | Hyperbolic Tangent | `Editor/Data/Nodes/Math/Trigonometry/HyperbolicTangentNode.cs` | `shadergraph/src/nodes/math_trigonometry/hyperbolic_tangent.cpp` |
| TEST | `Math/Trigonometry` | Radians To Degrees | `Editor/Data/Nodes/Math/Trigonometry/RadiansToDegreesNode.cs` | `shadergraph/src/nodes/math_trigonometry/radians_to_degrees.cpp` |
| TEST | `Math/Trigonometry` | Sine | `Editor/Data/Nodes/Math/Trigonometry/SineNode.cs` | `shadergraph/src/nodes/math_trigonometry/sine.cpp` |
| TEST | `Math/Trigonometry` | Tangent | `Editor/Data/Nodes/Math/Trigonometry/TangentNode.cs` | `shadergraph/src/nodes/math_trigonometry/tangent.cpp` |
| TEST | `Math/Vector` | Cross Product | `Editor/Data/Nodes/Math/Vector/CrossProductNode.cs` | `shadergraph/src/nodes/math_vector/cross_product.cpp` |
| TEST | `Math/Vector` | Distance | `Editor/Data/Nodes/Math/Vector/DistanceNode.cs` | `shadergraph/src/nodes/math_vector/distance.cpp` |
| TEST | `Math/Vector` | Dot Product | `Editor/Data/Nodes/Math/Vector/DotProductNode.cs` | `shadergraph/src/nodes/math_vector/dot_product.cpp` |
| TEST | `Math/Vector` | Fresnel Effect | `Editor/Data/Nodes/Math/Vector/FresnelEffectNode.cs` | `shadergraph/src/nodes/math_vector/fresnel_effect.cpp` |
| TEST | `Math/Vector` | Projection | `Editor/Data/Nodes/Math/Vector/ProjectionNode.cs` | `shadergraph/src/nodes/math_vector/projection.cpp` |
| TEST | `Math/Vector` | Reflection | `Editor/Data/Nodes/Math/Vector/ReflectionNode.cs` | `shadergraph/src/nodes/math_vector/reflection.cpp` |
| TEST | `Math/Vector` | Refract | `Editor/Data/Nodes/Math/Vector/RefractNode.cs` | `shadergraph/src/nodes/math_vector/refract.cpp` |
| TEST | `Math/Vector` | Rejection | `Editor/Data/Nodes/Math/Vector/RejectionNode.cs` | `shadergraph/src/nodes/math_vector/rejection.cpp` |
| TEST | `Math/Vector` | Rotate About Axis | `Editor/Data/Nodes/Math/Vector/RotateAboutAxisNode.cs` | `shadergraph/src/nodes/math_vector/rotate_about_axis.cpp` |
| TEST | `Math/Vector` | Sphere Mask | `Editor/Data/Nodes/Math/Vector/SphereMaskNode.cs` | `shadergraph/src/nodes/math_vector/sphere_mask.cpp` |
| TEST | `Math/Vector` | Transform | `Editor/Data/Nodes/Math/Vector/TransformNode.cs` | `shadergraph/src/nodes/math_vector/transform.cpp` |
| TEST | `Math/Wave` | Noise Sine Wave | `Editor/Data/Nodes/Math/Wave/NoiseSineWaveNode.cs` | `shadergraph/src/nodes/math_wave/noise_sine_wave.cpp` |
| TEST | `Math/Wave` | Sawtooth Wave | `Editor/Data/Nodes/Math/Wave/SawtoothWaveNode.cs` | `shadergraph/src/nodes/math_wave/sawtooth_wave.cpp` |
| TEST | `Math/Wave` | Square Wave | `Editor/Data/Nodes/Math/Wave/SquareWaveNode.cs` | `shadergraph/src/nodes/math_wave/square_wave.cpp` |
| TEST | `Math/Wave` | Triangle Wave | `Editor/Data/Nodes/Math/Wave/TriangleWaveNode.cs` | `shadergraph/src/nodes/math_wave/triangle_wave.cpp` |
| TEST | `Procedural` | Checkerboard | `Editor/Data/Nodes/Procedural/CheckerboardNode.cs` | `shadergraph/src/nodes/procedural/checkerboard.cpp` |
| TEST | `Procedural/Noise` | Gradient Noise | `Editor/Data/Nodes/Procedural/Noise/GradientNoiseNode.cs` | `shadergraph/src/nodes/procedural_noise/gradient_noise.cpp` |
| TEST | `Procedural/Noise` | Simple Noise | `Editor/Data/Nodes/Procedural/Noise/SimpleNoiseNode.cs` | `shadergraph/src/nodes/procedural_noise/simple_noise.cpp` |
| TEST | `Procedural/Noise` | Voronoi | `Editor/Data/Nodes/Procedural/Noise/VoronoiNode.cs` | `shadergraph/src/nodes/procedural_noise/voronoi.cpp` |
| TEST | `Procedural/Shape` | Ellipse | `Editor/Data/Nodes/Procedural/Shape/EllipseNode.cs` | `shadergraph/src/nodes/procedural_shape/ellipse.cpp` |
| TEST | `Procedural/Shape` | Polygon | `Editor/Data/Nodes/Procedural/Shape/PolygonNode.cs` | `shadergraph/src/nodes/procedural_shape/polygon.cpp` |
| TEST | `Procedural/Shape` | Rectangle | `Editor/Data/Nodes/Procedural/Shape/RectangleNode.cs` | `shadergraph/src/nodes/procedural_shape/rectangle.cpp` |
| TEST | `Procedural/Shape` | Rounded Polygon | `Editor/Data/Nodes/Procedural/Shape/RoundedPolygonNode.cs` | `shadergraph/src/nodes/procedural_shape/rounded_polygon.cpp` |
| TEST | `Procedural/Shape` | Rounded Rectangle | `Editor/Data/Nodes/Procedural/Shape/RoundedRectangleNode.cs` | `shadergraph/src/nodes/procedural_shape/rounded_rectangle.cpp` |
| TEST | `UV` | Flipbook | `Editor/Data/Nodes/UV/FlipbookNode.cs` | `shadergraph/src/nodes/uv/flipbook.cpp` |
| TEST | `UV` | Parallax Mapping | `Editor/Data/Nodes/UV/ParallaxMappingNode.cs` | `shadergraph/src/nodes/uv/parallax_mapping.cpp` |
| TEST | `UV` | Parallax Occlusion Mapping | `Editor/Data/Nodes/UV/ParallaxOcclusionMappingNode.cs` | `shadergraph/src/nodes/uv/parallax_occlusion_mapping.cpp` |
| TEST | `UV` | Polar Coordinates | `Editor/Data/Nodes/UV/PolarCoordinatesNode.cs` | `shadergraph/src/nodes/uv/polar_coordinates.cpp` |
| TEST | `UV` | Radial Shear | `Editor/Data/Nodes/UV/RadialShearNode.cs` | `shadergraph/src/nodes/uv/radial_shear.cpp` |
| TEST | `UV` | Rotate | `Editor/Data/Nodes/UV/RotateNode.cs` | `shadergraph/src/nodes/uv/rotate.cpp` |
| TEST | `UV` | Spherize | `Editor/Data/Nodes/UV/SpherizeNode.cs` | `shadergraph/src/nodes/uv/spherize.cpp` |
| TEST | `UV` | Tiling And Offset | `Editor/Data/Nodes/UV/TilingAndOffsetNode.cs` | `shadergraph/src/nodes/uv/tiling_and_offset.cpp` |
| TEST | `UV` | Triplanar | `Editor/Data/Nodes/UV/TriplanarNode.cs` | `shadergraph/src/nodes/uv/triplanar.cpp` |
| TEST | `UV` | Twirl | `Editor/Data/Nodes/UV/TwirlNode.cs` | `shadergraph/src/nodes/uv/twirl.cpp` |

## 批次 C（20）

| 状态 | 分类 | 节点 | Unity 源 | 计划 C++ |
|---|---|---|---|---|
| TEST | `Custom Interpolators` | Instance | `Editor/Data/Nodes/Input/CustomInterpolatorNode.cs` | `shadergraph/src/nodes/custom_interpolators/instance.cpp` |
| TEST | `Input/Mesh Deformation` | Compute Deformation | `Editor/Data/Nodes/MeshDeformation/ComputeDeformNode.cs` | `shadergraph/src/nodes/input_mesh_deformation/compute_deformation.cpp` |
| TEST | `Input/Mesh Deformation` | Linear Blend Skinning | `Editor/Data/Nodes/MeshDeformation/LinearBlendSkinningNode.cs` | `shadergraph/src/nodes/input_mesh_deformation/linear_blend_skinning.cpp` |
| TEST | `Input/UI` | Element Layout UV | `Editor/Data/Nodes/Input/UI/ElementLayoutUVNode.cs` | `shadergraph/src/nodes/input_ui/element_layout_uv.cpp` |
| TEST | `Input/UI` | Element Opacity | `Editor/Data/Nodes/Input/UI/ElementOpacityNode.cs` | `shadergraph/src/nodes/input_ui/element_opacity.cpp` |
| TEST | `Input/UI` | Element Texture Size | `Editor/Data/Nodes/Input/UI/ElementTextureSizeNode.cs` | `shadergraph/src/nodes/input_ui/element_texture_size.cpp` |
| TEST | `Input/UI` | Element Texture UV | `Editor/Data/Nodes/Input/UI/ElementTextureUVNode.cs` | `shadergraph/src/nodes/input_ui/element_texture_uv.cpp` |
| TEST | `Input/UI` | Element Tint | `Editor/Data/Nodes/Input/UI/ElementTintNode.cs` | `shadergraph/src/nodes/input_ui/element_tint.cpp` |
| TEST | `Input/UI` | SDF Text Parameters | `Editor/Data/Nodes/Input/UI/SDFTextParametersNode.cs` | `shadergraph/src/nodes/input_ui/sdf_text_parameters.cpp` |
| TEST | `Terrain` | Terrain Properties | `Editor/Data/Nodes/Terrain/TerrainPropertiesNode.cs` | `shadergraph/src/nodes/terrain/terrain_properties.cpp` |
| TEST | `Terrain` | Terrain Texture | `Editor/Data/Nodes/Terrain/TerrainTexture.cs` | `shadergraph/src/nodes/terrain/terrain_texture.cpp` |
| TEST | `UI` | Default Bitmap Text | `Editor/Data/Nodes/UI/DefaultBitmapTextNode.cs` | `shadergraph/src/nodes/ui/default_bitmap_text.cpp` |
| TEST | `UI` | Default Gradient | `Editor/Data/Nodes/UI/DefaultGradientNode.cs` | `shadergraph/src/nodes/ui/default_gradient.cpp` |
| TEST | `UI` | Default SDF Text | `Editor/Data/Nodes/UI/DefaultSDFTextNode.cs` | `shadergraph/src/nodes/ui/default_sdf_text.cpp` |
| TEST | `UI` | Default Solid | `Editor/Data/Nodes/UI/DefaultSolidNode.cs` | `shadergraph/src/nodes/ui/default_solid.cpp` |
| TEST | `UI` | Default Texture | `Editor/Data/Nodes/UI/DefaultTextureNode.cs` | `shadergraph/src/nodes/ui/default_texture.cpp` |
| TEST | `UI` | Is Forced Gamma | `Editor/Data/Nodes/UI/IsForcedGammaNode.cs` | `shadergraph/src/nodes/ui/is_forced_gamma.cpp` |
| TEST | `UI` | Render Type | `Editor/Data/Nodes/UI/RenderTypeNode.cs` | `shadergraph/src/nodes/ui/render_type.cpp` |
| TEST | `UI` | Render Type Branch | `Editor/Data/Nodes/UI/RenderTypeBranchNode.cs` | `shadergraph/src/nodes/ui/render_type_branch.cpp` |
| TEST | `UI` | Sample Element Texture | `Editor/Data/Nodes/UI/SampleElementTextureNode.cs` | `shadergraph/src/nodes/ui/sample_element_texture.cpp` |

## 无 `[Title]` 的节点类/基础设施

以下文件包含 `class *Node` 但未声明 `[Title]`，需要实现或明确排除（BlockNode、RedirectNode、SubGraphOutputNode、LegacyUnknownTypeNode 等）：

- `Editor/Data/Nodes/AbstractMaterialNode.cs`
- `Editor/Data/Nodes/BlockNode.cs`
- `Editor/Data/Nodes/CodeFunctionNode.cs`
- `Editor/Data/Nodes/GeometryNode.cs`
- `Editor/Data/Nodes/LegacyUnknownTypeNode.cs`
- `Editor/Data/Nodes/RedirectNode.cs`

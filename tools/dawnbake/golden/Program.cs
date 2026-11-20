using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using GPUBaking;

namespace DawnGolden
{
    internal sealed class FileContext : FImportExportContext
    {
        private readonly FileStream m_stream;

        public FileContext(string path, bool saving) : base(saving)
        {
            m_stream = new FileStream(path,
                saving ? FileMode.Create : FileMode.Open,
                saving ? FileAccess.Write : FileAccess.Read);
        }

        protected override int Read(byte[] data, int offset, int size) => m_stream.Read(data, offset, size);

        protected override int Write(byte[] data, int offset, int size)
        {
            m_stream.Write(data, offset, size);
            return size;
        }

        public void Close() => m_stream.Close();
    }

    internal static class Program
    {
        private static int Main(string[] args)
        {
            try
            {
                if (args.Length == 0 || args[0] == "sizes" || args[0] == "--sizes")
                {
                    PrintSizes();
                    return 0;
                }

                switch (args[0])
                {
                    case "write-empty":
                        if (args.Length != 3) throw new ArgumentException("usage: write-empty <dir> <guid32hex>");
                        WriteEmpty(args[1], args[2]);
                        return 0;

                    case "write-min":
                        if (args.Length != 3) throw new ArgumentException("usage: write-min <dir> <guid32hex>");
                        WriteMinimal(args[1], args[2]);
                        return 0;

                    case "hexdump":
                        if (args.Length != 2) throw new ArgumentException("usage: hexdump <file>");
                        HexDump(args[1]);
                        return 0;

                    case "-h":
                    case "--help":
                    case "help":
                        PrintHelp();
                        return 0;

                    default:
                        Console.Error.WriteLine("unknown command: " + args[0]);
                        PrintHelp();
                        return 2;
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("ERROR: " + ex);
                return 1;
            }
        }

        private static void PrintHelp()
        {
            Console.WriteLine("GoldenGen - Dawn protocol golden-sample generator (protocol 1002)");
            Console.WriteLine("  sizes                          print Marshal.SizeOf / OffsetOf table");
            Console.WriteLine("  write-empty <dir> <guid32hex>  write empty .scene/.jobs");
            Console.WriteLine("  write-min   <dir> <guid32hex>  write minimal quad + directional light + 1 lightmap job");
            Console.WriteLine("  hexdump <file>                 print first 256 bytes");
        }

        private static bool IsHexGuid(string value)
        {
            if (value == null || value.Length != 32)
            {
                return false;
            }

            foreach (char c in value)
            {
                bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                if (!hex)
                {
                    return false;
                }
            }

            return true;
        }

        private static void PrintSizes()
        {
            Console.WriteLine("=== struct sizes ===");
            SizeOf<FGuidInfo>("FGuidInfo");
            SizeOf<FSHAHashInfo>("FSHAHashInfo");
            SizeOf<FLightProfileInfo>("FLightProfileInfo");
            SizeOf<FLightInfo>("FLightInfo");
            SizeOf<FLightFullInfo>("FLightFullInfo");
            SizeOf<FSkyLightInfoHeader>("FSkyLightInfoHeader");
            SizeOf<FTexture2DInfoHeader>("FTexture2DInfoHeader");
            SizeOf<FMaterialColor>("FMaterialColor");
            SizeOf<FMaterialInfoHeader>("FMaterialInfoHeader");
            SizeOf<FSceneBounds>("FSceneBounds");
            SizeOf<FSceneBoxBounds>("FSceneBoxBounds");
            SizeOf<FMeshElementInfo>("FMeshElementInfo");
            SizeOf<FMeshInfoHeader>("FMeshInfoHeader");
            SizeOf<FMeshInstanceInfoHeader>("FMeshInstanceInfoHeader");
            SizeOf<FLandscapeInfoHeader>("FLandscapeInfoHeader");
            SizeOf<FSceneInfoHeader>("FSceneInfoHeader");
            SizeOf<FLightmap2DInput>("FLightmap2DInput");
            SizeOf<FLightmap2DSample>("FLightmap2DSample");
            SizeOf<FLightmap2DOutputHeader>("FLightmap2DOutputHeader");
            SizeOf<FLightmap2DBakingParameters>("FLightmap2DBakingParameters");
            SizeOf<FDawnDenoiserParameters>("FDawnDenoiserParameters");
            SizeOf<FArtifactBakingParameters>("FArtifactBakingParameters");
            SizeOf<FAmbientOcclusionBakingParameters>("FAmbientOcclusionBakingParameters");
            SizeOf<FLightProbeBakingParameters>("FLightProbeBakingParameters");
            SizeOf<FLightBVHParameters>("FLightBVHParameters");
            SizeOf<FSdfShadowBakingParameters>("FSdfShadowBakingParameters");
            SizeOf<FRayTracingBakingParameters>("FRayTracingBakingParameters");
            SizeOf<FRasterizationBakingParameters>("FRasterizationBakingParameters");
            SizeOf<FViewerParameters>("FViewerParameters");
            SizeOf<FLightmap2DOverrideBakingParameters>("FLightmap2DOverrideBakingParameters");
            SizeOf<FBakingJobParameters>("FBakingJobParameters");
            SizeOf<FAdaptiveVolumetricLightmapParameters>("FAdaptiveVolumetricLightmapParameters");
            SizeOf<FSparseVolumetricSamplesParameters>("FSparseVolumetricSamplesParameters");
            SizeOf<FShadowSettingParameters>("FShadowSettingParameters");
            SizeOf<FPrecomputedVisibilityParameterHeader>("FPrecomputedVisibilityParameterHeader");
            SizeOf<FBakingDebugInput_1_0_0>("FBakingDebugInput_1_0_0");
            SizeOf<FPartialJobsTotalNum>("FPartialJobsTotalNum");
            SizeOf<Matrix4x4>("Matrix4x4");
            SizeOf<float2>("float2");
            SizeOf<float3>("float3");
            SizeOf<float4>("float4");

            Console.WriteLine();
            Console.WriteLine("=== offsets ===");
            Offsets<FLightInfo>("FLightInfo",
                "Position", "Normal", "dPdu", "dPdv", "Color", "Dimensions", "Shaping",
                "Attenuation", "FalloffExponent", "Flags", "IESTextureSlice", "IndirectColor",
                "BoundMin", "BoundMax", "LightTextureMode", "ShadowTranslucency",
                "bEnablePenumbra", "PenumbraTint", "PenumbraLuma");
            Offsets<FLightmap2DSample>("FLightmap2DSample",
                "SHVectorR", "SHVectorG", "SHVectorB", "IncidentLighting", "SHCorrection",
                "SkyOcclusion", "AOMaterialMask");
            Offsets<FMaterialInfoHeader>("FMaterialInfoHeader",
                "Hash", "ShadingModelID", "BlendMode", "bTwoSided", "Flags",
                "OpacityMaskClipValue", "IndexInSceneInfo", "DiffuseBoost", "EmissiveBoost");
            Offsets<FMeshInfoHeader>("FMeshInfoHeader",
                "Guid", "LODIndex", "NumTriangles", "NumVertices", "NumElements",
                "LightmapUVIndex", "LightmapUVDensity", "LightmapTexelDensity", "IndexInSceneInfo");
            Offsets<FMeshInstanceInfoHeader>("FMeshInstanceInfoHeader",
                "LevelGuid", "Guid", "Transform", "InverseTransform", "InverseTransposeTransform",
                "LightmapId", "BoundingBox", "MeshIndex", "TexcoordIndex", "Flags",
                "DiffuseBoost", "EmissiveBoost", "RayOffset", "FullyOccludedSamplesFraction",
                "MeshLightAttenuationRadius", "MeshInstanceVisibilityMask",
                "MeshInstanceVisibilityMaskInHLODScene", "LODLevelInHLODScene",
                "UV2MapSize", "UV2Offset");
            Offsets<FLightmap2DInput>("FLightmap2DInput",
                "JobID", "MeshID", "Flags", "IsHLODActorMapping", "ScenePartitionIndex",
                "padding", "Size", "LightmapUVIndex", "OverrideBakingParams");
            Offsets<FBakingJobParameters>("FBakingJobParameters",
                "bUseFastVoxelization", "Lightmap2DParameter", "SdfShadowParameter",
                "AmbientOcclusionParameter", "LightProbeParameter", "RayTracingParameter",
                "ArtifactParameter", "RasterizationParameter", "DenoiserParameters",
                "DebugViewerParameters", "LightBVHParameter", "bPrtHQEnabled", "bPrtLQEnabled");
        }

        private static void SizeOf<T>(string name)
        {
            Console.WriteLine($"{name,-36} {Marshal.SizeOf<T>(),6} bytes");
        }

        private static void Offsets<T>(string typeName, params string[] fields)
        {
            Console.WriteLine($"[{typeName}] sizeof={Marshal.SizeOf<T>()}");
            foreach (string field in fields)
            {
                try
                {
                    Console.WriteLine($"  {field,-40} {Marshal.OffsetOf<T>(field),5}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  {field,-40} <n/a: {ex.GetType().Name}>");
                }
            }
        }

        private static void WriteEmpty(string dir, string guid)
        {
            RequireGuid(guid);
            Directory.CreateDirectory(dir);

            WriteObject(Path.Combine(dir, guid + ".scene"), new FSceneInfo());
            WriteObject(Path.Combine(dir, guid + ".jobs"), new FBakingJobInputs());
        }

        private static void WriteMinimal(string dir, string guid)
        {
            RequireGuid(guid);
            Directory.CreateDirectory(dir);

            WriteObject(Path.Combine(dir, guid + ".scene"), BuildMinimalScene());
            WriteObject(Path.Combine(dir, guid + ".jobs"), BuildMinimalJobs());
        }

        private static void RequireGuid(string guid)
        {
            if (!IsHexGuid(guid))
            {
                throw new ArgumentException("guid must be exactly 32 hex characters (A:B:C:D concatenated)");
            }
        }

        private static void WriteObject<T>(string path, T value) where T : ISerializableObject
        {
            var context = new FileContext(path, true);
            context.WriteVersion((int)EGPUBakingVersion.GPUBAKING_EXPORT_VERSION);
            value.Serialize(context);
            context.Close();

            var info = new FileInfo(path);
            Console.WriteLine($"{Path.GetFileName(path),-40} {info.Length,10} bytes");
        }

        // ------------------------------------------------------------------
        // Minimal scene: one quad (2 triangles) + one directional light.
        // ------------------------------------------------------------------
        private static FSceneInfo BuildMinimalScene()
        {
            var scene = new FSceneInfo();

            // -- mesh ------------------------------------------------------
            var mesh = new FMeshInfo();
            mesh.HeaderInfo.Guid = MakeGuid(0x00000001u, 0u, 0u, 0u);
            mesh.HeaderInfo.LODIndex = 0;
            mesh.HeaderInfo.NumTriangles = 2;
            mesh.HeaderInfo.NumVertices = 4;
            mesh.HeaderInfo.NumElements = 1;
            mesh.HeaderInfo.LightmapUVIndex = 1;
            mesh.HeaderInfo.LightmapUVDensity = 1.0f;
            mesh.HeaderInfo.LightmapTexelDensity = 1.0f;
            mesh.HeaderInfo.IndexInSceneInfo = 0;

            mesh.ElementInfos.AddElement(new FMeshElementInfo
            {
                StartIndex = 0,
                PrimitiveCount = 2,
                MaterialIndex = 0,
                Flags = 0
            });

            // 2x2 quad on the XZ plane at y=0, facing +Y.
            var positions = new[]
            {
                new float3(-1, 0, -1),
                new float3(-1, 0,  1),
                new float3( 1, 0,  1),
                new float3( 1, 0, -1),
            };
            var normal = new float3(0, 1, 0);
            var tangent = new float3(1, 0, 0);
            var bitangent = new float3(0, 0, 1);

            for (int i = 0; i < 4; ++i)
            {
                mesh.VertexBuffer.AddElement(new float4(positions[i].x, positions[i].y, positions[i].z, 1.0f));
                mesh.NormalBuffer.AddElement(new float4(normal.x, normal.y, normal.z, 0.0f));
                // Dawn's exporter swaps these on purpose (DawnMeshExporter.cs:773-775).
                mesh.TangentBuffer.AddElement(new float4(bitangent.x, bitangent.y, bitangent.z, 0.0f));
                mesh.BiTangentBuffer.AddElement(new float4(tangent.x, tangent.y, tangent.z, 0.0f));
            }

            // Two triangles, CCW when viewed from +Y.
            mesh.Index16BitBuffer.AddElement((ushort)0);
            mesh.Index16BitBuffer.AddElement((ushort)1);
            mesh.Index16BitBuffer.AddElement((ushort)2);
            mesh.Index16BitBuffer.AddElement((ushort)0);
            mesh.Index16BitBuffer.AddElement((ushort)2);
            mesh.Index16BitBuffer.AddElement((ushort)3);

            var uv0 = new FMeshTexcoordBuffer();
            uv0.TexcoordBuffer.AddElement(new float2(0, 0));
            uv0.TexcoordBuffer.AddElement(new float2(0, 1));
            uv0.TexcoordBuffer.AddElement(new float2(1, 1));
            uv0.TexcoordBuffer.AddElement(new float2(1, 0));

            var uv1 = new FMeshTexcoordBuffer();
            uv1.TexcoordBuffer.AddElement(new float2(0, 0));
            uv1.TexcoordBuffer.AddElement(new float2(0, 1));
            uv1.TexcoordBuffer.AddElement(new float2(1, 1));
            uv1.TexcoordBuffer.AddElement(new float2(1, 0));

            mesh.TexcoordBuffers.AddElement(uv0);
            mesh.TexcoordBuffers.AddElement(uv1);
            mesh.TexcoordBuffers.AddElement(uv1);

            scene.Meshes.AddElement(mesh);
            scene.HeaderInfo.NumMeshes = 1;

            // -- material --------------------------------------------------
            var material = new FMaterialInfo();
            material.HeaderInfo.Hash = new FSHAHashInfo { Hash = new byte[20] };
            material.HeaderInfo.ShadingModelID = 0;
            material.HeaderInfo.BlendMode = 0;
            material.HeaderInfo.bTwoSided = 0;
            material.HeaderInfo.Flags = 0;
            material.HeaderInfo.OpacityMaskClipValue = 0.5f;
            material.HeaderInfo.IndexInSceneInfo = 0;
            material.HeaderInfo.DiffuseBoost = 1.0f;
            material.HeaderInfo.EmissiveBoost = 1.0f;

            FillTexture(material.DiffuseTexture, 1, 1, new FMaterialColor { x = 255, y = 255, z = 255, w = 255 });
            FillTexture(material.EmissiveTexture, 1, 1, new FMaterialColor { x = 0, y = 0, z = 0, w = 255 });
            FillTexture(material.NormalTexture, 1, 1, new FMaterialColor { x = 128, y = 128, z = 255, w = 255 });
            FillTexture(material.SpecularTexture, 1, 1, new FMaterialColor { x = 0, y = 128, z = 255, w = 255 });

            scene.Materials.AddElement(material);
            scene.HeaderInfo.NumMaterials = 1;

            // -- mesh instance --------------------------------------------
            var instance = new FMeshInstanceInfo();
            instance.HeaderInfo.LevelGuid = MakeGuid(0, 0, 0, 0);
            instance.HeaderInfo.Guid = MakeGuid(0x00000002u, 0u, 0u, 0u);
            instance.HeaderInfo.Transform = IdentityMatrix();
            instance.HeaderInfo.InverseTransform = IdentityMatrix();
            instance.HeaderInfo.InverseTransposeTransform = IdentityMatrix();
            instance.HeaderInfo.LightmapId = 0;
            instance.HeaderInfo.BoundingBox = new FSceneBoxBounds(new float3(-1, 0, -1), new float3(1, 0, 1));
            instance.HeaderInfo.MeshIndex = 0;
            instance.HeaderInfo.TexcoordIndex = 0;
            instance.HeaderInfo.Flags = 0;
            instance.HeaderInfo.DiffuseBoost = 1.0f;
            instance.HeaderInfo.EmissiveBoost = 1.0f;
            instance.HeaderInfo.RayOffset = 0.0f;
            instance.HeaderInfo.FullyOccludedSamplesFraction = 1.0f;
            instance.HeaderInfo.MeshLightAttenuationRadius = 0.0f;
            instance.HeaderInfo.MeshInstanceVisibilityMask = 1;
            instance.HeaderInfo.MeshInstanceVisibilityMaskInHLODScene = 1;
            instance.HeaderInfo.LODLevelInHLODScene = 0;
            instance.HeaderInfo.UV2MapSize = new uint2 { x = 64, y = 64 };
            instance.HeaderInfo.UV2Offset = new uint2 { x = 0, y = 0 };
            instance.MaterialOverrides.AddElement(0u);

            scene.MeshInstances.AddElement(instance);
            scene.HeaderInfo.NumMeshInstances = 1;
            scene.MeshGuids.AddElement(instance.HeaderInfo.Guid);
            scene.HeaderInfo.NumMeshGuids = 1;

            // -- sky light -------------------------------------------------
            scene.SkyLight.SkyData = new FSkyLightInfoHeader
            {
                Color = new float3(0.05f, 0.05f, 0.08f),
                EnvColor = new float3(0.05f, 0.05f, 0.08f),
                MipDimensions = new int3(1, 1, 1),
                transform = IdentityMatrix(),
                IndirectLightingIntensity = 1.0f,
                ShadowTranslucency = 0.0f,
                Flags = 0
            };
            scene.SkyLight.EnvCube.AddElement(new float4(0.05f, 0.05f, 0.08f, 1.0f));

            // -- directional light ----------------------------------------
            scene.Lights.AddElement(new FLightInfo
            {
                Position = new float3(0, 10, 0),
                Normal = new float3(0.5f, -1.0f, 0.5f),
                dPdu = new float3(1, 0, 0),
                dPdv = new float3(0, 0, 1),
                Color = new float3(3.0f, 3.0f, 3.0f),
                Dimensions = new float3(0, 0, 0),
                Shaping = new float2(0, 0),
                Attenuation = 1.0f,
                FalloffExponent = 0.0f,
                Flags = (uint)(GPUBakingConst.DAWN_PT_FLAG_DIRECT_LIGHTING |
                               GPUBakingConst.DAWN_PT_FLAG_INDIRECT_LIGHTING |
                               GPUBakingConst.DAWN_PT_LIGHT_DIRECTIONAL),
                IESTextureSlice = -1,
                IndirectColor = new float3(3.0f, 3.0f, 3.0f),
                BoundMin = new float3(-1000, -1000, -1000),
                BoundMax = new float3(1000, 1000, 1000),
                LightTextureMode = 0,
                ShadowTranslucency = 0.0f,
                bEnablePenumbra = 0,
                PenumbraTint = new float4(1, 1, 1, 1),
                PenumbraLuma = 1.0f
            });
            scene.HeaderInfo.NumLights = 1;
            scene.LightGuids.AddElement(MakeGuid(0x00000003u, 0u, 0u, 0u));
            scene.LightPowers.AddElement(1.0f);

            // -- bounds ----------------------------------------------------
            scene.HeaderInfo.Bounds = new FSceneBounds(new float3(0, 0, 0), new float3(1, 0.01f, 1), 1.5f);
            scene.HeaderInfo.ImportanceBounds = scene.HeaderInfo.Bounds;

            return scene;
        }

        private static FBakingJobInputs BuildMinimalJobs()
        {
            var jobs = new FBakingJobInputs();

            // Defaults mirror DawnSettings.Initialize* (v2.4.5).
            jobs.BakingParameters.bUseFastVoxelization = 0;

            jobs.BakingParameters.Lightmap2DParameter.DenoiserMode = 0; // DAWN
            jobs.BakingParameters.Lightmap2DParameter.IterationNum = 64;
            jobs.BakingParameters.Lightmap2DParameter.MaxDepth = 3;
            jobs.BakingParameters.Lightmap2DParameter.SuperSampleFactor = 1;
            jobs.BakingParameters.Lightmap2DParameter.MaxLightmapResolution = 64;
            jobs.BakingParameters.Lightmap2DParameter.PenumbraShadowFraction = 0.3f;
            jobs.BakingParameters.Lightmap2DParameter.SkyOcclusionCompensateScale = 1.0f;
            jobs.BakingParameters.Lightmap2DParameter.OptixSmoothLevel = 0;
            jobs.BakingParameters.Lightmap2DParameter.bGPURaster = 0;
            jobs.BakingParameters.Lightmap2DParameter.bUseLightmapDirection = 1;
            jobs.BakingParameters.Lightmap2DParameter.bSkyOcclusionDenoise = 0;

            jobs.BakingParameters.RayTracingParameter.MinNormalOffset = 0.5f;
            jobs.BakingParameters.RayTracingParameter.MaxRayOffset = 10.0f;
            jobs.BakingParameters.RayTracingParameter.MinRayOffset = 0.0001f;
            jobs.BakingParameters.RayTracingParameter.NormalOffsetSampleRadiusScale = 0.005f;
            jobs.BakingParameters.RayTracingParameter.TangentOffsetSampleRadiusScale = 0.008f;

            jobs.BakingParameters.RasterizationParameter.RasterizationBias = 1.0f;
            jobs.BakingParameters.RasterizationParameter.SmallestTexelRadius = 0.1f;
            jobs.BakingParameters.RasterizationParameter.bUseConservativeTexelRasterization = 0;
            jobs.BakingParameters.RasterizationParameter.bUseMaxWeight = 0;

            jobs.BakingParameters.ArtifactParameter.bFillUnmappedTexel = 1;
            jobs.BakingParameters.ArtifactParameter.bSeamFixed = 0;

            jobs.BakingParameters.LightBVHParameter.DirectLightSampleMode = 2; // MIS
            jobs.BakingParameters.LightBVHParameter.MaxPathIntensity = 50.0f;
            jobs.BakingParameters.LightBVHParameter.LightGridResolution = 256;
            jobs.BakingParameters.LightBVHParameter.MaxLightCountPerGrid = 256;
            jobs.BakingParameters.LightBVHParameter.VirtualLightDirectIntensity = 1.0f;
            jobs.BakingParameters.LightBVHParameter.VirtualLightIndirectIntensity = 1.0f;
            jobs.BakingParameters.LightBVHParameter.VirtualLightSampleMode = 1; // FVirtualLightGrid

            jobs.BakingParameters.DenoiserParameters.bEnableDawnLightmapDenoiser = 0;
            jobs.BakingParameters.DenoiserParameters.bDenoiserSoftShadow = 0;
            jobs.BakingParameters.DenoiserParameters.lightLeakFixMode = 0;
            jobs.BakingParameters.DenoiserParameters.directSmoothFactor = 0;
            jobs.BakingParameters.DenoiserParameters.indirectSmoothFactor = 0;

            jobs.DebugInput.debugInput_1_0_0.DawnLogLevel = 0;

            var input = new FLightmap2DInput
            {
                JobID = MakeGuid(0u, 0u, 0u, 5u),
                MeshID = MakeGuid(0x00000002u, 0u, 0u, 0u), // must match FMeshInstanceInfo.Guid
                Flags = 0,
                IsHLODActorMapping = 0,
                ScenePartitionIndex = 0,
                padding = 0,
                Size = new int2(64, 64),
                LightmapUVIndex = 1
            };

            jobs.Lightmap2DJobs.AddElement(input);
            return jobs;
        }

        private static Matrix4x4 IdentityMatrix() => new Matrix4x4
        {
            mm = new[]
            {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            }
        };

        private static void FillTexture<TColor>(FTexture2DInfo<TColor> texture, int width, int height, TColor color)
            where TColor : struct
        {
            texture.HeaderInfo.SizeX = (uint)width;
            texture.HeaderInfo.SizeY = (uint)height;
            texture.HeaderInfo.WrapMode = 0;
            texture.HeaderInfo.Format = 0;
            for (int i = 0; i < width * height; ++i)
            {
                texture.Colors.AddElement(color);
            }
        }

        private static FGuidInfo MakeGuid(uint a, uint b, uint c, uint d) =>
            new FGuidInfo { A = a, B = b, C = c, D = d };

        private static void HexDump(string path)
        {
            byte[] bytes = File.ReadAllBytes(path);
            int count = Math.Min(bytes.Length, 256);
            var sb = new StringBuilder();
            for (int i = 0; i < count; ++i)
            {
                if (i % 16 == 0)
                {
                    if (i != 0)
                    {
                        Console.WriteLine(sb.ToString());
                    }

                    sb.Clear();
                    sb.Append($"{i:X8}  ");
                }

                sb.Append($"{bytes[i]:X2} ");
            }

            if (sb.Length > 0)
            {
                Console.WriteLine(sb.ToString());
            }

            Console.WriteLine($"({bytes.Length} bytes total)");
        }
    }
}

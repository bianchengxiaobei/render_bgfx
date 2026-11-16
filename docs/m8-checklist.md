# M8 Scalable Light Lists（32/64/128 容量）规划
> 工程根：`E:\Learn\hdrp-bgfx`。> 状态：**M8.0–M8.7 完成**（2026-11-14）；M8 验收通过。> 原则：先读 HDRP 与 vendored bgfx 源码再实现；不改动 M7 已验证的视觉语义；容量扩展必须保留 32-light M7 基线的 **0 diff** 回退。> 前置基线：M7.8 已完成，`supportGpuLightList=true`、`supportGpuLightListBuffers=true` 默认开启；texture/CPU 仍可回退；cluster 每 cell 固定 32。
---

## 0. 缁撹鎽樿

M8 鐨勭洰鏍囨槸鎶?punctual light 瀹归噺浠庡浐瀹?32 鎵╁睍鍒?**32/64/128 涓夋。**锛屽悓鏃朵繚鎸?M7 鐨?FPTL 16脳16銆乥ig-tile 64脳64銆乧luster 64脳64脳32銆佸崌搴?light index 鍜?CPU/texture/buffer 鍙鐓ц涔夈€?
M8 鐨勬牳蹇冨伐浣滐細

1. **瀹归噺濂戠害**锛歚lightListCapacity = 32|64|128`锛屾€?slot 鍖呭惈鏈€澶?1 涓?directional锛沺unctual 鏈€澶氫负 capacity-1锛堟湁鏂瑰悜鍏夋椂锛夈€傞粯璁ゅ€欓€?64锛?2 涓哄熀绾垮洖閫€锛?28 涓?experimental銆?2. **鍔ㄦ€?list stride**锛欶PTL/big-tile 鐨?`count + capacity` 琛屽竷灞€浠庡浐瀹?33 鏀逛负杩愯鏃?`capacity+1`锛沜luster 淇濇寔鍥哄畾 33锛堟瘡 cell 鏈€澶?32锛夈€?3. **GPU producer/consumer 鎵╁**锛歴creen AABB銆乥ig-tile銆丗PTL銆乧luster 鍜屾墍鏈夎鍙?list 鐨?shader 绉婚櫎纭紪鐮?32锛?28 slot 鐢?4 涓?32-bit mask锛?4 threads 寰幆鍐欏嚭銆?4. **瀹归噺閫夋嫨涓庡洖閫€**锛歜uffer path 鏀寔 128锛泃exture path 鍙?`maxTextureSize` 闄愬埗锛岃秴闄愭椂鎸?128鈫?4鈫?2 闄嶄綆 effective capacity 骞朵竴娆℃€у憡璀︼紝涓嶉潤榛樻埅鏂垨瓒婄晫銆?5. **楠屾敹涓庢€ц兘**锛?2 涓?M7 0 diff锛?4 鍙屽悗绔€丆PU/texture/buffer 0 diff锛?28 buffer 0 diff锛?4/128 鐨?1080p 鎬ц兘鍜屽唴瀛樻湁璁板綍锛孧8.6 鍐嶅喅瀹氶粯璁ゅ閲忋€?
M8 **涓嶅紩鍏?* HDRP 鐨?16-bit packed list銆乴ayered cluster offset銆乴ight category/feature variants銆乴ight layers銆乧ookies銆乤rea lights銆丟PU shadow atlas packing銆傝繖浜涘湪 M9 鎴栧悗缁ā鍧楀鐞嗐€?
### 0.1 涓?M7 璁″垝鐨勫樊寮?
M7 鏂囨。鏇炬妸 layered cluster offset 鍜?64/128 lights 鐣欑粰 M8銆侻8 閲嶆柊璇勪及鍚庡仛濡備笅鏀舵暃锛?
- **64/128 瀹归噺杩涘叆 M8**锛岃繖鏄?M8 鐨勪富浣撱€?- **layered offset/16-bit packing 涓嶈繘鍏?M8**锛氬綋鍓嶅浐瀹?32-bit row 鍦?128 lights銆?080p 涓嬬害 6.6 MB锛屼粛鍙帴鍙楋紱鍏堜繚璇?0 diff銆乼exture fallback 鍜屽閲忔纭紝閬垮厤鍚屾椂鏀?list 缂栫爜銆乧luster 鍒嗛厤鍜屾墍鏈夋秷璐硅€呫€?- **cluster 姣?cell 涓婇檺淇濇寔 32**锛欻DRP 鐨?cluster cell 纭笂闄愭槸 24锛堥粯璁よ祫浜у€?8锛宍ShaderConfig.cs:70`/`GlobalLightLoopSettings.cs:207`锛夛紝鏈」鐩?32 宸查珮浜?HDRP锛?4/128 鍙彁楂樻€诲閲忓拰 FPTL/big-tile 瀹归噺锛屼笉鏀瑰彉 cluster cell 璇箟銆?- **category/layer/feature variants/cookies** 涓嶈繘鍏?M8锛涘畠浠細鏀瑰彉鏉愯川 lighting variant銆丟Buffer channel 鎴?light data 濂戠害锛屽崟鐙綔涓?M9 瑙勫垝銆?
### 0.2 榛樿鍐崇瓥锛堟殏瀹氾級

- 寮€鍙戞湡榛樿浠嶄负 32锛孧8.6 閫氳繃闂ㄧ鍚庢墠鑰冭檻鎶婇粯璁ゆ敼涓?64銆?- 128 姘歌繙鍏堜綔涓?opt-in/experimental锛涗笉鍥?128 鑳借窇灏遍粯璁ゅ紑鍚€?- `supportGpuLightListBuffers` 缁存寔 M7.8 鐨勯粯璁?true锛沚uffer 涓嶅彲鐢ㄦ椂 texture/CPU 鍥為€€銆?- M7 鐨?GPU shadow culling/indirect 榛樿浠嶄负 false锛孧8 涓嶆敼鍙樸€?
---

## 1. 褰撳墠鍩虹嚎涓庣己鍙?
### 1.1 褰撳墠 32-light 濂戠害

- `src/render/lighting/gpu_light_data.h:11-18`锛?  - `kMaxGpuLights=32`銆乣kLightListMaxLights=32`銆乣kLightListStride=33`锛?  - `kLightTileSize=16`銆乣kLightBigTileSize=64`銆乣kClusterTileSize=64`銆乣kClusterSliceCount=32`锛?  - `GpuLightData` 涓?`32 * 6` 涓?`Vec4`銆?- `src/render/lighting/light_builder.cpp:92-95`锛歞irectional 鏈€澶?1 涓紝`maxPunctual = kMaxGpuLights - 1`锛涙寜 importance 鎺掑簭鍚庢埅鏂€?- `src/render/lighting/light_list_builder.cpp:176-332`锛欳PU 鍚屾椂鏋勫缓 FPTL銆乥ig-tile銆乧luster锛岃緭鍑?`count + 32 indices`锛宑luster 鍥哄畾 33 texel row銆?- `shaders/common/light_data.sc:14-140`锛歚HBG_LIGHT_LIST_STRIDE=33`銆乣HBG_LIGHT_CLUSTER_STRIDE=33`銆乣HBG_LIGHT_CLUSTER_MAX_LIGHTS=32`锛屽苟纭紪鐮?32 鐨?loop 涓婇檺銆?- `shaders/lighting/light_loop.sc:78-116`锛歚lightCount` 涓?tile/cluster count 閮?`min(..., 32)`銆?- `src/render/passes/forward_renderer.cpp:559-566`锛歭ight data texture 楂樺害 32銆乻creen bounds 瀹藉害 `32*2`銆?- `src/render/passes/forward_renderer.cpp:1327-1331`銆乣:1758-1766`锛歜uffer 澶у皬鎸夊浐瀹?33 stride 璁＄畻骞舵牎楠屻€?
### 1.2 褰撳墠 GPU producer

- `light_screen_aabb_buffer.cs.sc`锛?4 threads 瑕嗙洊 32 涓粷瀵?slot锛涙棤鏁?slot 鍐欓浂銆?- `light_big_tile_buffer.cs.sc`锛氫竴涓?workgroup 瀵瑰簲 64脳64 big tile锛宍uint` mask 鍙兘瑕嗙洊 32 涓?light銆?- `light_fptl_buffer.cs.sc`锛氫竴涓?workgroup 瀵瑰簲 16脳16 tile锛宮ask/32-slot shared list銆?- `light_cluster_buffer.cs.sc`锛氫竴涓?workgroup 瀵瑰簲 `(64脳64 tile, slice)`锛屾渶澶?32 light銆?- M7 鐨?`light_list_passes=4`锛坈luster锛?`3`锛坣o-cluster锛?`5`锛坕ndirect锛変繚鎸佷笉鍙樸€?
### 1.3 褰撳墠闄愬埗

- 瓒呰繃 32 涓?punctual lights 鏃讹紝`LightBuilder` 鐩存帴鎴柇锛涙病鏈夎瘖鏂鏄庤鎴柇鐨勬暟閲忋€?- FPTL/big-tile/cluster 鐨?stride 鍜?mask 閮芥槸缂栬瘧鏈?32锛屾棤娉曞彧鏀逛竴涓缃墿瀹广€?- cluster 姣?cell 宸插浐瀹?32锛涙墿瀹规€诲閲忔椂濡傛灉鐩存帴娌跨敤锛屼細鍑虹幇鈥淔PTL 鏈?64銆乧luster 鍙湁 32鈥濈殑棰勬湡宸紓锛岄渶瑕佹槑纭祴璇曞拰 debug銆?- texture path 鐨?list 瀹藉害闅?capacity 澧為暱锛屽彲鑳借秴杩?backend `maxTextureSize`锛沚uffer path 鏃犺闄愬埗銆?
---

## 2. HDRP 婧愮爜缁撹

### 2.1 瀹归噺涓庝笂闄?
HDRP 鐨?FPTL 瀹归噺鏄璁炬。浣嶏紝涓嶆槸浠绘剰鍊硷細

- `com.unity.render-pipelines.high-definition-config/Runtime/ShaderConfig.cs:10-19`锛?  - `FPTLMaxLightSizes.Low=31`銆乣High=63`銆乣Higher=127`銆乣Ultra=255`銆?- `ShaderConfig.cs:58-70`锛氶粯璁?`FPTLMaxLightCount=High (63)`锛沗LightClusterMaxCellElementCount=24`銆?- `ShaderConfig.cs:132-151`锛?  - big-tile `s_MaxNrBigTileLightsPlusOne = clamp((FPTLMax+1)*8, 512, 1024)`锛?  - FPTL `s_LightDwordPerFptlTile = (FPTLMax+1)/2`锛?6-bit packed锛夛紱
  - cluster block/offset packing bits銆?- `GlobalLightLoopSettings.cs:201-207`锛歚maxDirectionalLightsOnScreen=16`銆乣maxPunctualLightsOnScreen=512`銆乣maxLightsPerClusterCell=8`锛堣繍琛屾椂璧勪骇鍙厤锛孲haderConfig 鍐?cap 24锛夈€?- `LightLoop.cs:685-686`锛氫粠 `lightLoopSettings` 璇诲彇 directional/punctual 涓婇檺銆?
瀵?M8 鐨勫惈涔夛細HDRP 鐨?63/127 瀵瑰簲鏈」鐩殑 64/128 鎬?slot锛堝惈 1 涓?directional锛宲unctual 63/127锛夛紝瀹归噺鏂瑰悜鍜?HDRP High/Higher 涓€鑷达紱鏈」鐩粛杩滀綆浜?HDRP 鐨?512 punctual 涓婇檺銆?
### 2.2 FPTL/big-tile 鍒楄〃

HDRP 浣跨敤 16-bit light index锛宑ount 鏀惧湪绗竴涓?dword 鐨勪綆 16 浣嶏細

- `LightLoopDef.hlsl:232-262`锛?  - `GetCountAndStartTile` 璇?`g_vLightListTile[...] & 0xffff`锛?  - `FetchIndex` 鐢?`(lightOffset+1)>>1` 鍜?16-bit shift 鍙?index銆?- `lightlistbuild.compute:413-422`锛?  - 姣忎釜 category 鐨?count clamp 鍒?`SHADEROPTIONS_FPTLMAX_LIGHT_COUNT`锛?  - 姣忎釜 dword 鍐欎袱涓?16-bit index銆?- `HDRenderPipeline.LightLoop.cs:621-657`锛?  - FPTL buffer 涓?`LightCategory.Count * s_LightDwordPerFptlTile * nrTiles`锛?  - big-tile buffer 涓?`s_MaxNrBigTileLightsPlusOne * nrBigTiles / 2`锛?  - cluster offset/index 涓虹嫭绔?buffer銆?
M8 **涓嶇Щ妞?* 16-bit packing锛氬綋鍓?32-bit 琛屽竷灞€鍦?64/128 涓嬪唴瀛樺彲鎺ュ彈锛屼笖鑳戒繚鎸?texture fallback 涓?M7 0 diff锛?6-bit packed list 浣滀负 M9 鐨勫彲閫変紭鍖栵紝灞婃椂鍜?category/layered 涓€璧峰仛銆?
### 2.3 Cluster 涓?layered offset

HDRP cluster 鐢变袱閮ㄥ垎缁勬垚锛?
- `ClusteredUtils.hlsl:106-111`锛歚GenerateLayeredOffsetBufferIndex` 鎸?category/tile/cluster 璁＄畻 offset buffer 涓嬫爣銆?- `LightLoopDef.hlsl:286-315`锛?  - `UnpackClusterLayeredOffset` 浠?packed uint 鎷?offset/count锛?  - `GetCountAndStartCluster` 鍏堣 offset/count锛屽啀浠?`g_vLightListCluster[start+i]` 鍙?index銆?- `lightlistbuild-clustered.compute:76-78`锛歚PackClusterLayeredOffset(offset,count)`銆?- `lightlistbuild-clustered.compute:482-502`锛氬啓鍏?`g_vLayeredLightList`锛屽苟鐢?`g_LayeredOffset` 璁板綍姣忎釜 category/tile/cluster 鐨?start/count銆?- `LightLoop.cs:871-874`锛氭瘡 cluster tile 鐨?index 瀹归噺绾︿负 `(FPTLMax+1) * (1 << (log2Clusters+1))`銆?
M8 淇濇寔鏈」鐩浐瀹?row锛歚count + 32 indices`锛宑luster stride 33锛涘師鍥犳槸锛?
1. M8 鐨?64/128 鍙墿鎬诲閲忥紝cluster cell 涓婇檺浠?32锛?2. 鍥哄畾 row 鍙繚鎸佺‘瀹氭€у崌搴忚緭鍑哄拰 M7 0 diff锛?3. layered/atomic append 浼氬紩鍏ラ潪纭畾鎬ч『搴忔垨澶氫釜 buffer锛岄€傚悎鍜?category/layer 涓€璧峰湪 M9 鍋氥€?
### 2.4 HDRP 涓庢湰椤圭洰鏄犲皠

| 椤?| HDRP | M8 鏈」鐩?|
|---|---|---|
| FPTL 瀹归噺 | 31/63/127/255锛坄ShaderConfig.cs:10-19`锛?| 32/64/128 鎬?slot锛堝惈 1 directional锛?|
| big-tile 涓婇檺 | `clamp((FPTLMax+1)*8,512,1024)`锛坄ShaderConfig.cs:133`锛?| effective capacity锛?2/64/128锛?|
| tile 灏哄 | FPTL 16銆乥ig-tile 64銆乧luster 32銆?2 slices锛坄LightLoop.cs:106-108`锛?| FPTL 16銆乥ig-tile 64銆乧luster 64脳32 slices锛堜繚鎸?M6/M7锛?|
| cluster cell 涓婇檺 | 榛樿璧勪骇 8锛孲haderConfig cap 24锛坄GlobalLightLoopSettings.cs:207`銆乣ShaderConfig.cs:70`锛?| 鍥哄畾 32 |
| FPTL 缂栫爜 | 16-bit packed + count low 16锛坄LightLoopDef.hlsl:242-262`锛?| 32-bit row + count dword锛圡8 鏆備笉 packed锛?|
| cluster offset | layered `g_LayeredOffset` + 鍏ㄥ眬 index list锛坄LightLoopDef.hlsl:286-315`锛?| 鍥哄畾 33 row锛岀‘瀹氭€у崌搴忥紙M8 涓?layered锛?|
| category/feature | Punctual/Area/Env/Decal + feature flags锛坄LightLoop.cs:66-83`锛?| 浠?punctual + 1 directional |
| 榛樿瀹归噺 | FPTL High=63锛坄ShaderConfig.cs:65`锛?| 32 baseline锛?4 鍊欓€夛紱128 experimental |

### 2.5 涓嶈繘鍏?M8 鐨?HDRP 鑳藉姏

- **Category/feature flags**锛歚LightLoop.cs:66-83`锛圥unctual/Area/Env/Decal 涓?feature flags锛夛紝`lightlistbuild.compute:356-386` 鐨勫垎绫昏鏁?feature flags锛汳8 鍙湁 punctual + 1 directional銆?- **Light layers**锛歚LightLoop.hlsl:305` 鐨?`IsMatchingLightLayer`锛宍HDAdditionalLightData.cs:1033-1073` 鐨?light layer mask锛涢渶瑕?GBuffer/prepass rendering-layer channel锛孧9 鍗曠嫭瑙勫垝銆?- **Cookies/IES**锛歚LightCookieManager.cs:47-62`銆乣:242-317` 鐨?cookie atlas/atlas packing锛汳9 鎴栫嫭绔嬫ā鍧椼€?- **Shadow request/atlas**锛歚HDShadowCullingUtils.cs:64-132`銆乣HDShadowManager.cs:1414-1452` 浠嶆槸 CPU request/atlas锛汳8 涓嶆敼 shadow 鏁版嵁濂戠害銆?
---

## 3. M8 浠诲姟鎷嗗垎

| 浠诲姟 | 浜у嚭 | 鐘舵€?|
|---|---|---|
| **M8.0 瀹归噺濂戠害/鍩虹嚎閿?* | `32/64/128` 璁剧疆銆丆LI/JSON/UI銆両DL銆?2 baseline 鍥炲綊 | `[x]` 瀹屾垚锛圙PU >32 鏆?gate锛?|
| **M8.1 CPU reference** | `LightBuilder`/`LightListBuilder` 瀹归噺鍙傛暟銆佽竟鐣?鎴柇娴嬭瘯 | `[x]` 瀹屾垚 |
| **M8.2 GPU FPTL/big-tile** | 128-slot AABB、4×mask、动态 stride、64/128 0 diff | `[x]` 完成（2026-11-13） |
| **M8.3 GPU cluster** | 32/cell 保持、128 输入、overflow 打包/debug、0 diff | `[x]` 完成（2026-11-13） |
| **M8.4 Buffer/texture consumers** | forward/deferred/contact/fog/debug 移除 32 硬编码；texture/CPU 128→64→32 clamp | `[x]` 完成（2026-11-14） |
| **M8.5 集成/回退/CLI/UI** | capacity 选择、texture 宽度 clamp、resize/4K、诊断 | [x] 完成（2026-11-14） |
| **M8.6 性能与默认决策** | 32/64/128 1080p 300 帧、内存、默认值 | [x] 完成（2026-11-14） |
| **M8.7 M8 验收** | 双后端矩阵、测试/IDL/gfx/docs、边界 | [x] 完成（2026-11-14） |

渚濊禆锛歁8.0 鈫?M8.1 鈫?M8.2/M8.3 鈫?M8.4 鈫?M8.5 鈫?M8.6 鈫?M8.7銆侻8.2 涓?M8.3 鍦?M8.1 鍚庡彲浠ュ苟琛岋紝浣嗛兘蹇呴』鍏堟湁 CPU reference銆?
---

## 4. 鏁版嵁濂戠害

### 4.1 瀹归噺妗ｄ綅

| 璁剧疆 | 鎬?slot | 鏈?directional 鏃?punctual 涓婇檺 | FPTL/big-tile stride | cluster stride | cluster cell 涓婇檺 |
|---|---:|---:|---:|---:|---:|
| 32 | 32 | 31 | 33 | 33 | 32 |
| 64 | 64 | 63 | 65 | 33 | 32 |
| 128 | 128 | 127 | 129 | 33 | 32 |

- `lightListCapacity` 鏄€?slot锛涙病鏈?directional 鏃?punctual 鍙互浣跨敤鍏ㄩ儴 capacity銆?- directional 浠嶅彧鏀寔 1 涓富鏂瑰悜鍏夛紱澶?directional 涓嶅湪 M8銆?- cluster 姣?cell 涓婇檺淇濇寔 32锛涘嵆浣挎€诲閲?64/128锛宑luster 涔熶笉浼氳秴杩?32銆俙--no-light-cluster` 璧?FPTL 鏃跺彲浠ョ湅鍒板畬鏁?64/128銆?- FPTL/big-tile 姣?tile 鐨?light 涓婇檺绛変簬 effective capacity銆?- 鎵€鏈?list 浠嶆寜缁濆 `GpuLightData` index 鍗囧簭锛沗LightBuilder` 鐨?importance 鎺掑簭鍙喅瀹氭埅鏂摢浜?light锛屼笉鏀瑰彉 list 鍐呴儴鍗囧簭銆?
### 4.2 GpuLightData 涓?uniform

- `kMaxGpuLights` 缂栬瘧鏈熶笂闄愭敼涓?**128**锛沗GpuLightData::texels` 涓?`128 * 6` 涓?`Vec4`銆?- light data texture 楂樺害涓?**effective capacity**锛?2/64/128锛夛紝瀹藉害浠?6锛沜onsumer 璇诲彇 `index < capacity`銆?- `LightScreenBounds` buffer 鍥哄畾 `2 * 128` 涓?vec4锛涜秴鍑?effective capacity 鐨?slot 鍐欓浂銆?- `PassUniforms`/`ClusterUniforms` 鏂板鎴栭暅鍍?`lightListParams2 = (listStride, capacity, maxFptlLights, maxBigTileLights)`锛?  - `listStride = effectiveCapacity + 1`锛?  - `capacity = effectiveCapacity`锛?  - `maxFptlLights = maxBigTileLights = effectiveCapacity`锛?  - cluster cell 涓婇檺浠嶇敱 `HBG_LIGHT_CLUSTER_MAX_LIGHTS=32` 琛ㄨ揪銆?- 涓嶆敼鍙?`lightListParams = (tileSize,tileCountX,tileCountY,enabled)` 鐨勬棦鏈夎涔夈€?
### 4.3 FPTL/big-tile 琛屽竷灞€

淇濇寔 M6/M7 鐨?32-bit 琛屽竷灞€锛屽彧鎶?stride 鍙樻垚杩愯鏃讹細

```text
row = (tileY * tileCountX + tileX) * listStride
row[0] = lightCount
row[1..capacity] = absolute light index锛堝崌搴忥級
```

- texture path锛歚width = tileCountX * listStride`锛宍height = tileCountY`锛?- buffer path锛歚count = tileCountX * tileCountY * listStride`锛?- big-tile 鍚屾牱浣跨敤 `listStride`锛屾瘡 big tile 鏈€澶?effective capacity 涓?light锛?- 涓嶆敼鍙?16脳16/64脳64 tile 灏哄鍜屽崌搴忚涔夈€?
### 4.4 Cluster 琛屽竷灞€

淇濇寔鍥哄畾 33 stride锛?
```text
row = (tileY * 32 + slice) * tileCountX + tileX
row[0] = lightCount锛?= 32锛?row[1..32] = absolute light index锛堝崌搴忥級
```

- cluster 杈撳叆浠?big-tile list 璇诲彇锛宐ig-tile 鍙互鏈?64/128 涓?light锛?- cluster 杈撳嚭 clamp 鍒?32锛涜秴杩?32 鐨?light 涓嶈繘鍏?cluster锛屼絾璁″叆 overflow 璇婃柇锛?- debug slice銆乼ransparent/forward-only銆乫og 鐨?cluster 璇诲彇淇濇寔 M7 璇箟锛?- 涓嶅紩鍏?`g_LayeredOffset`/鍏ㄥ眬 atomic append锛汳8 鐨?cluster 鏄浐瀹?row锛岀‘瀹氭€у崌搴忋€?
### 4.5 璺緞涓?effective capacity

涓夊眰璺緞淇濇寔锛?
```text
buffer GPU -> texture GPU -> CPU fallback
```

- **buffer**锛氭敮鎸?32/64/128锛屼笉鍙?texture width 闄愬埗锛涙墍鏈?list buffer 鎸?effective capacity 閲嶅缓銆?- **texture**锛氭敮鎸?32/64/128锛屼絾 `width = tileCountX * (capacity+1)` 蹇呴』 <= `bgfx::getCaps()->limits.maxTextureSize`锛沜luster 瀹藉害 33脳tileCountX 涓嶅彉銆?- **CPU fallback**锛氬拰 texture 涓€鏍峰彈 width 闄愬埗锛涘鏋滆秴闄愶紝effective capacity 閫愭。闄嶄綆銆?- `effectiveCapacity` 閫夋嫨椤哄簭锛氳姹?128 鈫?64 鈫?32锛沚uffer 鍙敤鏃朵笉闄嶄綆銆?- 闄嶄綆鍙戠敓鏃跺彧璀﹀憡涓€娆★紝骞跺湪 diagnostics 涓褰?`light_list_capacity_clamped=true`銆乣light_list_capacity_requested/effective`銆?- 涓嶅厑璁告妸璇锋眰 128 闈欓粯鍙樻垚 128 涓?light 浣嗗彧鍐?32 涓紱鎴柇蹇呴』鏉ヨ嚜鏄庣‘鐨?capacity 瑙勫垯銆?
### 4.6 甯ч『搴?
M8 涓嶆敼鍙?M6/M7 鐨?light-list 甯ч『搴忥細

```text
ClearLightLists
GPU Light Screen AABB
GPU Big Tile Light List
GPU FPTL Light List
GPU Light Cluster
GBuffer / Forward / Contact / Fog / Debug consumers
```

- `light_screen_aabb_buffer` 鐨?slot 寰幆瑕嗙洊 128锛?- cluster 浠嶈緭鍑?32/cell锛?- indirect dispatch 鐨?group 鏁颁笉鍥?light capacity 鏀瑰彉锛屽彧鏀瑰彉姣忎釜 group 鍐呯殑 slot 鏁般€?
### 4.7 涓?shadow/contact/fog 鐨勮竟鐣?
- 1 个 directional shadow 不变；shadow request 上限仍为 64。
- contact shadow mask 实际为 **8-bit**；最多 8 个 contact shadow light，超过部分不分配 contact mask（保持 M5 语义）。
- 体积雾 punctual light 上限仍由 fog settings（默认 8）控制。
- shadowedCount/阴影索引和 light capacity 解耦；不因 capacity=128 自动创建 128 个 shadow request。
- M7 GPU shadow culling/indirect 仍是 opt-in，M8 不修改其数据布局。
---

## 5. 瀛愪换鍔¤缁嗚璁?
### M8.0 瀹归噺濂戠害涓庡熀绾块攣

鐩爣锛氬厛鍥哄畾鏁版嵁濂戠害鍜?32-light 鍥炲綊锛屼笉鏀硅瑙夈€?
- 鏂板 `PipelineSettings::lightListCapacity`锛?2/64/128锛夛紝`sanitize` 鍒板悎娉曟。浣嶃€?- 鏂板 CLI `--light-capacity=32|64|128`锛汮SON key `lightListCapacity`锛汥ebugUI 鏄剧ず requested/effective銆?- `kMaxGpuLights=128`銆乣kLightListMaxLights=128`銆乣kLightListStrideMax=129`锛涘垹闄ゅ浐瀹?`kLightListStride=33` 鐨?C++ 缂栬瘧鏈熺敤娉曪紝`LightListBuilder` 浣跨敤杩愯鏃?`capacity+1`銆侴PU shader 鐨?33/32 鍥哄畾鍊煎湪 M8.2 鎵嶆浛鎹紱鍦ㄦ涔嬪墠 GPU 璺緞鎶婅姹傚閲?clamp 鍒?32 骞跺彧璀﹀憡涓€娆°€?- 鍏堥攣瀹?M7 32-light 鍩虹嚎锛歚m61_thirtytwo`銆乣m61_transparent`銆乫og銆乧ontact銆乨eferred銆乫orward锛屽湪 capacity=32銆乥uffer/texture/CPU 涓嬩笌 M7 鎴浘 **0 diff**銆?- 鍗曟祴锛歝apacity 鍚堟硶鍊笺€侀潪娉曞€煎洖閫€銆乪ffective capacity銆?2 baseline 涓嶈蛋 128 鍒嗘敮銆?
楠屾敹锛?2-light 鍏ㄩ儴鏃㈡湁娴嬭瘯/鎴浘涓嶅彉锛涙柊璁剧疆鍙В鏋?搴忓垪鍖栵紱鏃?shader 缂栬瘧閿欒銆?
M8.0 验证：ctest **207 cases / 20,170 assertions** + IDL check 通过；D3D11/Vulkan 默认 buffer、texture 和 --no-gpu-light-list CPU 路径对 M7 default_*.png **0 diff**；M8.2 后 --light-capacity=64/128 不再 clamp，见第 12 节。
### M8.1 CPU reference

鐩爣锛欳PU `LightListBuilder` 鍜?`LightBuilder` 鏀寔 32/64/128锛宑luster 浠?32/cell銆?
- `LightBuilder::build(..., capacity)`锛氭寜 importance 鎺掑簭鍚庢埅鏂埌 `capacity`锛涜褰?`truncatedLightCount`锛堝€欓€夋暟 - 閫変腑鏁帮級銆?- `LightListBuilder::build(..., capacity)`锛?  - `lightRects/lightVisible` 鎵╁埌 128锛?  - FPTL/big-tile 鐨?list 涓婇檺鍜?stride 浣跨敤 effective capacity锛?  - cluster 浠嶆埅鏂?32锛屽苟璁板綍 `overflowClusters`锛?  - `textureWidthFor(width, capacity)`銆乣bigTileTextureWidthFor(width, capacity)`銆乧luster width 淇濇寔 33锛?  - `maxLightsPerTile`/`maxLightsPerBigTile`/`maxLightsPerCluster` 缁熻鍑嗙‘銆?- 鍗曟祴瑕嗙洊锛?  - 31/32/33銆?3/64/65銆?27/128/129 鐏紱
  - 鏈?鏃?directional锛?  - 64/128 鐏悓鏃惰惤鍦ㄥ悓涓€ tile銆乥ig tile銆乧luster锛?  - cluster 鍙湁鍓?32 涓崌搴?index锛?  - 鎴柇椤哄簭涓?importance 鎺掑簭涓€鑷达紱
  - texture width 璁＄畻銆?- 涓?M7 32-light CPU reference 閫?tile/閫?cluster 瀵圭収 0 diff銆?
楠屾敹锛欳PU 娴嬭瘯鍏ㄨ繃锛?28 capacity 涓嬫棤瓒婄晫銆?
M8.1 楠岃瘉锛歚LightBuilder` 鍦?capacity=32 鎴柇 33 涓€欓€変负 32/truncated=1锛宑apacity=64 淇濈暀 33锛沗LightListBuilder` capacity=64/128 鐨?FPTL stride 涓?65/129銆?28 鐏悓 tile 璁℃暟涓?128銆乧luster 浠?32/cell 涓旇褰?overflow锛?2-light 榛樿璺緞涓?M7 0 diff銆?
### M8.2 GPU big-tile/FPTL

鐩爣锛欸PU producer 鏀寔 128 slot 鍜屽姩鎬?stride銆侻8.0/M8.1 宸插畬鎴?CPU/setting锛汳8.2 鍚屾椂绉婚櫎 `ForwardRenderer::setSettings` 鐨?32 瀹归噺涓存椂 gate 鍜?shader 鐨勫浐瀹?33/32銆?
- `light_screen_aabb_buffer.cs.sc`锛?  - 64 threads锛屾瘡涓?thread 璐熻矗 2 涓粷瀵?slot锛?..127锛夛紱
  - 瓒呭嚭 effective capacity 鐨?slot 鍐?`visible=0`锛?  - 淇濇寔 AABB `min/max` 甯冨眬涓嶅彉銆?- `light_big_tile_buffer.cs.sc` / `light_fptl_buffer.cs.sc`锛?  - 浣跨敤 `uint s_mask[4]`锛?28 bit锛夛紱
  - shared list `uint s_list[128]`锛?  - 杈撳叆 big-tile count/pass uniform 浣跨敤 effective capacity锛?  - `atomicOr` 鐢?`mask[index >> 5]`锛?  - thread 0 鎸?0..capacity-1 鎵弿 mask 鐢熸垚鍗囧簭 list锛?  - 64 threads 寰幆鍐欏嚭 `row[1..capacity]`锛堜笉鑳藉亣璁?thread 鏁?= capacity锛夛紱
  - 杈撳嚭 stride 浠?`lightListParams2.x` 璇诲彇锛屼笉鍐欐 33銆?- `light_indirect_args.cs.sc`锛歞ispatch group 鏁颁笉鍙橈紝涓嶄緷璧?capacity銆?- buffer 澶у皬/鏍￠獙浣跨敤 `capacity+1`锛屽苟妫€鏌?`bufferCount == tileCountX*tileCountY*stride`銆?- D3D11/Vulkan銆乧apacity 32/64/128銆乥uffer vs CPU reference **0 diff**銆?- 32-light M7 鍥炲綊 0 diff銆?
楠屾敹锛?4/128 鐨?tile/big-tile light 闆嗗悎鍜岄『搴忎笌 CPU 涓€鑷达紱鏃?shared memory/瓒婄晫/validation 閿欒銆?
### M8.3 GPU cluster

鐩爣锛氬湪鎬诲閲?64/128 涓嬩繚鎸?cluster 32/cell锛屽苟淇濊瘉杈撳叆/overflow 姝ｇ‘銆?
- `light_cluster_buffer.cs.sc`锛?  - 杈撳叆 big-tile list 鍙惈 64/128 涓?light锛屾寜 FPTL/big-tile 鐨勫姩鎬?`listStride` 璇诲彇锛涜緭鍏ュ惊鐜笉鑳界户缁敤 `HBG_LIGHT_CLUSTER_MAX_LIGHTS` 鎴柇 big-tile锛屽彧鑳界敤瀹冮檺鍒惰緭鍑猴紱
  - 杈撳嚭 shared list 浠?32锛?  - 姣忎釜鍊欓€夋鏌?`lightIndex < effective capacity`銆丄ABB銆乨epth slice锛?  - 瓒呰繃 32 鐨勫€欓€夎鍏?overflow锛坰hared/atomic 鎴?CPU 鍙缁熻锛夛紝涓嶅啓瓒婄晫锛?  - 杈撳嚭 count 鍜?32 涓崌搴?index銆?- cluster stride/绾圭悊甯冨眬淇濇寔 33锛?- `light_cluster_buffer` 鐨?debug slice 浠嶄笌 CPU 鍚?slice 0 diff锛?- 64/128 lights 涓嬶細
  - cluster 鍓?32 涓崌搴?index 涓?CPU 涓€鑷达紱
  - 绗?33 涓強浠ュ悗涓嶅嚭鐜帮紱
  - transparent/forward-only cluster 璺緞浠嶅伐浣滐紱
  - `--no-light-cluster` 鍥為€€ FPTL 鑳界湅鍒板畬鏁?64/128銆?- 涓嶅湪 M8 寮曞叆 layered offset锛涘鏋滃疄娴?fixed row 32/cell 涓嶅彲鎺ュ彈锛孧8.3 鍙褰曟暟鎹紝layered 杞叆 M9锛屼笉浣滀负 M8 闃诲椤广€?
楠屾敹锛歝luster 32/cell 杈圭晫銆?4/128 杈撳叆銆乨ebug slice 鍏ㄩ儴 0 diff/鏃犺秺鐣屻€?
### M8.4 Buffer/texture consumers

鐩爣锛氭墍鏈夋秷璐硅€呬笉鍐嶇‖缂栫爜 32锛屽苟淇濇寔 M7 缁戝畾璺緞銆?
- `shaders/common/light_data.sc`锛?  - `HBG_LIGHT_LIST_STRIDE` 鏀逛负 `u_pass_lightListParams2.x`锛坆uffer/texture 閮界敤杩愯鏃?stride锛夛紱
  - `HBG_LIGHT_LIST_MAX_LIGHTS` 涓?128锛?  - cluster 瀹忎繚鎸?`33/32`锛?  - `hbgLightListCount/Index` 鐢?`tileCountX * stride`锛?  - texture path 鐨?`texelFetch` x 鍧愭爣鐢ㄨ繍琛屾椂 stride锛?  - 鎵€鏈?index 璇诲彇鍔?`index < capacity` 闃叉姢銆?- `shaders/lighting/light_loop.sc`锛?  - FPTL `lightCount`/tile count clamp 鍒?128锛?  - cluster clamp 浠?32锛?  - fallback loop 鏈€澶?128锛?  - 涓嶆敼鍙?directional 鍗曠嫭寰幆鍜?punctual 姹傚€奸『搴忋€?- forward/deferred/contact/fog/debug锛?  - 缁戝畾 `s_lightList` 鐨?stage 7 涓嶅彉锛?  - `bindLightList` 鐨?buffer/texture 鏉′欢涓嶅彉锛?  - debug light-cluster 缁х画璇?cluster 33 stride锛?  - fog punctual 浠嶇敱 fog settings 闄愬埗锛屼笉鍥?128 鎵╁銆?- `debug` 杈撳嚭 capacity/stride/cluster cap锛屼究浜庡畾浣?tile/list 閿欒銆?
楠屾敹锛歝apacity 32/64/128銆乥uffer/texture銆乫orward/deferred/transparent/fog/contact/debug 鍚屽悗绔?0 diff锛汳7 32 鍩虹嚎 0 diff銆?
### M8.5 闆嗘垚銆佸洖閫€銆丆LI銆乁I

鐩爣锛氬閲忛€夋嫨鍙厤缃€佸彲璇婃柇銆佸彲鍥為€€銆?
- CLI/JSON/UI锛?  - `--light-capacity=32|64|128`锛?  - `config/pipeline.json` 澧炲姞 `lightListCapacity`锛堥粯璁?32锛孧8.6 鍐嶅喅瀹氭槸鍚﹀垏 64锛夛紱
  - DebugUI 鏄剧ず requested/effective capacity銆乻tride銆乸ath銆乼runcated/overflow銆?- effective capacity锛?  - buffer path 鐩存帴浣跨敤璇锋眰鍊硷紱
  - texture/CPU path 妫€鏌?`(capacity+1)*tileCountX <= maxTextureSize`锛屼笉婊¤冻鍒欓€愭。闄嶄綆锛?  - 鍙戠敓 clamp 鍙鍛婁竴娆★紝璁板綍 requested/effective锛?  - 涓嶅洜 capacity 闄嶄綆鑰屾敼鍙?32-light 鍩虹嚎銆?- 璧勬簮锛?  - light data texture 楂樺害闅?effective capacity锛?  - screen bounds 鍥哄畾 2*128锛?  - FPTL/big-tile buffer/texture 闅?stride 閲嶅缓锛?  - cluster 璧勬簮涓嶅彉锛?  - resize/RT scale/4K 鍚庨噸寤哄苟鏍￠獙锛屾棤娉勬紡銆?- diagnostics锛?  - `light_list_capacity_requested/effective`锛?  - `light_list_stride`锛?  - `light_truncated`锛?  - `cluster_overflow`锛?  - 淇濈暀 M7 鐨?`light_list_passes/cpu_ms/path`銆?- 4K/1440p锛?  - capacity 64/32 鍦?4K texture 瀹藉害鍐咃紙鎸夊疄闄?maxTextureSize 鏍￠獙锛夛紱
  - capacity 128 鍦?1080p texture 鍙繍琛岋紝1440p 浠ヤ笂搴旇蛋 buffer 鎴?clamp锛?  - buffer path 浣滀负 128 鐨勪富璺緞銆?- 澶辫触鍥為€€锛歜uffer 涓嶅彲鐢?鈫?texture锛泃exture 瀹藉害涓嶈冻 鈫?闄?capacity锛涘啀澶辫触 鈫?CPU reference锛涙瘡涓€姝ュ彧璀﹀憡涓€娆°€?
楠屾敹锛欳LI/JSON/UI/璇婃柇姝ｇ‘锛況esize/0.5/4K 鏃犺秺鐣?娉勬紡锛?2 鍥炲綊涓嶅彉銆?
### M8.6 鎬ц兘涓庨粯璁ゅ喅绛?
鐩爣锛氱粰鍑?32/64/128 鐨勭湡瀹炴垚鏈拰榛樿鍊笺€?
- 1080p 300 甯?full post锛堟部鐢?M7.7 鍦烘櫙/璁剧疆锛夛細
  - capacity 32 baseline锛?  - 64 鐩爣 >60 fps锛屼笖涓嶆樉钁楁參浜?32锛?  - 128 璁板綍 fps/鍐呭瓨锛屼笉瑕佹眰榛樿杈炬爣銆?- 璁板綍锛?  - FPTL/big-tile/cluster buffer 瀛楄妭鏁帮紱
  - `light_list_stride`銆乵ax lights銆乼runcated/overflow锛?  - GPU/CPU frame銆乻hadow/contact/fog 鏄惁鍙楀奖鍝嶃€?- 榛樿鍐崇瓥锛?  - 64 閫氳繃鍙屽悗绔?0 diff銆?2 鍥炲綊銆?080p >60銆佹棤娉勬紡鍚庢墠鍙粯璁わ紱
  - 128 涓嶄綔涓洪粯璁ゅ閲忥紝淇濇寔 opt-in/experimental锛?  - 32 濮嬬粓鏄畨鍏ㄥ洖閫€銆?- 濡傛灉 64 鍦ㄧ洰鏍?GPU 涓婃槑鏄鹃€€鍖栵紝淇濈暀 32 榛樿骞惰褰?64 opt-in锛涗笉涓轰簡鈥滃閲忊€濈壓鐗?60 fps 闂ㄧ銆?
楠屾敹锛氭€ц兘鏃ュ織銆佸唴瀛樿〃銆侀粯璁ゅ喅绛栧啓鍏ユ枃妗ｃ€?
### M8.7 M8 楠屾敹

- 瑙嗚锛?2 M7 鍩虹嚎 0 diff锛?4/128 CPU vs GPU vs texture/buffer 0 diff锛沜luster 32/cell 杈圭晫姝ｇ‘銆?- 鏁板€硷細max lights/stride/count/overflow 缁熻姝ｇ‘锛涙棤瓒婄晫/绌?list銆?- 鍚庣锛欴3D11/Vulkan 鍙屽悗绔€?- 鍦烘櫙锛歠orward/deferred/transparent/fog/contact/debug/0.5/4K銆?- 鎬ц兘锛?080p 300 甯?64 >60锛?28 璁板綍銆?- 闂ㄧ锛歚hbg_tests`銆乣ctest`銆乣hbg_idl_check`銆乣--gfx-test`銆乣git diff --check`銆佹枃妗ｆ洿鏂般€?- 鍥為€€锛歜uffer 鈫?texture 鈫?CPU锛泃exture 瀹藉害 鈫?capacity clamp锛涜祫婧愰噴鏀炬棤娉勬紡銆?
---

## 6. 楠屾敹鐭╅樀

| 鐢ㄤ緥 | 鐩爣 |
|---|---|
| 32-light M7 鍥炲綊 | m61_thirtytwo/transparent/fog/contact/deferred/forward 鍦?CPU/texture/buffer 涓?0 diff |
| 64-light CPU vs GPU | tile/big-tile 闆嗗悎/椤哄簭 0 diff锛沜luster 鍓?32 0 diff锛汥3D11/Vulkan |
| 128-light buffer | 1080p 0 diff锛泃ile/big-tile 瀹屾暣锛沜luster 32/cell |
| 128-light texture | 1080p 瀹藉害鍙绾虫椂 0 diff锛涜秴闄愭椂 clamp/buffer |
| 64-light texture | 4K 瀹藉害鍙绾虫椂 0 diff锛涘惁鍒?clamp |
| capacity 杈圭晫 | 31/32/33銆?3/64/65銆?27/128/129锛涙埅鏂『搴忔纭?|
| cluster 杈圭晫 | 32/33 涓噸鍙?light锛涚 33 涓笉杩?cluster锛涙棤瓒婄晫 |
| transparent/forward-only | cluster 32/cell锛沗--no-light-cluster` 鍥為€€ FPTL 64/128 |
| fog/contact | fog 8-light 涓婇檺銆乧ontact 32-bit mask 涓嶅彉锛涗笉鍑虹幇绌?list/閿欒 shadow |
| buffer/texture/CPU | 鍚屽悗绔?0 diff锛涘け璐ラ€愬眰鍥為€€鍙鍛婁竴娆?|
| resize/0.5/4K | list/buffer/texture 閲嶅缓銆乻tride/灏哄姝ｇ‘銆佹棤娉勬紡 |
| 鎬ц兘 | 1080p 300 甯?64 >60锛?28 璁板綍锛?2 baseline |
| 闂ㄧ | 鍗曟祴/ctest/IDL/gfx/docs/diff 鍏ㄩ€氳繃 |

---

## 7. 鎬ц兘涓庡唴瀛橀绠楋紙浼扮畻锛?
1080p 涓嬶紙`tileCountX=120`銆乣bigTileCountX=30`銆乧luster tiles `30脳17`锛?2 slices锛夛細

| capacity | FPTL stride | FPTL bytes | big-tile bytes | cluster bytes锛?3锛?| 鍚堣锛堜笉鍚?light data锛?|
|---:|---:|---:|---:|---:|---:|
| 32 | 33 | 120脳68脳33脳4 鈮?1.08 MB | 30脳17脳33脳4 鈮?67 KB | 30脳17脳32脳33脳4 鈮?2.15 MB | 鈮?3.30 MB |
| 64 | 65 | 鈮?2.12 MB | 鈮?133 KB | 2.15 MB | 鈮?4.40 MB |
| 128 | 129 | 鈮?4.21 MB | 鈮?263 KB | 2.15 MB | 鈮?6.63 MB |

- light data texture锛?28脳6脳16 鈮?12 KB锛泂creen bounds 鍥哄畾 128脳2脳16 = 4 KB銆?- texture path 瀹藉害锛?080p FPTL 涓?3960/7800/15480锛?28 宸叉帴杩?16384 涓婇檺锛?440p 128 浼氳秴杩囬檺鍒躲€?- buffer path 鏃?texture 瀹藉害闄愬埗锛屼絾 128 鐨?FPTL 甯﹀/寰幆鎴愭湰闇€瑕佸疄娴嬨€?- 鍐呭瓨棰勭畻涓嶆槸榛樿寮€鍚?128 鐨勭悊鐢憋紱M8.6 鎸夊疄闄?GPU 缁撴灉鍐冲畾銆?
---

## 8. 椋庨櫓涓庡鐞?
1. **shared mask/shared list 鎵╁**锛?28 bit 闇€瑕?4 涓?`uint`锛宼hread 0 鎵弿 128锛泂hared list 128銆傚厛鍋?64锛屽啀寮€ 128锛涙瘡姝ュ鐓?CPU銆?2. **64 threads 鍐欏嚭 128 indices**锛氫笉鑳芥部鐢?`if (thread < MAX)`锛涘繀椤荤敤 `for (i=thread; i<capacity; i+=64)`锛屽惁鍒?64..127 涓㈠け銆?3. **texture 瀹藉害闄愬埗**锛?28 鍦?1440p/4K 瓒?`maxTextureSize`锛涘繀椤?query caps锛屾寜 128鈫?4鈫?2 clamp 鎴栬蛋 buffer锛屼笉闈欓粯瓒婄晫銆?4. **cluster 32/cell 涓庢€诲閲忎笉涓€鑷?*锛氳繖鏄湁鎰忚竟鐣岋紝闇€ debug/overflow 缁熻鍜屾枃妗ｏ紱涓嶈兘鐢ㄢ€渃luster 32鈥濊鍒や负 128 鍏ㄤ涪銆?5. **contact/fog/shadow 涓婇檺鏈墿瀹?*锛歝ontact mask 浠?32 bit锛宖og 浠?8锛宻hadow requests 浠?64锛涗笉瑕佸０绉?128 鐏叏閮ㄦ湁 contact/shadow/fog銆?6. **FPTL 涓?cluster 瑙嗚宸紓**锛歰paque FPTL 64/128銆乼ransparent cluster 32锛涢獙鏀跺繀椤诲湪鍚岃矾寰勫鐓э紝`--no-light-cluster` 鍗曠嫭璁板綍銆?7. **M7 0 diff 鍥炲綊**锛氭敼 stride 鏃跺厛閿?32 baseline锛汳8.0 蹇呴』鍏堟埅 32 鍥撅紝鍐嶆敼 producer/consumer銆?8. **shader 鍙樹綋/瀹?*锛歚HBG_LIGHT_LIST_STRIDE` 涓嶈兘鍐嶆槸鍥哄畾 33锛涙墍鏈?include 璺緞缁熶竴浠?uniform 璇伙紝閬垮厤 texture/buffer 鍒嗘敮涓嶄竴鑷淬€?9. **IDL 瀵归綈**锛歚PassUniforms`/`ClusterUniforms` 鏂板 vec4 浼氬奖鍝?offset锛涙洿鏂?`test_uniform_layout.cpp`锛屽弻鍚庣 gfx 楠岃瘉銆?10. **scope 鑶ㄨ儉**锛歁8 涓嶉『鎵嬪仛 layered/16-bit/category/layer/cookie/shadow atlas锛涘嚭鐜伴渶姹傛柊寤?M9銆?
---

## 9. 寰呯‘璁?瀹炵幇鍓嶆鏌?
- [ ] `maxTextureSize` 鍦?D3D11/Vulkan 瀹為檯鍊硷紱128 鍦?1080p 鏄惁绋冲畾灏忎簬涓婇檺銆?- [ ] 128-slot AABB 鐢?64 threads 杩樻槸 128 threads锛汥3D11/Vulkan shared/atomic 鎴愭湰銆?- [ ] `uint[4]` mask 鍦?GLSL/HLSL/SPIR-V 鐨?shared memory 澹版槑鍜?`atomicOr` 鍏煎鎬с€?- [ ] FPTL/big-tile 鐨?`lightListParams2` 鏀?Pass/Cluster 涓ゅ uniform 鏄惁闇€瑕侀噸澶嶄笂浼犮€?- [ ] texture path 128 鐨勫搴︽槸鍚﹀湪鐩爣 backend 涓婄ǔ瀹氾紙1080p 15480锛夈€?- [ ] 64 threads 鍐?128 index 鐨勫惊鐜湪 FPTL/big/cluster 涓変釜 shader 涓竴鑷淬€?- [ ] `LightBuilder` 鐨?importance 鎴柇鏄惁闇€瑕佸湪 diagnostics 涓樉绀猴紱鏄惁淇濇寔鐜版湁鎺掑簭绋冲畾銆?- [ ] 64 榛樿瀹归噺鏄惁婊¤冻 1080p >60锛涘鏋滃惁锛屾槸鍚﹀彧淇濈暀 64 opt-in銆?- [ ] 128 cluster 32/cell 鏄惁瓒冲锛沴ayered 鏄惁纭畾鐣?M9銆?- [ ] shadow/contact/fog 涓婇檺鏄惁闇€瑕佸湪 UI 涓彁绀猴紝閬垮厤璇В 128 鍏ㄥ姛鑳姐€?
---

## 10. M8 鏄庣‘涓嶅仛

- HDRP 16-bit packed FPTL/big-tile list锛汳8 缁х画 32-bit row銆?- Layered cluster offset銆乬lobal atomic append銆乸er-category offset buffer銆?- Light category锛圓rea/Env/Decal锛夈€乫eature flags銆乸er-material lighting variants銆?- Light layers/rendering layer masks銆丟Buffer rendering-layer channel銆?- Cookies/IES銆乤rea lights銆乵ulti-directional >1銆?- GPU shadow request/atlas packing銆乧ached shadow GPU 鏇存柊锛汳7 shadow 鎵╁睍浠嶆槸 opt-in銆?- 256/512 lights銆丏3D12銆丏XR/RT銆乥indless/descriptor indexing銆丠i-Z銆乵eshlet/mesh shader銆?- 鏀瑰彉 M7 榛樿鐨?GPU shadow culling/indirect/async 寮€鍏炽€?
---

## 11. 鏂囨。涓庝唬鐮佽竟鐣?
鏈枃浠跺彧鍋?M8 瑙勫垝锛屼笉鍖呭惈鍔熻兘瀹炵幇銆傝繘鍏ュ疄鐜板悗锛?
- 姣忎釜瀛愪换鍔″畬鎴愬悗鏇存柊鏈枃浠剁姸鎬併€佸疄闄?`file:line`銆佹祴璇?鎴浘/鎬ц兘锛?- 鏇存柊 `docs/implementation-plan.md` 鐨?M8 鐘舵€佸拰涓嬩竴浠诲姟锛?- 鏇存柊 `AGENTS.md` 鐨勫閲?list stride/cluster 32 杈圭晫锛?- 涓存椂鍦烘櫙/鎴浘/鏃ュ織缁х画鏀惧湪 `C:\Users\chenfuling\AppData\Local\Temp\`锛屼笉鎻愪氦鍒颁粨搴擄紱
- 鏈畬鎴愭垨闃诲鐨勮兘鍔涙槑纭啓鈥滄湭瀹炵幇/闃诲鈥濓紝涓嶆妸瑙勫垝鍐欐垚宸插畬鎴愩€?
M8 鍑哄彛锛?2/64/128 瀹归噺濂戠害绋冲畾锛?2 M7 鍩虹嚎 0 diff锛?4 鍙屽悗绔?0 diff 涓?1080p >60锛?28 鏈夊彲杩愯鐨?buffer 璺緞鍜岀湡瀹炴€ц兘璁板綍锛泃exture/CPU 鍥為€€銆乺esize/4K銆佽瘖鏂€佹祴璇曞拰鏂囨。榻愬叏銆?

---

## 12. M8.2 实现记录（2026-11-13）

### 12.1 代码
- `light_screen_aabb_buffer.cs.sc` / `light_screen_aabb.cs.sc`：64 threads × 2 slots，覆盖 0..127；超出 effective capacity 的 slot 写零。
- `light_big_tile_buffer.cs.sc` / `light_big_tile.cs.sc`、`light_fptl_buffer.cs.sc` / `light_fptl.cs.sc`：`uint mask[4]`、`shared list[128]`，capacity/stride 从 `u_pass_lightListParams2` 读取；64 threads 循环写 `capacity` 个 index。
- `light_cluster_buffer.cs.sc` / `light_cluster.cs.sc`：big-tile 输入按 runtime stride 读取，候选 mask 扩到 128；输出保持 33 stride、32/cell，按升序取前 32。
- `shaders/common/light_data.sc`：FPTL/big-tile count/index 使用 `u_pass_lightListParams2.x` 运行时 stride；新增 `hbgLightListCapacity()`。
- `shaders/lighting/light_loop.sc`：light count/tile count/fallback 上限改为 capacity（128）。
- `ForwardRenderer::setSettings()`：移除 32 容量 gate；`ensureLightDataTexture()` 在 capacity 变化时重建 light-data texture；buffer 日志输出 capacity/stride。
- texture producer 同步支持 32/64/128；1280×720 / 1920×1080 的 128-light texture 宽度 10320/15480，未超 16384。

### 12.2 验证
- 构建：`cmake --build build/vs2022 --config Debug --parallel 8` 通过；shader 全量编译通过。
- `ctest --preset vs2022-debug --output-on-failure`：2/2 通过（207 cases / 20,170 assertions + `hbg_idl_check`）。
- `--gfx-test`：D3D11、Vulkan 通过。
- 32-light M7 基线：stress scene，D3D11/Vulkan buffer/texture 对 `hbg_m78\default_*.png`、`default_tex_*.png` 均 **0 diff**；`--no-gpu-light-list` CPU 与 GPU 也 **0 diff**。
- 64/128：自建 64/128 punctual 场景（32 个位置重复，保证同 tile 压力；单 tile/big-tile 最大 64/128）。
  - buffer/texture、cluster/no-cluster、D3D11/Vulkan、forward/deferred/transparent、GPU vs CPU 全部 **0 diff**。
  - 128-light forward buffer max lights tile=128、big tile=128；CPU cluster `overflow clusters=1440`，GPU 输出与 CPU **0 diff**。
  - 128-light indirect buffer 路径（D3D11）与 CPU **0 diff**。
  - `--render-scale=0.5` 128-light buffer 与 CPU **0 diff**。
  - 1920×1080 128-light texture 路径与 CPU **0 diff**；2560×1440 128-light texture 超宽时按既有行为回退 CPU，与 CPU **0 diff**。
  - directional + punctual 边界：1 个 directional + 127 punctual（capacity 128，max tile=127）与 1 + 63（capacity 64，max tile=63）GPU vs CPU **0 diff**。
- 临时产物：`C:\Users\chenfuling\AppData\Local\Temp\hbg_m8_2\*`。

### 12.3 剩余
- M8.4：texture 路径 `maxTextureSize` clamp/诊断、contact/fog 既有 32 边界收尾、其余 debug/consumer 审计。
- M8.5：CLI/UI 的 requested/effective/clamped 诊断与 resize/4K 矩阵。
- M8.6：32/64/128 1080p 性能与默认容量决策。

### 13. M8.3 实现记录（2026-11-13）

#### 13.1 实现
- `light_cluster_buffer.cs.sc` / `light_cluster.cs.sc`：
  - big-tile 输入按 `u_pass_lightListParams2` 的 runtime stride 读取，候选上限为 effective capacity（32/64/128）。
  - mask 扩到 4×32-bit；输出 shared list 保持 32。
  - thread 0 升序扫描 mask，前 32 个写入 list；第 33 个起累加 `s_clusterOverflow`，不写越界。
  - count texel 打包为 `count | (overflow << 8)`，低 8 bit 仍是 count，高 8 bit 是溢出候选数（最多 96）。
- `shaders/common/light_data.sc`：
  - 新增 `hbgClusterLightListRawCount` / `hbgClusterLightListOverflow`；`hbgClusterLightListCount` 只取低 8 bit。
  - forward/deferred/transparent/fog 等消费者通过 helper 继续读取 32/cell count，M7 语义不变。
- `debug_light_cluster.fs.sc`：
  - `--debug-view=light-cluster` 的 overflow cell 显示为品红；绿色通道按 overflow 数量增强（1/8..1），便于区分 1 个和多个溢出。
- `LightListBuilder`：
  - CPU reference 同样按 cluster 记录 overflow count 并打包到 count texel；新增 `clusterOverflowCount()` 供测试/诊断。
  - `overflowClusters` 仍统计“有溢出的 cluster 数”。
- `tests/render/test_light_list.cpp`：
  - 新增 33 个同位置 light、capacity=64 的边界用例：count=32、overflow=1、index 升序 0..31；capacity=32 时不产生 overflow。
- HDRP 参考：`lightlistbuild-clustered.compute:453` 用 `min(iSum, MAX_NR_COARSE_ENTRIES)` 限制 cluster 写入、超限不越界；本实现保留同样的 32/cell 上限，并把超出数量额外写入 count 高 8 bit 作为诊断。

#### 13.2 验证
- shader 全量编译 + Debug 构建通过；`ctest` **208 cases / 20,209 assertions** + `hbg_idl_check` 通过；D3D11/Vulkan `--gfx-test` 通过。
- 32-light M7 基线：D3D11/Vulkan buffer/texture 对 `default_*.png` / `default_tex_*.png` 仍为 **0 diff**。
- 33-light 同位置边界（capacity=64）：CPU `max lights cluster=32, overflow clusters=1440`；GPU/CPU buffer、texture、D3D11、Vulkan `--debug-view=light-cluster --debug-light-cluster-slice=2` 均 **0 diff**；overflow 像素颜色 `(255,32,115)` 对应 overflow=1，证明第 33 个 light 计入 overflow 且前 32 个升序输出。
- 64/128：normal forward/deferred/transparent、buffer/texture、cluster/no-cluster、D3D11/Vulkan GPU vs CPU 全部 **0 diff**；debug light-cluster slice 0/2 的 buffer/texture 与 CPU **0 diff**。
- 128-light max lights tile=128、big tile=128；`--no-light-cluster` 仍能读取完整 128；`--debug-light-cluster-slice=0` 与 CPU 同 slice **0 diff**。
- 临时产物：`C:\Users\chenfuling\AppData\Local\Temp\hbg_m8_2\m83_*`、`m8_lights33_same.gltf`。

#### 13.3 剩余
- M8.4：texture `maxTextureSize` clamp/诊断，contact/fog 既有 32 边界收尾，其余 consumer 审计。
- M8.5：GPU overflow 数值 readback/CLI/UI 诊断（当前可经 debug view 观察，CPU reference 有 `clusterOverflowCount`）。
- M8.6：32/64/128 性能与默认容量决策。

### 14. M8.4 实现记录（2026-11-14）

#### 14.1 实现
- `LightListBuilder::textureFits()` / `textureCapacityFor()`：按 backend `maxTextureSize` 和 bgfx `uint16_t` pitch 的 16383 宽度上限，同时校验 FPTL、big-tile、cluster 三张 list 纹理；请求 128→64→32 逐档选择，0 表示没有可用档位。
- `ForwardRenderer`：新增 `m_lightListTextureCapacity` / `m_lightListEffectiveCapacity`；texture/CPU 路径使用 clamped capacity，buffer 路径保持 requested capacity；clamp 只在档位变化时告警一次，并输出 requested/effective/clamped 诊断。
- Consumers：`volumetric_fog.fs.sc` 的 `lightCount` 从硬编码 32 改为 `hbgLightListCapacity()`；`contact_shadows.cs.sc` 的 `lightCount` / `tileLightCount` 同样改为 capacity（contact mask 仍是 8 bit，最多 8 盏灯，不随 capacity 扩展）；`debug_light_data.fs.sc` 只显示 effective capacity 内的灯。
- 文档修正：contact shadow mask 实际为 8 bit（最多 8 盏灯，含方向光），旧 checklist 的“32-bit mask”描述不再适用；fog punctual 上限仍由 fog settings 决定（默认 8）。
- HDRP 参考：`LightLoop.hlsl` / `lightlistbuild-clustered.compute` 使用运行时 list count 和 `MAX_NR_COARSE_ENTRIES` 上限；本实现同样先取 effective capacity，再做 list/cluster 读取，不在 consumer 端写死 32。HDRP 没有 texture light-list 路径，`maxTextureSize` clamp 是本工程 texture 后端约束。

#### 14.2 验证
- shader 全量编译 + Debug 构建通过；`ctest` **209 cases / 20,225 assertions** + `hbg_idl_check` 通过；D3D11/Vulkan `--gfx-test` 通过。
- 32-light M7 buffer/texture/CPU 基线 D3D11/Vulkan **0 diff**。
- 64/128 normal/fog/contact：buffer/texture、forward、D3D11/Vulkan GPU vs CPU 全部 **0 diff**。
- 1440p texture 128（D3D11 maxTextureSize=16384）自动 clamp 到 64，GPU/CPU **0 diff**；4K texture 128 在 D3D11/Vulkan 都因 16383 pitch 上限 clamp 到 64，GPU/CPU **0 diff**；4K texture 64/32 **0 diff**；4K buffer 128 保持 `capacity=128/128` 且无 fallback。
- 1440p texture 128 debug light-cluster slice 0 GPU/CPU **0 diff**；4K buffer 128 日志为 `path=buffer, capacity=128/128`。

#### 14.3 剩余
- M8.5：CLI/UI 的 requested/effective/clamped 完整展示、resize/RT scale/4K 矩阵（已覆盖主要路径）、GPU overflow 数值 readback。
- M8.6：32/64/128 1080p 性能与默认容量决策；contact shadow 在 128 下遍历更多候选，性能需实测。


### 15. M8.5 实现记录（2026-11-14）

#### 15.1 实现
- DebugUI：capacity 行显示 requested/effective/stride/truncated/clamped；新增 `Read GPU cluster stats` 按钮，显示 clusters/active/overflow clusters/overflow lights/max overflow/max lights。
- CLI：新增 `--cluster-stats-readback`；render test 自动补足 8 帧等待异步 readback；`render performance` 增加 `light_list_capacity_requested/effective/stride/truncated/clamped` 与 `cluster_stats_*` 字段。
- Buffer 路径：`bgfx::read(BufferRegion)` 直接读回 `lighting.lightClusterBuffer`；Texture 路径：创建 `R32U` + `BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK` staging texture，经诊断 view 4093 从 `lighting.clusterLightList` blit 后 `bgfx::read(TextureRegion)`；按 33-word stride 解析 count/overflow。
- `gfx::Buffer::native()` / `ResourceManager::native(BufferHandle)` 暴露 bgfx handle；readback 资源在 shutdown 释放，pending 时拒绝重复请求。
- clamp 告警改为仅在 texture/CPU 路径实际使用时输出；buffer 路径 4K 不再误报 texture clamp。
- HDRP 参考：`LightLoop.hlsl` / `LightLoopDef.hlsl` 的 runtime cluster count、`lightlistbuild-clustered.compute` 的 `MAX_NR_COARSE_ENTRIES` clamp；HDRP 不暴露 overflow 数值，本实现为诊断扩展。

#### 15.2 验证
- `ctest` **209 cases / 20,225 assertions** + IDL check 通过。
- 33 lights 同位置、capacity 64/128：CPU、D3D11/Vulkan buffer、D3D11/Vulkan texture 的 readback 均为 `clusters=7680 active=1440 overflow_clusters=1440 overflow_lights=1440 max_overflow=1 max_lights=32`（1280x720）。
- 1440p：`clusters=23040 active=4320 overflow_clusters=4320`；4K：`clusters=34560 active=6480 overflow_clusters=6480`；buffer/texture、D3D11/Vulkan 与 CPU reference 一致。
- 4K buffer 128 无 texture clamp 告警且 `effective=128/stride=129`；4K texture 128 告警 clamp 到 64（`effective=64/stride=65`），readback 仍与 CPU 一致。
- 32-light M7 baseline、gfx-test、`git diff --check` 通过。

#### 15.3 剩余
- M8.6：已完成，见 §16（默认容量切换为 64；128 保持 opt-in）。
- M8.7：M8 收尾、性能/内存/IDL/gfx/docs 全量验收。


---

## 16. M8.6 性能与默认决策（2026-11-14）

### 16.1 测试方法

- 构建：RelWithDebInfo；GPU：RTX 2060 SUPER；1920x1080；300 帧；`--no-vsync`。
- 场景：
  - 32-light：`m61_thirtytwo.gltf`（32 个 point light，32 个不同位置）；
  - 64-light：`m8_lights64_perf.gltf`（64 个 light node，32 个位置，每位置 2 个）；
  - 128-light：`m8_lights128.gltf`（128 个 light node，32 个位置，每位置 4 个）。
- 统一设置：`--pipeline-settings=config/pipeline.json --frame-settings=config/frame_settings.json --lit-shader-mode=forward --postprocess-profile=config/postprocess_quality.json`，Uffizi sky，shadows on，contact off（16.4 单独打开）。
- 每个 case 重复 7 次；wall fps 受桌面/DWM present 节流影响，结论以 GPU timer 和 CPU submit 为主，fps 只用于 60 门禁。表中为 min–max（avg）。

### 16.2 1080p 300 帧性能

| case | capacity | D3D11 fps | D3D11 GPU ms | Vulkan fps | Vulkan GPU ms |
|---|---:|---:|---:|---:|---:|
| 32-light | 32 | 67.7–75.9 (72.7) | 6.27–11.44 (8.53) | 88.2–104.2 (100.7) | 8.06–8.78 (8.35) |
| 32-light | 64 | 67.4–81.4 (73.2) | 6.40–10.16 (8.19) | 99.9–105.1 (102.0) | 8.10–10.01 (8.60) |
| 32-light | 128 | 66.8–74.2 (71.0) | 6.80–9.55 (8.16) | 100.1–109.8 (103.7) | 7.46–8.59 (8.15) |
| 64-light | 64 | 60.8–64.9 (63.1) | 12.33–20.51 (15.05) | 75.1–79.7 (77.0) | 10.94–11.91 (11.58) |
| 128-light | 128 | 40.0–43.8 (42.0) | 19.82–22.66 (21.32) | 48.2–51.3 (50.0) | 17.65–18.95 (18.53) |

CPU submit 平均：32-light/32 D3D11 8.94 ms / Vulkan 9.20 ms；64-light/64 15.29 / 12.58 ms；128-light/128 21.39 / 19.95 ms。所有 case light_list_truncated=0；shadow_drawn 分别为 130 / 134 / 138，未出现回退或空 list；fog 在性能矩阵关闭，contact 见 16.4。

结论：

- 32-light 场景下 capacity 32/64/128 的 GPU timer 在噪声范围内，没有“容量越大越慢”的稳定趋势；64 在双后端均 >60 fps。
- 64-light/64 的 D3D11 最低 60.8 fps、平均 63.1 fps；Vulkan 75.1–79.7 fps，满足 1080p >60 门禁，但 D3D11 余量较小。
- 128-light/128 双后端均 <60 fps（D3D11 40–44、Vulkan 48–51），不满足默认门禁；128 只保留 opt-in/experimental。
- 默认 deferred 路径复核（3 次）：64-light/64 D3D11 **63.4–65.2 fps**（GPU 12.79–14.52 ms）、Vulkan **75.1–84.0 fps**（GPU 10.61–11.98 ms），同样 >60。

### 16.3 1080p light-list 内存

| capacity | FPTL bytes | big-tile bytes | cluster bytes | bounds bytes | total buffers | light data |
|---:|---:|---:|---:|---:|---:|---:|
| 32 | 1,077,120 | 67,320 | 2,154,240 | 4,096 | 3,302,776 | 3,072 |
| 64 | 2,121,600 | 132,600 | 2,154,240 | 4,096 | 4,412,536 | 6,144 |
| 128 | 4,210,560 | 263,160 | 2,154,240 | 4,096 | 6,632,056 | 12,288 |

- runtime 日志已增加实际字节数：`light list buffers: capacity=... stride=... bounds=... B, big=... B, fptl=... B, cluster=... B, total=... B`。
- 64 相对 32 增加约 **1.11 MB**（1080p），远小于 texture/cluster 固定成本；1080p texture 宽度 7800，4K texture 宽度 15600，均低于 16383 pitch 上限。
- 4K texture/64 实测 `capacity=64/64`、`clamped=false`，FPTL 宽度 15600；默认 64 在 4K texture/CPU 路径不需要 clamp。

### 16.4 Contact shadows 在 128 capacity 的成本

| case | contact | D3D11 GPU ms | Vulkan GPU ms |
|---|---:|---:|---:|
| 32-light / 32 | off | 8.53 | 8.35 |
| 32-light / 32 | on | 10.40 | 11.31 |
| 128-light / 128 | off | 21.32 | 18.53 |
| 128-light / 128 | on | 25.95 | 23.77 |

- contact mask 仍只有 **8 bit**（`kMaxContactShadowLights=8`），但 shader 需要遍历 tile 的 effective capacity 才能找到有 mask 的 light；因此 128 capacity 下 contact 额外 **4.6–5.2 ms**，32 capacity 下约 **1.9–3.0 ms**。
- 128-light + contact 双后端 36–39 fps，明确低于 60；文档和 UI 不应把 128 宣传为“全功能 128 灯 contact”。
- fog 仍最多 8 个 punctual，shadow requests 仍最多 64；128 不等于 contact/fog/shadow 全部扩容。

### 16.5 默认容量决策

- **默认 64**：`config/pipeline.json`、`PipelineSettings::lightListCapacity`、`kLightListDefaultCapacity` 统一为 64；`--light-capacity=32` 保留安全回退，`128` 保持 opt-in/experimental。
- 依据：32-light 基线 **0 diff** 且无性能退化；64-light 双后端 >60 fps；4K texture 路径不 clamp；内存增加约 1.11 MB；HDRP 的 coarse cluster 上限为 128、fine cell 默认 8/上限 24，64 总容量处于合理区间。
- 不默认 128：128-light/128 双后端 <60 fps，1440p+ texture 路径会 clamp，contact 额外成本明显。
- 不为了容量牺牲 60 fps 门禁；如果后续场景在目标 GPU 上 64 明显退化，可继续用 32 作为默认回退并记录 64 opt-in。

### 16.6 验证

- 默认 64、32-light M7 基线：D3D11/Vulkan 对 `hbg_m78/default_*.png` **0 diff**。
- 默认 64、64-light 场景：D3D11/Vulkan 的 GPU buffer vs CPU reference **0 diff**，`light_list_truncated=0`。
- 4K texture/64：`capacity=64/64`、`clamped=false`。
- `ctest` 209 cases / 20,225 assertions + IDL check 通过。

---

## 17. M8.7 验收记录（2026-11-14）

### 17.1 方法

- Debug/RelWithDebInfo 双构建；RelWithDebInfo 用于截图/性能；GPU RTX 2060 SUPER；D3D11 + Vulkan。
- 核心视觉矩阵：1280×720、3 帧、`--no-vsync`、`config/pipeline.json` + `config/frame_settings.json`；64/128-light 场景分别覆盖 forward、deferred、fog、contact、debug `light-cluster` slice 2、transparent、transparent + `--no-light-cluster`。
- 每类场景三路径：GPU buffer（`--gpu-light-list-path=buffer`）、GPU texture（`--gpu-light-list-path=texture`）、CPU reference（`--no-gpu-light-list`）。
- M7 基线：`m5_shadow_stress.gltf`，1280×720，capacity=32，forward + contact，与 `hbg_m78/default_d3d.png`、`default_vk.png`、`default_tex_*.png` 对比。
- 边界场景：`m8_lights33_same.gltf`（33 个同位置灯）、`m8_lights127_dir.gltf`（128 punctual + 1 directional，共 129 候选）、`m8_lights64/128.gltf`。
- 读回统计：`--cluster-stats-readback`，buffer 直接 `bgfx::read`，texture 走 READ_BACK staging blit；CPU 路径输出同格式统计。
- HDRP 参考：`LightLoopDef.hlsl:232-262` 的 runtime count/index 读取、`lightlistbuild-clustered.compute:388,453,475-482` 的 `MAX_NR_COARSE_ENTRIES`/`iSpaceAvail` 上限、`GlobalLightLoopSettings.cs:207` 的 cluster cell 默认 8/上限 24。

### 17.2 视觉与数值结果

- **M7 32-light 基线**：D3D11/Vulkan × buffer/texture/CPU 共 10 组对比，全部 **0 diff**。
- **64/128 核心矩阵**：2 capacity × 7 场景 × 3 路径 × 2 后端 = 84 组 GPU/CPU 及 buffer/texture 对比，全部 **0 diff**，所有进程 exit=0。
- **1080p / 128**：`path=texture, capacity=128/128, stride=129, clamped=false`，FPTL 宽度 15480；buffer/texture/CPU 三路径在 D3D11/Vulkan 均 **0 diff**。
- **边界/读回**：
  - 33 个同位置灯，capacity 64/128：CPU、GPU buffer、GPU texture 的 `clusters=7680 active=1440 overflow_clusters=1440 overflow_lights=1440 max_overflow=1 max_lights=32` 完全一致；第 33 个灯只进 overflow，不越界。
  - 64-light/64：`overflow_lights=42240 max_overflow=32 max_lights=32`，CPU tile/big-tile 最大值均为 64；GPU buffer/texture 与 CPU 统计一致。
  - 128-light/128：`overflow_lights=130560 max_overflow=96 max_lights=32`，CPU tile/big-tile 最大值均为 128；GPU buffer/texture 与 CPU 统计一致。
  - 129 候选（128 punctual + 1 directional）：capacity 64 时 `truncated=65`、CPU tile 最大 63；capacity 128 时 `truncated=1`、CPU tile 最大 127；两种情况下 cluster 仍为 32/cell，overflow 统计 CPU/GPU 一致。
- **0.5/高分辨率**：
  - 1280×720 `--render-scale=0.5`、capacity 128：buffer/texture GPU 与 CPU **0 diff**。
  - 3840 宽度（4K 水平）/capacity 32/64：buffer/texture/CPU 全部 **0 diff**；texture/64 `capacity=64/64`、无 clamp。
  - 2560/3840 宽度、capacity 128 texture：按 16383 pitch 上限 clamp 到 64，texture GPU vs CPU（同 clamp）**0 diff**；buffer 保持 `capacity=128/128`。注意 buffer 128 与 CPU 64 的截图不同是预期行为，不是回归。
  - 桌面高度限制使 1440/4K 窗口 framebuffer 高度为 1141；FPTL/big-tile 宽度与 clamp 仅由宽度和 stride 决定，因此不影响本次容量结论。
- **Resize/资源重建**：
  - texture 路径 1280×720 → 2544×1141 → 1264×681：观察到 `light list texture capacity: 128 -> 64` 与 `64 -> 128`，双后端一致。
  - buffer 路径同一 resize：`light list buffers` 从 2,999,296 B（1280）重建为 9,324,064 B（2544×1141），再重建为 2,799,748 B（1264×681）；旧 buffer 先 destroy 再 create，无 `ERROR`/validation。
- **Debug/回退验证**：`--bgfx-debug` + texture/128 + readback 无 validation error；仅缺少 RenderDoc/Aftermath DLL 的既有警告。禁用 buffer（`--no-gpu-light-list-buffers`）时 128/1280 自动走 `path=texture`，D3D11/Vulkan 与显式 texture 路径 **0 diff**；`--no-gpu-light-list` 的 CPU reference 也一致。
- **Fog 边界诊断**：shader 仍以 `u_fog_params5.x` 将每采样 punctual light 限制为 8；`fog_lights` 诊断改为 `min(punctualCount, maxPunctualLights)`，capacity=128 时日志为 `fog_lights=8`，不再误报 128。
- 本轮自动矩阵共 130 组对比：**126 组 0 diff**；其余 4 组是 1440p/4K 的 buffer/128 vs CPU/64，属于 CPU texture-width clamp 的设计内差异，不是回归。

### 17.3 1080p 性能复核

- 方法：RelWithDebInfo、1920×1080、300 帧、`--no-vsync`、Uffizi、full post、forward、capacity=64/128；64 为 7 次，128 为 3 次。
- 64-light/64 forward：
  - D3D11：**58.9–66.5 fps**（avg 63.8、median 64.1），GPU timer **11.67–16.14 ms**（avg 13.31）。最低一次 CPU submit 38.2 ms，属于桌面/DWM 抖动；GPU timer 仍低于 16.67 ms。
  - Vulkan：**69.1–76.3 fps**（avg 73.2），GPU **11.26–11.85 ms**。
- 64-light/64 默认 deferred：
  - D3D11：**47.1–62.5 fps**（avg 57.9、median 61.7），GPU **12.14–18.27 ms**；7 次中 2 次受系统调度影响明显，deferred 路径在 D3D11 上接近 60 fps 门禁，不承诺稳定余量。
  - Vulkan：**73.0–75.2 fps**，GPU **12.41–13.17 ms**。
- 128-light/128 forward：
  - D3D11：**40.8–46.5 fps**（avg 43.0），GPU **20.54–20.59 ms**。
  - Vulkan：**50.5–51.1 fps**（avg 50.7），GPU **17.80–19.11 ms**。
- 结论：M8.6 的 forward 64-light >60 门禁仍成立；deferred D3D11 余量较小，保留 32 为安全回退，128 继续 experimental。

### 17.4 门禁

- `hbg_tests`：**209 cases / 20,225 assertions**，全部通过。
- `ctest --preset vs2022-debug`：`hbg_tests` + `hbg_idl_check` 100% 通过。
- `--gfx-test`：D3D11、Vulkan 均通过（draw=true, compute=true）。
- `git diff --check`：无 whitespace error（仅工作区 LF→CRLF 提示）。
- Debug/RelWithDebInfo 构建通过，shader 全量变体编译通过。

### 17.5 M8 结论与边界

- M8 出口达成：32/64/128 契约稳定；32-light M7 基线 0 diff；64/128 双后端三路径 0 diff；cluster 32/cell 边界、overflow、truncation、texture clamp、resize 资源重建均有数值/截图证据。
- 默认保持 **64**（HDRP coarse cluster 128 / fine cell 默认 8、上限 24；本项目固定 32/cell）；32 是安全回退；128 是 opt-in/experimental。
- 已知限制：128 texture 在 1440p+ 自动 clamp 到 64；buffer 128 没有同分辨率满容量 CPU 对照（CPU 路径受 texture width 限制）；contact 最多 8 盏、fog punctual 默认 8、shadow requests 最多 64；D3D11 deferred 64-light 性能接近 60 fps，需在目标场景复核。
- M9 可继续 layered cluster offset、16-bit packing、category/feature variants、light layers、cookies/IES、GPU shadow atlas；M8 不引入这些。

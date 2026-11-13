# M8 Scalable Light Lists锛?2/64/128 瀹归噺锛夎鍒?
> 宸ョ▼鏍癸細`E:\Learn\hdrp-bgfx`銆?> 鐘舵€侊細**M8.0/M8.1 瀹屾垚锛孧8.2 鏈紑濮?*锛?026-11-13锛夈€侻8.0 宸茶惤瀹归噺璁剧疆銆丆LI/JSON/UI銆両DL 鍜?32 鍩虹嚎锛汳8.1 宸茶惤 CPU 32/64/128 reference銆侻8.2 璧风户缁敼 GPU shader/producer銆?> 鍘熷垯锛氬厛璇?HDRP 涓?vendored bgfx 婧愮爜鍐嶅疄鐜帮紱涓嶆敼鍙?M7 宸查獙璇佺殑瑙嗚璇箟锛涘閲忔墿灞曞繀椤讳繚鐣?32-light M7 鍩虹嚎鐨?**0 diff** 鍥為€€銆?> 鍓嶇疆鍩虹嚎锛歁7.8 宸插畬鎴愶紝`supportGpuLightList=true`銆乣supportGpuLightListBuffers=true` 榛樿寮€鍚紱texture/CPU 浠嶅彲鍥為€€锛汫PU 璺緞 effective punctual 浠?32銆乧luster 姣?cell 鍥哄畾 32锛岀洿鍒?M8.2銆?
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
| **M8.2 GPU FPTL/big-tile** | 128-slot AABB銆?脳mask銆佸姩鎬?stride銆?4/128 0 diff | `[ ]` 鏈紑濮?|
| **M8.3 GPU cluster** | 32/cell 淇濇寔锛?28 杈撳叆銆乷verflow/debug銆? diff | `[ ]` 鏈紑濮?|
| **M8.4 Buffer/texture consumers** | forward/deferred/contact/fog/debug 绉婚櫎 32 纭紪鐮?| `[ ]` 鏈紑濮?|
| **M8.5 闆嗘垚/鍥為€€/CLI/UI** | capacity 閫夋嫨銆乼exture 瀹藉害 clamp銆乺esize/4K銆佽瘖鏂?| `[ ]` 鏈紑濮?|
| **M8.6 鎬ц兘涓庨粯璁ゅ喅绛?* | 32/64/128 1080p 300 甯с€佸唴瀛樸€侀粯璁ゅ€?| `[ ]` 鏈紑濮?|
| **M8.7 M8 楠屾敹** | 鍙屽悗绔煩闃点€佹祴璇?IDL/gfx/docs銆佽竟鐣?| `[ ]` 鏈紑濮?|

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
- 1 涓?directional shadow 涓嶅彉锛泂hadow request 涓婇檺浠?64銆?- contact shadow mask 浠?32-bit锛涙渶澶?32 涓?punctual contact shadow 妲斤紝瓒呰繃閮ㄥ垎涓嶅垎閰?contact mask锛堜繚鎸?M5 璇箟锛夈€?- 浣撶Н闆?punctual light 涓婇檺浠嶇敱 fog settings锛堥粯璁?8锛夋帶鍒躲€?- shadowedCount/闃村奖绱㈠紩鍜?light capacity 瑙ｈ€︼紱涓嶅洜 capacity=128 鑷姩鍒涘缓 128 涓?shadow request銆?- M7 GPU shadow culling/indirect 浠嶆槸 opt-in锛孧8 涓嶄慨鏀瑰叾鏁版嵁甯冨眬銆?
---

## 5. 瀛愪换鍔¤缁嗚璁?
### M8.0 瀹归噺濂戠害涓庡熀绾块攣

鐩爣锛氬厛鍥哄畾鏁版嵁濂戠害鍜?32-light 鍥炲綊锛屼笉鏀硅瑙夈€?
- 鏂板 `PipelineSettings::lightListCapacity`锛?2/64/128锛夛紝`sanitize` 鍒板悎娉曟。浣嶃€?- 鏂板 CLI `--light-capacity=32|64|128`锛汮SON key `lightListCapacity`锛汥ebugUI 鏄剧ず requested/effective銆?- `kMaxGpuLights=128`銆乣kLightListMaxLights=128`銆乣kLightListStrideMax=129`锛涘垹闄ゅ浐瀹?`kLightListStride=33` 鐨?C++ 缂栬瘧鏈熺敤娉曪紝`LightListBuilder` 浣跨敤杩愯鏃?`capacity+1`銆侴PU shader 鐨?33/32 鍥哄畾鍊煎湪 M8.2 鎵嶆浛鎹紱鍦ㄦ涔嬪墠 GPU 璺緞鎶婅姹傚閲?clamp 鍒?32 骞跺彧璀﹀憡涓€娆°€?- 鍏堥攣瀹?M7 32-light 鍩虹嚎锛歚m61_thirtytwo`銆乣m61_transparent`銆乫og銆乧ontact銆乨eferred銆乫orward锛屽湪 capacity=32銆乥uffer/texture/CPU 涓嬩笌 M7 鎴浘 **0 diff**銆?- 鍗曟祴锛歝apacity 鍚堟硶鍊笺€侀潪娉曞€煎洖閫€銆乪ffective capacity銆?2 baseline 涓嶈蛋 128 鍒嗘敮銆?
楠屾敹锛?2-light 鍏ㄩ儴鏃㈡湁娴嬭瘯/鎴浘涓嶅彉锛涙柊璁剧疆鍙В鏋?搴忓垪鍖栵紱鏃?shader 缂栬瘧閿欒銆?
M8.0 楠岃瘉锛歚ctest` **207 cases / 20,170 assertions** + IDL check 閫氳繃锛汥3D11/Vulkan 榛樿 buffer銆乼exture 鍜?`--no-gpu-light-list` CPU 璺緞瀵?M7 `default_*.png` **0 diff**锛沗--light-capacity=64` 浼氭槑纭鍛婂苟鍥為€€ 32锛?4/128 鐨?GPU 鏁版嵁甯冨眬鐣欑粰 M8.2銆?
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

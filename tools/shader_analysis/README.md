# HBG Shader Analysis

C++20 rewrite of the Unity `com.unity.shaderanalysis` report pipeline plus a local
Vite/React browser. It compiles DXBC (D3D11) and SPIR-V (Vulkan) shader variants with
bgfx `shaderc`, parses disassembly/liveness/resource data, and writes summary, detail,
index, diff and CSV artifacts.

The full design and stage history live in
[`docs/shader-analysis-plan.md`](../../docs/shader-analysis-plan.md). The user guide is
[`docs/shader-analysis.md`](../../docs/shader-analysis.md).

## Requirements

- Windows with Visual Studio 2022 and CMake presets from `CMakePresets.json`.
- A configured bgfx `shaderc` target, or an explicit `--shaderc` path.
- Python 3 for `shader_build.py` plan checks.
- Node.js 20+ and npm for the web app.

## Build

Configure with shader analysis enabled; shader tools may remain disabled:

```powershell
cmake --preset vs2022 -DHBG_BUILD_SHADER_ANALYSIS=ON -DHBG_BUILD_SHADER_TOOLS=OFF
cmake --build --preset vs2022-debug --target hbg_shader_analysis hbg_shader_analysis_tests
```

If reconfigure tries to update an already configured xatlas dependency and the network
is unavailable, keep the existing FetchContent cache with
`-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.

The executable is at `build/vs2022/Debug/hbg_shader_analysis.exe`. The test binary is
`build/vs2022/tools/shader_analysis/Debug/hbg_shader_analysis_tests.exe`.

## Compile reports

```powershell
.\build\vs2022\Debug\hbg_shader_analysis.exe build `
  --root . `
  --variants shaders\variants.json `
  --backend all `
  --pass forward `
  --jobs 8 `
  --out shader_analysis\reports
```

Useful options:

- `--backend dxbc|spirv|all`
- `--stage vertex|fragment|compute`
- `--variant-filter "ALPHA_CLIP&HAS_MASK_MAP|DOUBLE_SIDED"` (`A&B|C` keyword semantics)
- `--define NAME[=VALUE]`
- `--debug-info` for SPIR-V line information (debug cache is separate)
- `--force`, `--no-cache`, `--fail-fast`, `--progress plain|json|none`

Reports are written to:

```text
shader_analysis/reports/<backend>/<sourcePath>.json          summary
shader_analysis/reports/<backend>/<sourcePath>/<id>.json     variant detail
shader_analysis/reports/index.json                          program index
```

Debug detail names use `<id>-debug.json`.

## Shared compile plan

`shaders/variants.json` contains both the compile tasks and the `shaders/manifest.json`
pass-slot mapping. Normal Python builds use this plan automatically when `shaders/variants.json` exists; `--no-plan` falls back to the legacy hardcoded schedule. CMake `hbg_shaders` also executes it directly:

```powershell
py -3 tools/shader_build.py --execute-plan shaders/variants.json `
  --shaderc <shaderc> --bgfx-src <bgfx-src>
```

After changing shaders or variant generation, regenerate and check the plan:

```powershell
py -3 tools/shader_build.py --dump-plan shaders/variants.json
py -3 tools/shader_build.py --plan-check shaders/variants.json
```

## Compare, export, index

```powershell
# Diff current reports against the checked-in summary baseline
.\build\vs2022\Debug\hbg_shader_analysis.exe diff `
  shader_analysis\reports `
  shader_analysis\reference `
  --out shader_analysis\reports\diff.json

# Export the 23-column backend-neutral CSV
.\build\vs2022\Debug\hbg_shader_analysis.exe export `
  --report shader_analysis\reports `
  --format csv `
  --out shader_analysis\reports\report.csv

# Copy summary-only baselines (paths are cleared, target index is rebuilt)
.\build\vs2022\Debug\hbg_shader_analysis.exe set-reference `
  --from shader_analysis\reports `
  --to shader_analysis\reference

# Inspect/validate
.\build\vs2022\Debug\hbg_shader_analysis.exe list --report shader_analysis\reports
.\build\vs2022\Debug\hbg_shader_analysis.exe validate --report shader_analysis\reports
```

`diff` ignores compile time/cache/timestamps. `--ignore-timestamps` writes null
generation times; `--fail-on-regression` returns 1 for positive deltas in the
regression metrics. Reference folders intentionally contain summaries only, so assembly
diff needs a report root that also has variant details.

## Run the web app

```powershell
cd tools\shader_analysis\web
npm install
npm run build
cd ..\..\..
node tools\shader_analysis\serve.mjs --root .
```

Open `http://127.0.0.1:8787/web/`. The server is read-only and accepts GET/HEAD only.
It serves `/web/*` from `web/dist`, `/shader_analysis/*` from the repository report
directory, and `/shaders/*` from the shader sources. Path traversal is rejected.

For hot reload:

```powershell
cd tools\shader_analysis\web
npm run dev
```

Vite proxies `/shader_analysis` and `/shaders` to port 8787.

## Tests

```powershell
ctest --test-dir build/vs2022 -C Debug -R shader_analysis --output-on-failure
cd tools\shader_analysis\web
npm run typecheck
npm run build
npm run smoke
```

`npm run smoke` starts a temporary `serve.mjs`, checks reports, source files, SPA
fallback, traversal and POST rejection, then uses headless Chrome/Edge when available
to verify rendered Index, Program, Source search and Assembly routes. Use
`npm run smoke:browser` to require a browser or set `CHROME_PATH`.

The C++ test suite covers manifest selection, filters, hashing, report JSON, DXBC/SPIR-V
parsing, liveness, diff, CSV, reference copying and plan consistency.

## Notes and limitations

- Metrics are static estimates from a linear liveness scan, not hardware occupancy.
- DXBC does not provide source line mapping in this version.
- SPIR-V source lines require `--debug-info`; line numbers can refer to preprocessed
  source, so the browser warns when the line is outside the original file.
- Summary JSON is small; detail JSON can be many megabytes per optimized fragment
  variant. The browser loads detail only for the selected variant.
- The web app is local-only; it is not part of CI or a production service.

#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const webDirectory = path.resolve(scriptDirectory, '..');
const repoRoot = path.resolve(scriptDirectory, '../../../..');
const serveScript = path.join(repoRoot, 'tools/shader_analysis/serve.mjs');
const distIndex = path.join(webDirectory, 'dist/index.html');
const args = process.argv.slice(2);
const requireBrowser = args.includes('--browser') || args.includes('--require-browser');
const explicitPort = args.find((value) => value.startsWith('--port='));
const timeoutMs = 180_000;

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

async function freePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const port = server.address().port;
  await new Promise((resolve) => server.close(resolve));
  return port;
}

async function waitForServer(url) {
  const deadline = Date.now() + timeoutMs;
  let lastError;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(url);
      if (response.ok) {
        return;
      }
      lastError = new Error(`${response.status} ${response.statusText}`);
    } catch (error) {
      lastError = error;
    }
    await new Promise((resolve) => setTimeout(resolve, 150));
  }
  throw new Error(`server did not start: ${lastError?.message ?? 'timeout'}`);
}

async function request(url, options = {}) {
  return fetch(url, { cache: 'no-store', ...options });
}

async function checkHttp(baseUrl) {
  const web = await request(`${baseUrl}/web/`);
  assert(web.status === 200, `/web/ returned ${web.status}`);
  assert(
    (web.headers.get('content-type') ?? '').includes('text/html'),
    '/web/ is not HTML',
  );
  const html = await web.text();
  assert(html.includes('id="root"'), '/web/ did not return the Vite shell');

  const indexResponse = await request(`${baseUrl}/shader_analysis/reports/index.json`);
  assert(indexResponse.status === 200, 'reports index is unavailable');
  const index = await indexResponse.json();
  assert(Array.isArray(index.entries) && index.entries.length > 0, 'reports index is empty');
  const entry = index.entries.find((item) => item.backend === 'dxbc') ?? index.entries[0];

  const summaryResponse = await request(
    `${baseUrl}/shader_analysis/reports/${entry.summaryPath}`,
  );
  assert(summaryResponse.status === 200, 'report summary is unavailable');
  const summary = await summaryResponse.json();
  const variant =
    summary.variants.find((item) => item.status === 'ok') ?? summary.variants[0];
  assert(variant != null, 'report summary has no variants');

  const sourceResponse = await request(`${baseUrl}/${entry.source}`);
  assert(sourceResponse.status === 200, 'shader source is unavailable');
  assert((await sourceResponse.text()).length > 0, 'shader source is empty');

  let assemblyNeedle =
    Object.entries(variant.metrics?.opcodeHistogram ?? {}).sort(
      (left, right) => right[1] - left[1],
    )[0]?.[0] || 'mov';
  if (variant.detailPath) {
    const detailResponse = await request(
      `${baseUrl}/shader_analysis/reports/${variant.detailPath}`,
    );
    if (detailResponse.ok) {
      const detail = await detailResponse.json();
      assemblyNeedle = detail.instructions?.[0]?.opcode || assemblyNeedle;
    }
  }

  const assetPath = html.match(/src="([^"]+\.js)"/)?.[1];
  if (assetPath) {
    const asset = await request(`${baseUrl}${assetPath}`);
    assert(asset.status === 200, `web asset ${assetPath} is unavailable`);
  }

  const route = `/web/variant?dataset=reports&backend=${encodeURIComponent(
    entry.backend,
  )}&summary=${encodeURIComponent(entry.summaryPath)}&source=${encodeURIComponent(
    entry.source,
  )}&variantId=${encodeURIComponent(variant.variantId)}&tab=source&q=ALPHA_CLIP`;
  const routeResponse = await request(`${baseUrl}${route}`);
  assert(routeResponse.status === 200, 'SPA fallback for a variant route failed');
  assert(
    (await routeResponse.text()).includes('id="root"'),
    'SPA fallback did not return the shell',
  );

  const traversal = await request(`${baseUrl}/web/%2e%2e/serve.mjs`);
  assert(traversal.status === 404, 'path traversal was not rejected');

  const post = await request(`${baseUrl}/web/`, { method: 'POST' });
  assert(post.status === 405, 'POST was not rejected');

  return { index, entry, summary, variant, assemblyNeedle, baseUrl };
}

function chromeCandidates() {
  const candidates = [];
  if (process.env.CHROME_PATH) {
    candidates.push(process.env.CHROME_PATH);
  }
  if (process.platform === 'win32') {
    const programFiles = process.env.ProgramFiles;
    const programFilesX86 = process.env['ProgramFiles(x86)'];
    const localAppData = process.env.LOCALAPPDATA;
    if (programFiles) {
      candidates.push(path.join(programFiles, 'Google/Chrome/Application/chrome.exe'));
      candidates.push(path.join(programFiles, 'Microsoft/Edge/Application/msedge.exe'));
    }
    if (programFilesX86) {
      candidates.push(
        path.join(programFilesX86, 'Google/Chrome/Application/chrome.exe'),
      );
      candidates.push(
        path.join(programFilesX86, 'Microsoft/Edge/Application/msedge.exe'),
      );
    }
    if (localAppData) {
      candidates.push(
        path.join(localAppData, 'Google/Chrome/Application/chrome.exe'),
      );
    }
  } else if (process.platform === 'darwin') {
    candidates.push('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome');
    candidates.push('/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge');
  } else {
    candidates.push('/usr/bin/google-chrome');
    candidates.push('/usr/bin/chromium');
    candidates.push('/usr/bin/chromium-browser');
    candidates.push('/usr/bin/microsoft-edge');
  }
  return candidates;
}

async function findChrome() {
  for (const candidate of chromeCandidates()) {
    try {
      await fs.access(candidate);
      return candidate;
    } catch {
      continue;
    }
  }
  return null;
}

function runChrome(chrome, url) {
  return new Promise((resolve, reject) => {
    const userDataDir = `${os.tmpdir()}/hbg-shader-analysis-smoke-${process.pid}-${Date.now()}`;
    const child = spawn(
      chrome,
      [
        '--headless=new',
        '--disable-gpu',
        '--no-sandbox',
        '--no-first-run',
        '--disable-extensions',
        '--log-level=3',
        `--user-data-dir=${userDataDir}`,
        '--virtual-time-budget=5000',
        '--dump-dom',
        url,
      ],
      { stdio: ['ignore', 'pipe', 'pipe'] },
    );
    let stdout = '';
    let stderr = '';
    const timer = setTimeout(() => {
      child.kill();
      reject(new Error(`Chrome timed out for ${url}`));
    }, timeoutMs);
    child.stdout.on('data', (chunk) => {
      stdout += chunk;
    });
    child.stderr.on('data', (chunk) => {
      stderr += chunk;
    });
    child.on('error', (error) => {
      clearTimeout(timer);
      reject(error);
    });
    child.on('close', async (code) => {
      clearTimeout(timer);
      await fs.rm(userDataDir, { recursive: true, force: true }).catch(() => {});
      if (code !== 0) {
        reject(new Error(`Chrome exited ${code}: ${stderr.slice(-500)}`));
        return;
      }
      resolve(stdout);
    });
  });
}

async function checkBrowser(chrome, context) {
  const { entry, summary, variant, assemblyNeedle, baseUrl } = context;
  const programUrl = `${baseUrl}/web/program?dataset=reports&backend=${encodeURIComponent(
    entry.backend,
  )}&summary=${encodeURIComponent(entry.summaryPath)}&source=${encodeURIComponent(
    entry.source,
  )}`;
  const baseVariantUrl = `${baseUrl}/web/variant?dataset=reports&backend=${encodeURIComponent(
    entry.backend,
  )}&summary=${encodeURIComponent(entry.summaryPath)}&source=${encodeURIComponent(
    entry.source,
  )}&variantId=${encodeURIComponent(variant.variantId)}`;
  const variantUrl = `${baseVariantUrl}&tab=source&q=ALPHA_CLIP`;
  const assemblyUrl = `${baseVariantUrl}&tab=assembly`;
  const assemblyDeepUrl = `${assemblyUrl}&asmq=${encodeURIComponent(
    assemblyNeedle,
  )}&asmfilter=matches`;

  const rootDom = await runChrome(chrome, `${baseUrl}/web/`);
  assert(rootDom.includes('Shader reports'), 'index did not render its heading');
  const programDom = await runChrome(chrome, programUrl);
  assert(programDom.includes('Download CSV'), 'program page did not render');
  const variantDom = await runChrome(chrome, variantUrl);
  assert(variantDom.includes('Find in source'), 'source search did not render');
  assert(
    variantDom.includes('matching line') || variantDom.includes('No lines match'),
    'source search result count did not render',
  );
  assert(
    variantDom.includes('match-active') || variantDom.includes('No lines match'),
    'source deep-link match was not activated',
  );
  const assemblyDom = await runChrome(chrome, assemblyUrl);
  assert(assemblyDom.includes('Search instructions'), 'assembly search did not render');
  assert(
    assemblyDom.includes('Filter matches'),
    'assembly highlight/filter control did not render',
  );
  const assemblyDeepDom = await runChrome(chrome, assemblyDeepUrl);
  assert(
    assemblyDeepDom.includes('matching · filtered'),
    'assembly deep-linked filter did not apply',
  );
  assert(
    assemblyDeepDom.includes('search-match'),
    'assembly deep-linked query did not highlight rows',
  );
  const uploadDom = await runChrome(chrome, `${baseUrl}/web/upload`);
  assert(
    uploadDom.includes('Open a report JSON'),
    'upload page did not render its drop zone',
  );
  assert(
    uploadDom.includes('Drop detail or summary JSON here'),
    'upload page did not render its drop instructions',
  );
  assert(summary.source.path.length > 0, 'summary source path is empty');
}

async function main() {
  await fs.access(distIndex).catch(() => {
    throw new Error('web/dist/index.html is missing; run npm run build first');
  });
  const port = explicitPort ? Number(explicitPort.slice('--port='.length)) : await freePort();
  assert(Number.isInteger(port) && port > 0, 'invalid --port value');

  const server = spawn(
    process.execPath,
    [serveScript, '--root', repoRoot, '--port', String(port)],
    { cwd: repoRoot, stdio: ['ignore', 'pipe', 'pipe'] },
  );
  let serverOutput = '';
  server.stdout.on('data', (chunk) => {
    serverOutput += chunk;
  });
  server.stderr.on('data', (chunk) => {
    serverOutput += chunk;
  });
  const shutdown = () => {
    server.kill();
  };
  process.once('SIGINT', shutdown);
  process.once('SIGTERM', shutdown);

  try {
    const baseUrl = `http://127.0.0.1:${port}`;
    await waitForServer(`${baseUrl}/web/`);
    const context = await checkHttp(baseUrl);
    const chrome = await findChrome();
    if (chrome) {
      console.log(`smoke: browser ${chrome}`);
      await checkBrowser(chrome, context);
    } else if (requireBrowser) {
      throw new Error('Chrome/Edge was not found; set CHROME_PATH');
    } else {
      console.log('smoke: browser not found, HTTP checks passed');
    }
    console.log(`smoke: OK (${context.index.entries.length} report entries)`);
  } catch (error) {
    if (serverOutput.trim()) {
      console.error(serverOutput.trim());
    }
    throw error;
  } finally {
    server.kill();
    await new Promise((resolve) => {
      const timer = setTimeout(resolve, 2000);
      server.once('close', () => {
        clearTimeout(timer);
        resolve();
      });
    });
  }
}

main().catch((error) => {
  console.error(`smoke: ${error.message}`);
  process.exitCode = 1;
});

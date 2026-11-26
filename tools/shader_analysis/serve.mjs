#!/usr/bin/env node

import { createReadStream } from 'node:fs';
import { promises as fs } from 'node:fs';
import { createServer } from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const toolDirectory = path.dirname(fileURLToPath(import.meta.url));
const webDirectory = path.join(toolDirectory, 'web', 'dist');

const mimeTypes = new Map([
  ['.html', 'text/html; charset=utf-8'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.mjs', 'text/javascript; charset=utf-8'],
  ['.css', 'text/css; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.map', 'application/json; charset=utf-8'],
  ['.svg', 'image/svg+xml'],
  ['.png', 'image/png'],
  ['.jpg', 'image/jpeg'],
  ['.jpeg', 'image/jpeg'],
  ['.ico', 'image/x-icon'],
  ['.woff', 'font/woff'],
  ['.woff2', 'font/woff2'],
  ['.sc', 'text/plain; charset=utf-8'],
  ['.bin', 'application/octet-stream'],
  ['.disasm', 'text/plain; charset=utf-8'],
  ['.md', 'text/markdown; charset=utf-8'],
  ['.txt', 'text/plain; charset=utf-8'],
]);

function usage() {
  console.log(`Usage: node tools/shader_analysis/serve.mjs [--root <repo>] [--port <port>]

Serves:
  /web/*              built Vite app from tools/shader_analysis/web/dist
  /shader_analysis/*  reports/cache/reference under <repo>/shader_analysis
  /shaders/*          shader sources under <repo>/shaders

Options:
  --root <repo>       repository root (default: current directory)
  --port <port>       listen port (default: 8787)
  --help, -h          show this help`);
}

function parseArguments(argv) {
  let root = process.cwd();
  let port = 8787;
  for (let index = 0; index < argv.length; index++) {
    const argument = argv[index];
    if (argument === '--help' || argument === '-h') {
      usage();
      process.exit(0);
    }
    if (argument === '--root' || argument === '--port') {
      const value = argv[++index];
      if (!value) {
        throw new Error(`missing value for ${argument}`);
      }
      if (argument === '--root') {
        root = path.resolve(value);
      } else {
        port = Number(value);
      }
      continue;
    }
    throw new Error(`unknown option: ${argument}`);
  }
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error(`invalid port: ${port}`);
  }
  return { root, port };
}

function inside(candidate, base) {
  return candidate === base || candidate.startsWith(`${base}${path.sep}`);
}

function isSafeRelativePath(relative) {
  if (!relative) {
    return true;
  }
  const normalized = relative.replaceAll('\\', '/');
  if (normalized.startsWith('/')) {
    return false;
  }
  return normalized
    .split('/')
    .every((part) => part !== '..' && part !== '.' && !part.startsWith('.'));
}

async function resolveInside(base, relative) {
  if (!isSafeRelativePath(relative)) {
    return null;
  }
  const candidate = path.resolve(base, relative);
  if (!inside(candidate, base)) {
    return null;
  }
  try {
    const real = await fs.realpath(candidate);
    return inside(real, base) ? real : null;
  } catch {
    return null;
  }
}

function sendText(response, status, text, method = 'GET') {
  const body = Buffer.from(text, 'utf8');
  response.writeHead(status, {
    'Content-Type': 'text/plain; charset=utf-8',
    'Content-Length': body.length,
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  });
  if (method === 'HEAD') {
    response.end();
  } else {
    response.end(body);
  }
}

async function sendFile(response, filePath, method) {
  const stats = await fs.stat(filePath);
  if (!stats.isFile()) {
    sendText(response, 404, 'Not found', method);
    return;
  }
  const extension = path.extname(filePath).toLowerCase();
  response.writeHead(200, {
    'Content-Type': mimeTypes.get(extension) ?? 'application/octet-stream',
    'Content-Length': stats.size,
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  });
  if (method === 'HEAD') {
    response.end();
    return;
  }
  const stream = createReadStream(filePath);
  stream.on('error', () => response.destroy());
  stream.pipe(response);
}

async function main() {
  const { root, port } = parseArguments(process.argv.slice(2));
  let rootReal;
  try {
    rootReal = await fs.realpath(root);
    const stats = await fs.stat(rootReal);
    if (!stats.isDirectory()) {
      throw new Error('not a directory');
    }
  } catch (error) {
    throw new Error(`invalid root ${root}: ${error.message}`);
  }

  let webReal = null;
  try {
    webReal = await fs.realpath(webDirectory);
  } catch {
    webReal = null;
  }

  const server = createServer(async (request, response) => {
    const method = request.method ?? 'GET';
    if (method !== 'GET' && method !== 'HEAD') {
      response.writeHead(405, { Allow: 'GET, HEAD' });
      response.end();
      return;
    }

    let pathname;
    try {
      const url = new URL(request.url ?? '/', 'http://127.0.0.1');
      pathname = decodeURIComponent(url.pathname);
    } catch {
      sendText(response, 400, 'Bad request', method);
      return;
    }

    if (pathname.includes('\0')) {
      sendText(response, 400, 'Bad request', method);
      return;
    }

    if (pathname === '/' || pathname === '/web') {
      const url = new URL(request.url ?? '/', 'http://127.0.0.1');
      response.writeHead(302, { Location: `/web/${url.search}` });
      response.end();
      return;
    }

    try {
      if (pathname.startsWith('/web/')) {
        if (!webReal) {
          sendText(
            response,
            503,
            'Web build not found. Run: cd tools/shader_analysis/web && npm install && npm run build',
            method,
          );
          return;
        }
        let relative = pathname.slice('/web/'.length);
        if (relative === '' || relative.endsWith('/')) {
          relative += 'index.html';
        }
        let target = await resolveInside(webReal, relative);
        if (!target && (!path.extname(relative) || relative.endsWith('/index.html'))) {
          target = await resolveInside(webReal, 'index.html');
        }
        if (!target) {
          sendText(response, 404, 'Not found', method);
          return;
        }
        await sendFile(response, target, method);
        return;
      }

      if (pathname.startsWith('/shader_analysis/') || pathname.startsWith('/shaders/')) {
        const relative = pathname.slice(1);
        const target = await resolveInside(rootReal, relative);
        if (!target) {
          sendText(response, 404, 'Not found', method);
          return;
        }
        await sendFile(response, target, method);
        return;
      }

      sendText(response, 404, 'Not found', method);
    } catch (error) {
      sendText(response, 500, `Server error: ${error.message}`, method);
    }
  });

  server.listen(port, '127.0.0.1', () => {
    console.log(`serve: http://127.0.0.1:${port}/web/`);
    console.log(`serve: reports http://127.0.0.1:${port}/shader_analysis/reports/`);
    console.log(`serve: root ${rootReal}`);
  });

  const shutdown = () => {
    server.close(() => process.exit(0));
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

main().catch((error) => {
  console.error(`serve: ${error.message}`);
  process.exitCode = 1;
});

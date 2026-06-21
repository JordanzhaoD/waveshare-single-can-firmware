#!/usr/bin/env node
/**
 * 将 docs/OPERATION_MANUAL.md 渲染为精美 HTML + PDF。
 * 用法: NODE_PATH=<marked 所在 node_modules> node scripts/build_manual_pdf.mjs
 * 依赖: marked (npm install marked)
 * PDF 引擎: macOS Chrome / Chromium / Edge headless --print-to-pdf
 */
import { marked } from 'marked';
import { readFileSync, writeFileSync, existsSync } from 'fs';
import { execSync } from 'child_process';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = join(__dirname, '..');
const SRC = join(ROOT, 'docs', 'OPERATION_MANUAL.md');
const HTML_OUT = join(ROOT, 'docs', 'OPERATION_MANUAL.html');
const PDF_OUT = join(ROOT, 'docs', 'OPERATION_MANUAL.pdf');

marked.setOptions({ gfm: true, breaks: false, headerIds: true });

let md = readFileSync(SRC, 'utf8');
md = md.replace(/^---[\s\S]*?---\n/, ''); // 去 frontmatter
let html = marked.parse(md);

// 折叠 callout（> [!question]-）：渲染时去掉 - 标记即可（PDF 全展开）
// OB callout: <blockquote><p>[!type]<br>...</p></blockquote> → div.callout
html = html.replace(
  /<blockquote>\s*<p>\[!([\w-]+)\]?\s*(?:<br\s*\/?>)?([\s\S]*?)<\/p>\s*<\/blockquote>/g,
  (m, typeRaw, content) => {
    const type = typeRaw.toLowerCase().replace(/-$/, '');
    const titles = {
      info: 'ℹ️ 信息', tip: '💡 提示', warning: '⚠️ 注意',
      danger: '⛔ 危险', check: '✅ 清单', bug: '🐛 排错',
      question: '❓ FAQ', example: '📝 示例', success: '✅ 完成', note: '📝 备注',
    };
    const icon = titles[type] || `📝 ${type}`;
    return `<div class="callout callout-${type}"><div class="callout-title">${icon}</div><div class="callout-body">${content.trim()}</div></div>`;
  }
);

const fullHtml = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>Atlas-FSD 操作手册</title>
<style>
@page { size: A4; margin: 18mm 15mm; }
* { box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, "PingFang SC", "Microsoft YaHei", "Helvetica Neue", sans-serif; font-size: 10.8pt; line-height: 1.68; color: #1f2328; }
img { max-width: 100%; max-height: 225mm; height: auto; object-fit: contain; border-radius: 6px; border: 1px solid #d0d7de; margin: 0.9em auto; display: block; page-break-inside: avoid; }
h1 { font-size: 23pt; color: #0969da; border-bottom: 3px solid #0969da; padding-bottom: 10px; margin-top: 0; }
h2 { font-size: 15.5pt; color: #0969da; margin-top: 1.7em; border-bottom: 1px solid #d0d7de; padding-bottom: 5px; page-break-after: avoid; }
h3 { font-size: 12.6pt; color: #24292f; margin-top: 1.3em; page-break-after: avoid; }
p { margin: 0.55em 0; }
a { color: #0969da; text-decoration: none; word-break: break-all; }
code { background: #eff1f3; padding: 1.5px 5px; border-radius: 4px; font-family: "SF Mono", "JetBrains Mono", Consolas, monospace; font-size: 0.86em; color: #cf222e; }
pre { background: #161b22; color: #e6edf3; padding: 12px 14px; border-radius: 8px; overflow-x: auto; page-break-inside: avoid; font-size: 9.2pt; }
pre code { background: transparent; color: inherit; padding: 0; font-size: 1em; }
table { border-collapse: collapse; width: 100%; margin: 0.8em 0; font-size: 9.8pt; page-break-inside: avoid; }
th, td { border: 1px solid #d0d7de; padding: 6px 10px; text-align: left; vertical-align: top; }
th { background: #f6f8fa; font-weight: 600; }
tr:nth-child(even) td { background: #fafbfc; }
blockquote { border-left: 3px solid #d0d7de; margin: 0.8em 0; padding: 0.4em 1em; color: #57606a; background: #f6f8fa; }
.callout { border: 1px solid; border-left-width: 4px; border-radius: 7px; padding: 10px 14px; margin: 1em 0; page-break-inside: avoid; }
.callout-title { font-weight: 700; margin-bottom: 5px; font-size: 10.5pt; }
.callout-info, .callout-note { background: #ddf4ff; border-color: #0969da; color: #0a3069; }
.callout-tip { background: #dafbe1; border-color: #1a7f37; color: #0a3622; }
.callout-warning, .callout-bug { background: #fff8c5; border-color: #9a6700; color: #5d4204; }
.callout-danger { background: #ffebe9; border-color: #cf222e; color: #82071e; }
.callout-check, .callout-success { background: #dafbe1; border-color: #1a7f37; color: #0a3622; }
.callout-question, .callout-faq { background: #ddf4ff; border-color: #0969da; color: #0a3069; }
.callout-example { background: #f6f8fa; border-color: #8250df; color: #512598; }
.callout-body p:first-child { margin-top: 0; }
.callout-body p:last-child { margin-bottom: 0; }
.callout-body code { background: rgba(0,0,0,0.06); }
hr { border: none; border-top: 1px solid #d0d7de; margin: 1.6em 0; }
ul, ol { padding-left: 1.6em; }
li { margin: 0.25em 0; }
strong { color: #24292f; }
</style>
</head>
<body>
${html}
</body>
</html>`;

writeFileSync(HTML_OUT, fullHtml);
console.log('✅ HTML:', HTML_OUT, `(${fullHtml.length} bytes)`);

const chromePaths = [
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  '/Applications/Chromium.app/Contents/MacOS/Chromium',
  '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
  '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser',
];
const chrome = chromePaths.find((p) => existsSync(p));
if (!chrome) {
  console.error('❌ 未找到 Chrome/Chromium/Edge。请安装 Google Chrome。');
  process.exit(1);
}
execSync(
  `"${chrome}" --headless --disable-gpu --no-pdf-header-footer --print-to-pdf="${PDF_OUT}" "file://${HTML_OUT}"`,
  { stdio: 'inherit' }
);
console.log('✅ PDF:', PDF_OUT);

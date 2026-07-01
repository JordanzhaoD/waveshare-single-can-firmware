#pragma once
#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <Arduino.h>
#endif

static const char DASH_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title data-product-name data-single-text="Atlas Single CAN">Atlas T-2CAN</title>
<style>
/* === CSS Variables === */
:root {
  --sidebar-bg: #111827;
  --main-bg: #1f2937;
  --card-bg: #374151;
  --card-bg-alt: #1f2937;
  --accent: #7c3aed;
  --accent-light: #a78bfa;
  --ok: #4ade80;
  --err: #f87171;
  --warn: #fbbf24;
  --info: #60a5fa;
  --tx1: #f9fafb;
  --tx2: #d1d5db;
  --tx3: #9ca3af;
  --border: #4b5563;
  --header-bg: #111827;
  --sidebar-w: 220px;
  --cockpit-bg: #05070b;
  --cockpit-panel: rgba(15,23,42,0.82);
  --cockpit-panel-strong: #101827;
  --cockpit-soft: rgba(30,41,59,0.62);
  --cockpit-stroke: rgba(148,163,184,0.20);
  --cockpit-stroke-strong: rgba(148,163,184,0.36);
  --cockpit-muted: #8d9bb4;
  --cockpit-dim: #66748c;
  --cockpit-cyan: #38bdf8;
  --cockpit-radius-xl: 28px;
  --cockpit-radius-lg: 20px;
  --cockpit-shadow: 0 24px 64px rgba(0,0,0,0.35);
  --cockpit-glow-purple: 0 0 42px rgba(124,58,237,0.22);
  --cockpit-glow-cyan: 0 0 34px rgba(56,189,248,0.18);
  --cockpit-glow-danger: 0 0 34px rgba(248,113,113,0.22);
  --cockpit-glow-ok: 0 0 34px rgba(74,222,128,0.20);
  --cockpit-card-blur: blur(22px) saturate(1.16);
  /* === 设计系统基底（V1.1，全板块统一） === */
  --ds-cyan: #0ea5e9;
  --ds-indigo: #6366f1;
  --ds-accent-grad: linear-gradient(135deg, #0ea5e9, #6366f1);
  --ds-ok: #34d399;
  --ds-warn: #f59e0b;
  --ds-err: #f87171;
  --ds-card-bg: rgba(15,23,42,.58);
  --ds-card-stroke: rgba(148,163,184,.20);
  --ds-radius: 18px;
}
/* === Reset & Base === */
* { margin:0; padding:0; box-sizing:border-box; }
body { font-family: -apple-system, 'SF Pro Text', 'Helvetica Neue', sans-serif;
  background: var(--main-bg); color: var(--tx1); font-size: 15px;
  display: flex; height: 100vh; overflow: hidden;
  -webkit-text-size-adjust: 100%; -webkit-overflow-scrolling: touch; }

/* === Sidebar === */
.sidebar { width: var(--sidebar-w); background: var(--sidebar-bg);
  display: flex; flex-direction: column; flex-shrink: 0;
  border-right: 1px solid var(--border); overflow-y: auto; }
.sidebar-hdr { padding: 22px 24px 18px; border-bottom: 1px solid var(--border); }
.sidebar-hdr h1 { font-size: 24px; font-weight: 800; color: var(--accent); letter-spacing: 0; }
.sidebar-hdr p { font-size: 11px; color: var(--tx3); margin-top: 2px; }
.sidebar-nav { flex: 1; padding: 16px 10px; }
.nav-item { display: flex; align-items: center; gap: 14px; padding: 16px 18px; color: var(--tx3);
  font-size: 19px; cursor: pointer; border-radius: 12px;
  transition: background .15s, color .15s; user-select: none; font-weight: 600; }
.nav-item .nav-icon { font-size: 24px; line-height: 1; flex-shrink: 0; }
.nav-item:hover { background: var(--main-bg); color: var(--tx2); }
.nav-item.active { background: rgba(124,58,237,0.18); color: var(--accent-light); font-weight: 800; font-size: 20px; }
.sidebar-ft { padding: 10px 12px; border-top: 1px solid var(--border);
  display: flex; gap: 6px; }
.sidebar-ft button { flex:1; background: var(--card-bg); border: none;
  color: var(--tx3); padding: 4px 0; border-radius: 4px; font-size: 11px;
  cursor: pointer; }
.sidebar-ft button:hover { color: var(--tx2); }

/* === Main Area === */
.main { flex: 1; display: flex; flex-direction: column; overflow: hidden; }
.topbar { background: var(--header-bg); padding: 10px 16px;
  display: flex; align-items: center; gap: 12px; flex-shrink: 0;
  border-bottom: 1px solid var(--border); min-height: 44px; }
.topbar-badge { padding: 7px 16px; border-radius: 999px; font-size: 13px; font-weight: 800; border: 1px solid var(--border); background: var(--card-bg-alt); }
.badge-ok { color: var(--tx1); }
.badge-err { color: var(--err); }
.toast{position:fixed;bottom:60px;left:50%;transform:translateX(-50%);background:#7f1d1d;color:#fff;padding:8px 18px;border-radius:8px;font-size:13px;z-index:999;opacity:0;transition:opacity .3s;pointer-events:none}
.toast.ok{background:#166534}
.toast.show{opacity:1}
.badge-warn { color: var(--tx1); }
.topbar-fps { color: var(--tx1); font-size: 14px; font-weight: 800; padding: 7px 16px; border-radius: 999px; background: var(--card-bg-alt); border: 1px solid var(--border); }
.topbar-time { margin-left: auto; color: var(--tx3); font-size: 12px; }
.topbar-exp { color: var(--warn); font-size: 12px; font-weight: 800; padding: 7px 12px; border-radius: 999px; background: rgba(251,191,36,0.12); border: 1px solid rgba(251,191,36,0.35); }
.mobile-theme-toggle { display: none; border: 1px solid var(--border); background: var(--card-bg-alt); color: var(--tx1); border-radius: 999px; min-width: 76px; height: 32px; padding: 0 10px; font-size: 12px; font-weight: 800; white-space: nowrap; }
.topbar-dot { width: 8px; height: 8px; border-radius: 50%; display: inline-block;
  flex-shrink: 0; }
.topbar-dot.ok { background: var(--ok); box-shadow: 0 0 6px var(--ok); }
.topbar-dot.err { background: var(--err); box-shadow: 0 0 6px var(--err); }
.topbar-dot.warn { background: var(--warn); box-shadow: 0 0 6px var(--warn); }
.content { flex: 1; overflow-y: auto; overflow-x: hidden; padding: 22px;
  -webkit-overflow-scrolling: touch; }

/* === Pages === */
.page { display: none; }
.page.active { display: block; }

/* === Cards === */
.card { background: var(--ds-card-bg); border: 1px solid var(--ds-card-stroke); border-radius: var(--ds-radius); padding: 20px; backdrop-filter: var(--cockpit-card-blur); box-shadow: 0 8px 28px rgba(0,0,0,.22); margin-bottom: 14px; }
.page-title { font-size: 26px; font-weight: 800; margin: 4px 0 20px; padding-bottom: 16px; border-bottom: 1px solid var(--border); color: var(--tx1); }
.card-title { font-size: 18px; font-weight: 800; color: var(--tx1);
  margin-bottom: 10px; }
.card-subtitle { font-size: 12px; color: var(--tx3); margin-top: -6px;
  margin-bottom: 10px; }
.quick-actions { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-bottom: 14px; }
.qa-btn { background: var(--card-bg); border: 2px solid var(--border); border-radius: 12px;
  padding: 14px 8px; cursor: pointer; text-align: center; color: var(--tx1);
  transition: all .2s; min-height: 80px; display: flex; flex-direction: column;
  align-items: center; justify-content: center; gap: 4px; }
.qa-btn:hover { border-color: var(--accent); transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0,0,0,0.3); }
.qa-btn:active { transform: translateY(0); }
.qa-btn.qa-fsd.active { border-color: var(--ok); background: rgba(74,222,128,0.1); }
.qa-btn.qa-danger { border-color: rgba(248,113,113,0.3); }
.qa-btn.qa-danger:hover { border-color: var(--err); }
.qa-icon { font-size: 24px; line-height: 1; }
.qa-label { font-size: 13px; font-weight: 700; }
.qa-status { font-size: 10px; color: var(--tx3); font-weight: 500; }
.exp-badge { display: inline-block; margin-left: 8px; padding: 2px 8px; border-radius: 999px; background: rgba(251,191,36,0.12); border: 1px solid rgba(251,191,36,0.35); color: var(--warn); font-size: 11px; font-weight: 800; vertical-align: middle; }
.status-triplet { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin: 10px 0 16px; }
.status-chip { background: var(--card-bg-alt); border: 1px solid var(--border); border-radius: 10px; padding: 10px; min-height: 54px; }
.status-chip .lbl { color: var(--tx3); font-size: 11px; margin-bottom: 4px; }
.status-chip .val { color: var(--tx1); font-size: 14px; font-weight: 800; overflow-wrap: anywhere; }
.s-ok { color: var(--ok) !important; }
.s-warn { color: var(--warn) !important; }
.s-err { color: var(--err) !important; }

/* === Stats Grid === */
.stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px;
  margin-bottom: 4px; }
.stat { background: var(--card-bg); border: 1px solid var(--border); border-radius: 8px; padding: 12px;
  text-align: center; box-shadow: 0 1px 3px rgba(0,0,0,0.2); }
.stat-lbl { font-size: 11px; color: var(--tx3); margin-bottom: 2px; font-weight: 500; }
.stat-val { font-size: 16px; font-weight: 700; }
.v-ok { color: var(--ok); }
.v-err { color: var(--err); }
.v-warn { color: var(--warn); }
.v-info { color: var(--info); }
.v-acc { color: var(--accent-light); }
.v-dim { color: var(--tx3); }

/* === Toggle Switch === */
.tgl { position: relative; display: inline-block; width: 52px; height: 28px; flex-shrink: 0; }
.tgl input { opacity: 0; width: 0; height: 0; }
.tgl-track { position: absolute; inset: 0; background: var(--border);
  border-radius: 12px; transition: background .2s; cursor: pointer; }
.tgl-track::after { content: ''; position: absolute; width: 24px; height: 24px;
  background: var(--tx3); border-radius: 50%; top: 2px; left: 2px;
  transition: transform .2s, background .2s; }
.tgl input:checked + .tgl-track { background: var(--accent); }
.tgl input:checked + .tgl-track::after { transform: translateX(24px); background: #fff; }

/* === Buttons === */
.btn { background: var(--accent); border: none; color: #fff; padding: 12px 20px;
  border-radius: 8px; font-size: 14px; cursor: pointer; font-weight: 600;
  transition: opacity .15s; min-height: 44px; }
.btn:hover { opacity: 0.85; }
.btn-danger { background: #dc2626; }
.btn-outline { background: transparent; border: 1px solid var(--border);
  color: var(--tx3); }
.btn-outline:hover { border-color: var(--accent-light); color: var(--accent-light); }
.btn-sm { padding: 8px 14px; font-size: 12px; min-height: 36px; }

/* === Selection Cards (HW, Profile, etc.) === */
.sel-cards { display: grid; gap: 16px; }
.sel-cards.c2 { grid-template-columns: repeat(2, 1fr); }
.sel-cards.c3 { grid-template-columns: repeat(3, 1fr); }
.sel-cards.c4 { grid-template-columns: repeat(4, 1fr); }
.sel-card { background: var(--card-bg-alt); border: 2px solid var(--border);
  border-radius: 12px; padding: 24px 14px; text-align: center; cursor: pointer;
  transition: border-color .15s, background .15s; }
.sel-card:hover { border-color: var(--tx3); }
.sel-card.active { border-color: var(--accent-light); background: rgba(124,58,237,0.18); color: var(--accent-light); }
.sel-card.active .sel-lbl { color: var(--accent-light); }
.sel-card .sel-lbl { font-size: 10px; color: var(--tx3); text-transform: uppercase;
  letter-spacing: 0.5px; }
.sel-card .sel-name { font-size: 20px; font-weight: 800; margin-top: 2px; }

/* === Drive Mode Cards (Phase 5A) === */
.drive-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; margin: 8px 0; }
.drive-card { background: var(--card-bg-alt); border: 2px solid var(--border); border-radius: 12px;
  padding: 16px 10px; text-align: center; cursor: pointer; transition: all 0.2s; }
.drive-card:hover { border-color: var(--tx3); transform: translateY(-2px); }
.drive-card.active { border-color:#38bdf8; background:linear-gradient(160deg, rgba(14,165,233,.20), rgba(99,102,241,.16)); box-shadow:0 6px 16px rgba(14,165,233,.18); }
.drive-card .drive-icon { font-size: 28px; margin-bottom: 6px; }
.drive-card .drive-name { font-size: 16px; font-weight: 700; color: var(--tx1); }
.drive-card .drive-desc { font-size: 11px; color: var(--tx3); margin-top: 4px; line-height: 1.3; }
@media(max-width:768px) {
  .drive-grid { grid-template-columns: repeat(2, 1fr); gap: 8px; }
  .drive-card { padding: 12px 6px; }
  .drive-card .drive-icon { font-size: 22px; }
  .drive-card .drive-name { font-size: 14px; }
  .drive-card .drive-desc { font-size: 10px; }
}

/* === Setting Row === */
.setting-row { display: flex; justify-content: space-between; align-items: center;
  padding: 12px 0; border-bottom: 1px solid rgba(75,85,99,0.3); }
.setting-row:last-child { border-bottom: none; }
.setting-name { font-size: 14px; color: var(--tx1); font-weight: 600; }
.setting-desc { font-size: 12px; color: var(--tx3); margin-top: 2px; }

/* === Sub-tabs (CAN tools) === */
.sub-tabs { display: flex; gap: 2px; background: var(--card-bg);
  border-radius: 6px; padding: 2px; margin-bottom: 10px; }
.sub-tab { flex: 1; text-align: center; padding: 8px 12px;
  border-radius: 4px; font-size: 13px; cursor: pointer;
  color: var(--tx3); transition: all .15s; font-weight: 600; }
.sub-tab.active { background: var(--accent); color: #fff; font-weight: 700; }

/* === Table === */
.tbl { width: 100%; border-collapse: collapse; font-size: 13px; table-layout: fixed; }
.tbl th { text-align: left; padding: 8px 10px; color: var(--tx3);
  border-bottom: 1px solid var(--border); font-weight: 500; font-size: 11px; }
.tbl td { padding: 6px 10px; border-bottom: 1px solid rgba(75,85,99,0.2);
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tbl .hex { font-family: 'SF Mono', 'Courier New', monospace; color: var(--accent-light); }
.tbl-wrap { overflow-x: auto; -webkit-overflow-scrolling: touch; }
select.inp { font-size: 16px; }

/* === Inputs === */
.inp { background: var(--card-bg-alt); border: 1px solid var(--border);
  border-radius: 8px; padding: 10px 14px; color: var(--tx1); font-size: 16px;
  width: 100%; outline: none; max-width: 100%; box-sizing: border-box; }
.inp:focus { border-color: var(--accent-light); }
.inp::placeholder { color: var(--tx3); }
textarea.inp { resize: vertical; min-height: 60px; font-family: monospace;
  font-size: 11px; line-height: 1.5; }

/* === Status Indicator === */
.status-dot { width: 10px; height: 10px; border-radius: 50%; display: inline-block; }
.status-dot.ok { background: var(--ok); box-shadow: 0 0 6px var(--ok); }
.status-dot.err { background: var(--err); }
.status-dot.warn { background: var(--warn); }

/* === Mobile Bottom Tab Bar === */
.mob-tabs { display: none; position: fixed; bottom: 0; left: 0; right: 0;
  z-index: 200; background: var(--sidebar-bg); border-top: 1px solid var(--border);
  padding: 2px 0; padding-bottom: env(safe-area-inset-bottom, 0px);
  flex-shrink: 0; }
.mob-tab { display: flex; flex-direction: column; align-items: center;
  justify-content: center; padding: 6px 0; color: var(--tx3); background: transparent; border: 0;
  font-size: 10px; cursor: pointer; flex: 1; -webkit-tap-highlight-color: transparent; font-weight: 600; }
.mob-tab.active { color: var(--accent-light); }
.mob-tab .mob-icon { font-size: 22px; line-height: 1; margin-bottom: 1px; }
.mob-more-panel { display: none; position: fixed; bottom: 52px; left: 0; right: 0;
  z-index: 210; background: var(--sidebar-bg); border-top: 1px solid var(--border);
  border-radius: 12px 12px 0 0; padding: 12px 16px; max-height: 60vh; overflow-y: auto; }
.mob-more-panel.open { display: block; }
.mob-more-item { display: block; padding: 10px 0; color: var(--tx2);
  font-size: 13px; border-bottom: 1px solid rgba(75,85,99,0.3); cursor: pointer; }
.mob-more-item:last-child { border-bottom: none; }
.mob-more-item.active { color: var(--accent-light); }
.mob-more-close { position: absolute; top: 8px; right: 12px; color: var(--tx3);
  font-size: 18px; cursor: pointer; }
.mobile-page { display: none; }
.mobile-card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 14px;
  padding: 14px; margin: 10px 12px; color: var(--tx1); }
.mobile-card h3 { font-size: 16px; margin: 0 0 10px; }
.mobile-card label { display: flex; align-items: center; justify-content: space-between; gap: 10px; margin: 10px 0; }
.mobile-card input, .mobile-card select, .mobile-card textarea { background: var(--card-bg-alt); border: 1px solid var(--border);
  color: var(--tx1); border-radius: 10px; padding: 10px; max-width: 100%; }
.mobile-card textarea { width: 100%; display: block; margin-bottom: 10px; }
.mobile-card button { border: 0; border-radius: 10px; padding: 10px 14px; background: var(--accent); color: #fff; font-weight: 800; }

/* === Mobile Responsive === */
@media (max-width: 1024px) {
  body { font-size: 14px; }
  .sidebar { display: none !important; }
  .overlay { display: none !important; }
  .mobile-toggle { display: none !important; }
  .mobile-theme-toggle { display: inline-flex; align-items: center; justify-content: center; order: 7; box-shadow: 0 1px 4px rgba(0,0,0,0.18); }
  .mob-tabs { display: flex; min-height: 68px; padding: 6px 0 calc(6px + env(safe-area-inset-bottom, 0px)); transform: translateZ(0); -webkit-backface-visibility: hidden; }
  .mob-tab { flex: 1 1 0; min-width: 0; min-height: 58px; padding: 7px 2px; font-size: 12px; gap: 2px; overflow: hidden; }
  .mob-tab .mob-icon { font-size: 29px; margin-bottom: 2px; }
  .mob-more-panel { bottom: 72px; padding: 16px 18px; }
  .mob-more-item { padding: 14px 0; font-size: 15px; }
  .mob-more-close { font-size: 22px; }
  .main { width: 100%; padding-bottom: 74px; }
  .mobile-page.active:not(#mob-home-page) { display: block; position: fixed; top: 54px; bottom: 74px; left: 0; right: 0;
    z-index: 160; background: var(--main-bg); overflow-y: auto; padding: 8px 0; }
  .topbar { padding: 7px 8px; gap: 5px; min-height: 0; flex-wrap: wrap; align-content: center; }
  .topbar-dot { width: 7px; height: 7px; }
  .topbar-fps { font-size: 11px; padding: 5px 8px; max-width: 72px; overflow: hidden; white-space: nowrap; }
  .topbar-badge { font-size: 11px; padding: 5px 8px; white-space: nowrap; }
  .topbar-exp { order: 9; flex: 1 0 100%; text-align: center; font-size: 11px; padding: 4px 8px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .topbar-time { order: 8; font-size: 11px; margin-left: auto; padding-right: 2px; white-space: nowrap; }
  .content { padding: 8px 10px 12px; }
  .card { padding: 12px 10px; margin-bottom: 10px; border-radius: 8px; }
  .card-title { font-size: 15px; margin-bottom: 8px; }
  .stats { grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 6px; }
  .stat { padding: 9px 6px; min-width: 0; }
  .stat-lbl { font-size: 10px; }
  .stat-val { font-size: 15px; overflow-wrap: anywhere; }
  .sel-cards.c2, .sel-cards.c3, .sel-cards.c4 { grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 6px; }
  .sel-card { padding: 13px 7px; min-width: 0; }
  .sel-card .sel-name { font-size: 17px; overflow-wrap: anywhere; }
  .setting-row { flex-wrap: wrap; gap: 4px; padding: 10px 0; }
  .setting-name { font-size: 13px; }
  .tbl { font-size: 11px; }
  .tbl th { padding: 6px 6px; font-size: 10px; }
  .tbl td { padding: 4px 6px; font-size: 11px; }
  .btn { padding: 10px 16px; font-size: 13px; min-height: 42px; }
  .btn-sm { padding: 6px 10px; font-size: 11px; min-height: 34px; }
  .big-toggle { padding: 18px 10px; }
  .big-toggle .toggle-visual { width: 70px; height: 36px; }
  .big-toggle .toggle-visual .thumb { width: 32px; height: 32px; }
  .big-toggle.on .toggle-visual .thumb { left: 36px; }
  .big-toggle .toggle-label { font-size: 16px; }
  .sub-tabs { gap: 1px; padding: 2px; }
  .sub-tab { padding: 6px 8px; font-size: 12px; }
  .diag-grid { grid-template-columns: 1fr 1fr; gap: 3px; }
  .diag-item { padding: 4px 6px; font-size: 11px; }
  .upload-area { padding: 14px 8px; }
  .quick-actions { gap: 6px; }
  .qa-btn { padding: 10px 4px; min-height: 68px; }
  .qa-icon { font-size: 20px; }
  .qa-label { font-size: 12px; }
  .page-title { font-size: 20px; margin: 2px 0 10px; padding-bottom: 8px; }
  .status-triplet { grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 6px; margin: 6px 0 10px; }
  .status-triplet .status-chip { min-height: 46px; padding: 8px; }
  .status-triplet .status-chip:nth-child(3) { grid-column: 1 / -1; }
  .status-chip .lbl { font-size: 10px; margin-bottom: 3px; }
  .status-chip .val { font-size: 13px; }
  .diag-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 5px; }
  .diag-item { min-width: 0; align-items: center; }
  .diag-item span:last-child { overflow-wrap: anywhere; text-align: right; }
}
.mobile-toggle { display: none; background: none; border: none;
  color: var(--tx2); font-size: 20px; cursor: pointer; padding: 4px 8px; }
.overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.5);
  z-index: 150; }
.overlay.active { display: block; }

/* === Atlas driving-first redesign === */
.cockpit-shell { position: relative; overflow: hidden; border: 1px solid var(--cockpit-stroke); border-radius: 30px; padding: 18px; background: radial-gradient(circle at 14% 0%, rgba(124,58,237,.20), transparent 34%), radial-gradient(circle at 88% 12%, rgba(56,189,248,.14), transparent 32%), linear-gradient(145deg, rgba(2,6,23,.84), rgba(8,13,24,.64)); box-shadow: var(--cockpit-shadow); }
.cockpit-ambient { position: absolute; inset: 0; pointer-events: none; background-image: linear-gradient(rgba(148,163,184,.045) 1px, transparent 1px), linear-gradient(90deg, rgba(148,163,184,.035) 1px, transparent 1px); background-size: 44px 44px; mask-image: radial-gradient(circle at center, black, transparent 74%); opacity: .82; }
.cockpit-home { position: relative; z-index: 1; display: grid; gap: 16px; }
.cockpit-status-bar { display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 10px; }
.cockpit-title small { display: block; color: var(--cockpit-muted); font-size: 11px; font-weight: 900; letter-spacing: .14em; text-transform: uppercase; }
.cockpit-title h2 { margin: 4px 0 0; font-size: 32px; line-height: .98; letter-spacing: -0.05em; }
.cockpit-chip-row { display: flex; flex-wrap: wrap; gap: 8px; justify-content: flex-end; }
.cockpit-chip { border: 1px solid var(--cockpit-stroke); border-radius: 999px; padding: 8px 12px; background: rgba(15,23,42,.62); color: var(--tx3); font-size: 12px; font-weight: 850; backdrop-filter: var(--cockpit-card-blur); }
.cockpit-chip.ok { color: var(--ok); border-color: rgba(74,222,128,.35); background: rgba(74,222,128,.09); }
.cockpit-chip.warn { color: var(--warn); border-color: rgba(251,191,36,.35); background: rgba(251,191,36,.09); }
.cockpit-grid { display: grid; grid-template-columns: minmax(0, 1.05fr) minmax(360px, .95fr); gap: 16px; }
.cockpit-home-main { display: grid; gap: 12px; }
.cockpit-primary { min-height: 360px; border: 1px solid var(--cockpit-stroke-strong); border-radius: var(--cockpit-radius-xl); padding: 24px; background: radial-gradient(circle at 86% 0%, rgba(124,58,237,.24), transparent 34%), linear-gradient(145deg, rgba(15,23,42,.90), rgba(8,13,24,.70)); box-shadow: var(--cockpit-shadow), var(--cockpit-glow-purple); display: grid; grid-template-rows: 1fr auto; gap: 18px; overflow: hidden; backdrop-filter: var(--cockpit-card-blur); }
.cockpit-state-panel { min-height: 230px; display: flex; flex-direction: column; align-items: center; justify-content: center; text-align: center; }
.cockpit-state-label { color: var(--cockpit-muted); font-size: 12px; font-weight: 950; letter-spacing: .16em; text-transform: uppercase; }
.cockpit-state { width: 100%; font-size: clamp(86px, 12vw, 152px); line-height: .78; font-weight: 950; letter-spacing: -.075em; margin: 18px 0 12px; color: var(--err); text-align: center; text-shadow: var(--cockpit-glow-danger); }
.cockpit-state.on { color: var(--ok); text-shadow: var(--cockpit-glow-ok); }
.cockpit-state-desc { color: var(--tx2); font-size: 14px; line-height: 1.55; max-width: 540px; margin: 0 auto; }
.cockpit-hold { width: 100%; min-height: 72px; margin-top: 18px; border: none; border-radius: 24px; background: linear-gradient(135deg, #991b1b, #ef4444); color: white; font-size: 16px; font-weight: 950; letter-spacing: .045em; cursor: pointer; box-shadow: 0 18px 42px rgba(239,68,68,.24); }
.cockpit-hold.on { background: linear-gradient(135deg, #047857, #22c55e); box-shadow: 0 18px 42px rgba(34,197,94,.22); }
.cockpit-metrics { display: grid; grid-template-columns: repeat(4, minmax(0,1fr)); gap: 10px; }
.cockpit-metric { min-height: 96px; border: 1px solid var(--cockpit-stroke); border-radius: var(--cockpit-radius-lg); background: rgba(15,23,42,.58); padding: 14px; backdrop-filter: var(--cockpit-card-blur); }
.cockpit-metric .lbl { color: var(--cockpit-muted); font-size: 10px; font-weight: 900; letter-spacing: .09em; text-transform: uppercase; }
.cockpit-metric .val { margin-top: 8px; color: var(--tx1); font-size: 24px; font-weight: 900; letter-spacing: -0.045em; overflow-wrap: anywhere; }
.cockpit-metric .hint { margin-top: 4px; color: var(--cockpit-dim); font-size: 11px; }
.cockpit-vehicle { border: 1px solid var(--cockpit-stroke); border-radius: var(--cockpit-radius-xl); background: linear-gradient(145deg, rgba(15,23,42,.60), rgba(8,13,24,.82)); padding: 20px; box-shadow: var(--cockpit-shadow), var(--cockpit-glow-cyan); backdrop-filter: var(--cockpit-card-blur); }
.cockpit-vehicle-visual { position: relative; min-height: 220px; display: grid; place-items: center; margin: 14px 0; border-radius: 24px; overflow: hidden; background: radial-gradient(circle at 50% 45%, rgba(56,189,248,.17), transparent 38%), linear-gradient(180deg, rgba(2,6,23,.30), rgba(2,6,23,.72)); }
.vehicle-lane { position: absolute; inset: 0; background: linear-gradient(90deg, transparent 18%, rgba(56,189,248,.20) 50%, transparent 82%); clip-path: polygon(43% 0, 57% 0, 82% 100%, 18% 100%); opacity: .72; }
.vehicle-silhouette { position: relative; width: 46%; min-width: 180px; height: 132px; border-radius: 42% 42% 28% 28%; background: linear-gradient(145deg, rgba(226,232,240,.94), rgba(100,116,139,.22)); box-shadow: 0 24px 70px rgba(56,189,248,.18); }
.vehicle-silhouette::before { content: ''; position: absolute; inset: 18px 32px 54px; border-radius: 999px 999px 18px 18px; background: linear-gradient(90deg, rgba(15,23,42,.86), rgba(56,189,248,.22)); }
.vehicle-silhouette::after { content: ''; position: absolute; left: 18%; right: 18%; bottom: 18px; height: 10px; border-radius: 999px; background: rgba(56,189,248,.72); box-shadow: 0 0 24px rgba(56,189,248,.45); }
.cockpit-mode-grid { display: grid; grid-template-columns: repeat(3, minmax(0,1fr)); gap: 10px; margin-top: 14px; }
.cockpit-mode { border: 1px solid var(--cockpit-stroke); border-radius: 18px; background: rgba(15,23,42,.55); padding: 14px; }
.cockpit-mode.active { border-color: rgba(124,58,237,.58); background: rgba(124,58,237,.16); }
.cockpit-mode strong { display: block; margin-top: 5px; font-size: 16px; }
.cockpit-mode span { display: block; margin-top: 4px; color: var(--tx3); font-size: 11px; line-height: 1.35; }
.cockpit-safety-strip, .cockpit-warning { border: 1px solid rgba(251,191,36,.35); border-radius: 18px; background: rgba(251,191,36,.08); padding: 14px; color: var(--tx2); font-size: 13px; line-height: 1.45; }
.mobile-remote { display: none; }
.mobile-app-shell { border-radius: 28px; padding: 16px; background: linear-gradient(180deg, #f8fafc, #e9eef7); color: #111827; box-shadow: 0 18px 50px rgba(0,0,0,.22); }
.mobile-app-shell .cockpit-title small { color: #64748b; }
.mobile-app-shell .cockpit-title h2 { color: #0f172a; }
.mobile-app-shell .cockpit-chip { background: #fff; color: #334155; border-color: #e2e8f0; }
.mobile-primary-card { margin-top: 14px; border-radius: 30px; padding: 22px; color: white; background: radial-gradient(circle at 86% 0%, rgba(56,189,248,.34), transparent 34%), linear-gradient(145deg, #111827, #312e81); box-shadow: 0 18px 42px rgba(49,46,129,.28); }
.mobile-primary-card .cockpit-state-label { color: #c4b5fd; }
.mobile-primary-card .mobile-state { font-size: 74px; line-height: .86; font-weight: 950; letter-spacing: -.1em; margin: 14px 0 4px; color: #ffb4bd; text-shadow: 0 0 24px rgba(255,180,189,.24); }
.mobile-primary-card .mobile-state.on { color: #adffc8; text-shadow: 0 0 24px rgba(173,255,200,.26); }
.mobile-primary-card p { color: #cbd5e1; font-size: 13px; line-height: 1.45; margin: 0; }
.mobile-hold { width: 100%; min-height: 58px; margin-top: 14px; border: none; border-radius: 22px; background: #ef4444; color: white; font-weight: 950; letter-spacing: .04em; box-shadow: 0 12px 26px rgba(239,68,68,.25); }
.mobile-hold.on { background: #16a34a; box-shadow: 0 12px 26px rgba(22,163,74,.24); }
.mobile-remote-grid { display: grid; grid-template-columns: repeat(2, minmax(0,1fr)); gap: 10px; margin-top: 12px; }
.mobile-remote-card { background: white; border: 1px solid #e2e8f0; border-radius: 22px; padding: 14px; min-height: 86px; }
.mobile-remote-card .lbl { color: #64748b; font-size: 10px; font-weight: 900; letter-spacing: .08em; text-transform: uppercase; }
.mobile-remote-card .val { margin-top: 7px; color: #0f172a; font-size: 21px; font-weight: 900; overflow-wrap: anywhere; }
.mobile-risk-note, .mobile-alert-card { margin-top: 12px; border-radius: 22px; padding: 14px; background: #fff7ed; border: 1px solid #fed7aa; color: #9a3412; font-size: 13px; line-height: 1.45; }
/* === AP 注入安全卡（驾驶舱风格，仅显示层，服务端强制不变） === */
.cockpit-card { background: radial-gradient(120% 60% at 50% 0%, rgba(56,189,248,.08), transparent 60%), linear-gradient(160deg,#0f172a,#0b1220); border:1px solid var(--ds-card-stroke); }
.ck-head { display:flex; align-items:center; justify-content:space-between; margin-bottom:14px; }
.ck-head h3 { margin:0; font-size:17px; color:var(--tx1); display:flex; align-items:center; gap:9px; }
.ck-chip { font-size:10.5px; padding:4px 11px; border-radius:999px; background:rgba(148,163,184,.15); color:var(--tx2); border:1px solid var(--ds-card-stroke); }
.ck-chip.ok { background:rgba(52,211,153,.14); color:#6ee7b7; border-color:rgba(52,211,153,.3); }
.state-panel { background:rgba(255,255,255,.04); border:1px solid var(--ds-card-stroke); border-radius:13px; padding:15px; text-align:center; margin-bottom:14px; }
.state-panel.active { border-color:rgba(52,211,153,.4); background:rgba(52,211,153,.06); }
.sp-label { font-size:10px; color:var(--cockpit-muted); letter-spacing:.06em; }
.sp-val { font-size:24px; font-weight:700; color:var(--tx3); margin:6px 0; }
.state-panel.active .sp-val { color:#34d399; }
.sp-desc { font-size:10.5px; color:var(--cockpit-dim); }
.gate-bar { height:5px; background:rgba(52,211,153,.15); border-radius:999px; margin-top:9px; overflow:hidden; }
.gate-bar i { display:block; height:100%; width:0; background:linear-gradient(90deg,#34d399,#10b981); border-radius:999px; transition:width .2s; }
.src-row { display:flex; align-items:center; justify-content:space-between; background:rgba(0,0,0,.2); border:1px solid var(--ds-card-stroke); border-radius:10px; padding:10px 14px; margin-bottom:14px; font-size:12px; }
.src-row b { color:var(--tx1); }
.ctl-section { font-size:11px; color:var(--ds-cyan); letter-spacing:.06em; margin:6px 0 10px; }
.ctl-row { display:flex; align-items:center; justify-content:space-between; background:rgba(255,255,255,.03); border:1px solid var(--ds-card-stroke); border-radius:11px; padding:11px 15px; margin-bottom:9px; }
.ctl-row .cn { font-size:13px; color:var(--tx1); }
.ctl-row .cd { font-size:10.5px; color:var(--cockpit-dim); margin-top:2px; }
.safety-strip { margin-top:14px; background:rgba(245,158,11,.08); border:1px solid rgba(245,158,11,.25); border-radius:10px; padding:11px 15px; font-size:11.5px; color:#fcd34d; line-height:1.6; }
/* === .tgl 开关（AP 门控/自动恢复，仅显示皮，input id/checked 不变） === */
.tgl { position:relative; display:inline-block; width:40px; height:22px; }
.tgl input { opacity:0; width:100%; height:100%; position:absolute; margin:0; cursor:pointer; }
.tgl-track { position:absolute; inset:0; background:rgba(148,163,184,.3); border-radius:999px; transition:.2s; }
.tgl input:checked + .tgl-track { background:linear-gradient(90deg,#0ea5e9,#6366f1); }
.tgl-track::after { content:""; position:absolute; top:2px; left:2px; width:18px; height:18px; border-radius:50%; background:#fff; transition:.2s; }
.tgl input:checked + .tgl-track::after { transform:translateX(18px); }
/* === 插件管理卡（Task 5，驾驶舱风格换皮） === */
.install-grid { display:grid; grid-template-columns:repeat(3,1fr); gap:10px; margin-bottom:14px; }
.install-cell { background:rgba(255,255,255,.04); border:1px solid var(--ds-card-stroke); border-radius:12px; padding:13px 12px; text-align:center; }
.install-cell .ii { font-size:22px; margin-bottom:6px; }
.install-cell .it { font-size:11.5px; color:var(--tx1); margin-bottom:6px; font-weight:600; }
.mock-input { background:rgba(0,0,0,.25); border:1px solid var(--ds-card-stroke); border-radius:7px; padding:6px 8px; font-size:11px; color:var(--tx2); width:100%; margin-bottom:6px; box-sizing:border-box; }
.ds-btn { display:inline-block; background:var(--ds-accent-grad); color:#fff; font-size:11px; padding:5px 12px; border-radius:7px; border:none; cursor:pointer; }
.ds-btn-muted { font-size:10px; color:var(--cockpit-dim); }
.ds-select { background:rgba(0,0,0,.25); border:1px solid var(--ds-card-stroke); border-radius:7px; color:var(--tx1); padding:6px; }
.plugin-list { display:grid; gap:8px; }
.collapse { margin-top:14px; background:rgba(255,255,255,.03); border:1px solid var(--ds-card-stroke); border-radius:10px; padding:10px 14px; font-size:11.5px; color:var(--cockpit-dim); }
/* === 已安装插件列表项（renderPluginsStatus，套驾驶舱设计系统） === */
.plugin-item { background:rgba(255,255,255,.03); border:1px solid var(--ds-card-stroke); border-radius:11px; padding:11px 15px; }
.plugin-item .pi-head { display:flex; align-items:center; justify-content:space-between; gap:10px; flex-wrap:wrap; }
.plugin-item .pi-name { font-weight:600; color:var(--tx1); font-size:13px; }
.plugin-item .pi-ver { color:var(--cockpit-dim); font-size:10.5px; }
.plugin-item .pi-controls { display:flex; align-items:center; gap:10px; }
.plugin-item .pi-prio { width:56px; margin-bottom:0; text-align:center; }
.plugin-item .pi-hint { font-size:10.5px; color:var(--cockpit-dim); margin-top:6px; }
.plugin-item .pi-warn { font-size:10.5px; color:var(--warn); margin-top:4px; }
.plugin-item .pi-details { margin-top:8px; }
.plugin-item .pi-details > pre { margin:6px 0 0; background:rgba(0,0,0,.25); border:1px solid var(--ds-card-stroke); border-radius:7px; padding:8px; font-size:10px; color:var(--tx2); overflow:auto; }
.ds-btn-danger { background:linear-gradient(135deg,#991b1b,#ef4444); }
.diag-shell { display: grid; grid-template-columns: 260px minmax(0,1fr); gap: 14px; }
.diag-console { align-items: start; }
.diag-kpi-rail { display: grid; gap: 10px; }
.diag-kpi { border: 1px solid var(--border); border-radius: 16px; background: var(--card-bg); padding: 13px; }
.diag-kpi-rail .diag-kpi { min-height: 92px; }
.diag-kpi .lbl { color: var(--tx3); font-size: 10px; font-weight: 900; letter-spacing: .08em; text-transform: uppercase; }
.diag-kpi .val { margin-top: 7px; font-size: 22px; font-weight: 900; overflow-wrap: anywhere; }
.diag-main-panel { min-width: 0; }
.diag-toolbar { display: flex; gap: 8px; margin-bottom: 10px; }
.diag-toolbar .inp { flex: 1; }
.diag-table-shell { max-height: 390px; overflow: auto; border: 1px solid var(--cockpit-stroke); border-radius: 20px; background: rgba(2,6,23,.42); }
.diag-priority-row { display: grid; grid-template-columns: 96px 62px 54px minmax(220px, 1fr) 76px; gap: 10px; align-items: center; padding: 10px 12px; border-bottom: 1px solid rgba(148,163,184,.11); font-family: 'SF Mono', 'Courier New', monospace; font-size: 12px; }
.diag-priority-row.header { background: rgba(15,23,42,.86); color: var(--tx3); font-family: -apple-system, 'SF Pro Text', 'Helvetica Neue', sans-serif; font-size: 10px; font-weight: 900; letter-spacing: .09em; text-transform: uppercase; position: sticky; top: 0; z-index: 1; }
.diag-priority-row.warn { background: rgba(251,191,36,.08); }
.diag-priority-row.err { background: rgba(248,113,113,.09); }
.diag-timeline { margin-top: 14px; border: 1px solid var(--cockpit-stroke); border-radius: 18px; padding: 14px; background: rgba(15,23,42,.48); }
.diag-timeline .label { color: var(--cockpit-muted); font-size: 10px; font-weight: 900; letter-spacing: .10em; text-transform: uppercase; }
.diag-bars { display: grid; gap: 8px; margin-top: 10px; }
.diag-bar { height: 9px; border-radius: 999px; overflow: hidden; background: rgba(15,23,42,.90); }
.diag-bar span { display: block; height: 100%; border-radius: inherit; background: linear-gradient(90deg, var(--accent), var(--info)); }
.diag-row-warn { background: rgba(251,191,36,.08); }
.diag-row-error { background: rgba(248,113,113,.10); }
@media (max-width: 900px) {
  .cockpit-grid { grid-template-columns: 1fr; }
  .cockpit-metrics { grid-template-columns: repeat(2, minmax(0,1fr)); }
  .diag-shell { grid-template-columns: 1fr; }
}
@media (max-width: 768px) {
  .desktop-home { display: none; }
  .mobile-remote { display: block; }
  .cockpit-title h2 { font-size: 22px; }
  .cockpit-status-bar { align-items: flex-start; }
  .cockpit-chip-row { width: 100%; }
  .cockpit-primary { min-height: 0; padding: 18px; border-radius: 22px; }
  .cockpit-state-panel { min-height: 160px; }
  .cockpit-state { font-size: 62px; }
  .cockpit-hold { min-height: 56px; border-radius: 18px; font-size: 14px; }
  .cockpit-mode-grid { grid-template-columns: 1fr; }
  .diag-toolbar { flex-direction: column; }
  .diag-priority-row { grid-template-columns: 78px 46px 42px minmax(150px, 1fr) 54px; gap: 6px; font-size: 11px; padding: 9px 10px; }
}

/* === Scrollbar === */
::-webkit-scrollbar { width: 6px; }
::-webkit-scrollbar-track { background: var(--main-bg); }
::-webkit-scrollbar-thumb { background: var(--border); border-radius: 3px; }

/* === Big Toggle Card === */
.big-toggle { text-align: center; padding: 24px; }
.big-toggle .toggle-visual { width: 80px; height: 40px; border-radius: 20px;
  margin: 0 auto 8px; position: relative; transition: background .3s;
  box-shadow: 0 0 20px rgba(0,0,0,0.3); }
.big-toggle .toggle-visual .thumb { width: 36px; height: 36px; border-radius: 50%;
  background: #fff; position: absolute; top: 2px; transition: left .3s; }
.big-toggle .toggle-label { font-size: 18px; font-weight: 700; }
.big-toggle.on .toggle-visual { animation: pulse-ok 2s ease-in-out infinite; }
@keyframes pulse-ok {
  0%,100% { box-shadow: 0 0 10px rgba(74,222,128,0.3); }
  50% { box-shadow: 0 0 25px rgba(74,222,128,0.6); }
}
.big-toggle.off .toggle-visual { animation: pulse-err 2.5s ease-in-out infinite; }
@keyframes pulse-err {
  0%,100% { box-shadow: 0 0 8px rgba(248,113,113,0.2); }
  50% { box-shadow: 0 0 18px rgba(248,113,113,0.4); }
}
.big-toggle.on .toggle-visual { background: var(--ok);
  box-shadow: 0 0 20px rgba(74,222,128,0.3); }
.big-toggle.on .toggle-visual .thumb { left: 42px; }
.big-toggle.off .toggle-visual { background: var(--err);
  box-shadow: 0 0 20px rgba(248,113,113,0.3); }
.big-toggle.off .toggle-visual .thumb { left: 2px; }

/* === Card Hover (desktop) === */
@media (min-width: 769px) {
  .card { transition: transform .2s, box-shadow .2s; }
  .card:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(0,0,0,0.25); }
}

/* === Upload Area === */
.upload-area { border: 2px dashed var(--border); border-radius: 10px;
  padding: 20px; text-align: center; cursor: pointer;
  transition: border-color .2s; }
.upload-area:hover { border-color: var(--accent-light); }

/* === Diag Grid === */
.diag-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 4px; }
.diag-item { background: var(--card-bg-alt); border-radius: 4px;
  padding: 4px 8px; display: flex; justify-content: space-between;
  font-size: 11px; }
.diag-item .lbl { color: var(--tx3); }
.hero-status { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin-bottom: 20px; }
.feature-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 14px; }
.mode-note { margin-top: 10px; color: var(--tx3); font-size: 12px; line-height: 1.45; }
.cap-hidden{display:none!important}
.mobile-home { display: none; }
@media (max-width: 768px) {
  .hero-status, .feature-grid { grid-template-columns: 1fr 1fr; }
  .nav-item { font-size: 16px; }
  .nav-item.active { font-size: 17px; }
  .mobile-home { display: block; }
  #pg-overview > .hero-status,
  #pg-overview > .quick-actions,
  #pg-overview > .card { display: none; }
  #pg-overview .page-title { margin-bottom: 8px; }
  #pg-overview > .status-triplet { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  #pg-overview > .status-triplet .status-chip:nth-child(3) { grid-column: 1 / -1; }
}
</style>
</head>
<body>
<div class="toast" id="toast"></div>

<!-- Mobile overlay -->
<div class="overlay" id="overlay" onclick="closeSidebar()"></div>

<!-- Sidebar -->
<nav class="sidebar" id="sidebar">
  <div class="sidebar-hdr">
    <h1 data-product-name data-single-text="Atlas Single CAN">Atlas T-2CAN</h1>
    <p>控制面板</p>
  </div>
  <div class="sidebar-nav">
    <div class="nav-item active" data-page="pg-overview"><span class="nav-icon">▣</span>驾驶状态</div>
    <div class="nav-item" data-page="pg-hardware"><span class="nav-icon">◇</span>硬件模式</div>
    <div class="nav-item" data-page="pg-drive"><span class="nav-icon">◉</span>驾驶风格</div>
    <div class="nav-item" data-page="pg-speed"><span class="nav-icon">↗</span>速度策略</div>
    <div class="nav-item" data-page="pg-bus2" data-cap="can2" data-single-hide="1"><span class="nav-icon">✦</span>CAN2 控制</div>
    <div class="nav-item" data-page="pg-strobe" data-cap="lighting" data-single-hide="1"><span class="nav-icon">⚡</span>灯光特技</div>
    <div class="nav-item" data-page="pg-defense"><span class="nav-icon">◈</span>FSD 防护</div>
    <div class="nav-item" data-page="pg-ota"><span class="nav-icon">⇧</span>OTA 升级</div>
    <div class="nav-item" data-page="pg-network"><span class="nav-icon">◎</span>网络设置</div>
    <div class="nav-item" data-page="pg-can"><span class="nav-icon">⌘</span>CAN 诊断</div>
    <div class="nav-item" data-page="pg-shift" data-single-hide="1"><span class="nav-icon">⚙</span>自动换挡</div>
  </div>
  <div class="sidebar-ft">
    <button onclick="toggleLanguage()" id="lang-btn">EN</button>
    <button onclick="toggleTheme()" id="theme-btn">☀</button>
  </div>
</nav>

<!-- Main Area -->
<div class="main">
  <!-- Top Status Bar -->
  <div class="topbar">
    <button class="mobile-toggle" onclick="openSidebar()">☰</button>
    <span class="topbar-fps" id="tb-fps">0.0 Hz</span>
    <span class="topbar-dot ok" id="tb-dot-status"></span>
    <span class="topbar-badge badge-ok" id="tb-status">已连接</span>
    <span class="topbar-dot warn" id="tb-dot-fsd"></span>
    <span class="topbar-badge badge-warn" id="tb-fsd">FSD 防护状态</span>
    <span class="topbar-exp" id="tb-exp">实验项 0</span>
    <button class="mobile-theme-toggle" onclick="toggleTheme()" id="mobile-theme-btn">☀ 白天</button>
    <span class="topbar-time" id="tb-up">00:00:00</span>
  </div>

  <!-- Content Area -->
  <div class="content" id="content">

    <!-- Page 1: Overview -->
    <div class="page active" id="pg-overview">
      <div class="desktop-home">
        <div class="cockpit-shell">
          <div class="cockpit-ambient"></div>
          <div class="cockpit-home">
        <div class="cockpit-status-bar">
          <div class="cockpit-title">
            <small data-product-name data-single-text="Atlas Single CAN">Atlas T-2CAN</small>
            <h2>驾驶状态中心</h2>
          </div>
          <div class="cockpit-chip-row">
            <span class="cockpit-chip" id="st-module-ui">UI 待同步</span>
            <span class="cockpit-chip" id="st-module-nvs">NVS 读取中</span>
            <span class="cockpit-chip" id="st-module-run">运行状态待检测</span>
          </div>
        </div>

        <div class="cockpit-grid">
          <div class="cockpit-home-main">
            <div class="cockpit-primary">
              <div class="cockpit-state-panel">
                <div class="cockpit-state-label">FSD 注入</div>
                <div class="cockpit-state" id="fsd-label">OFF</div>
                <div class="cockpit-state-desc" id="ov-fsd-desc">模块总开关关闭；启用前请确认车辆处于安全状态。</div>
              </div>
              <button class="cockpit-hold" id="fsd-toggle" onclick="toggleFsd()">长按 2 秒启用 FSD 注入</button>
            </div>

            <div class="cockpit-metrics">
              <div class="cockpit-metric"><div class="lbl">硬件模式</div><div class="val" id="ov-hw">Auto</div><div class="hint">自动识别</div></div>
              <div class="cockpit-metric"><div class="lbl">驾驶风格</div><div class="val" id="m-drive">Normal</div><div class="hint">均衡响应</div></div>
              <div class="cockpit-metric"><div class="lbl">速度策略</div><div class="val" id="m-speed">auto</div><div class="hint">自动偏移</div></div>
              <div class="cockpit-metric"><div class="lbl">运行时间</div><div class="val" id="ov-up">00:00:00</div><div class="hint" id="m-up">设备在线</div></div>
            </div>

            <div class="cockpit-metrics">
              <div class="cockpit-metric"><div class="lbl">CAN1</div><div class="val" id="s-can">Offline</div><div class="hint">--</div></div>
              <div class="cockpit-metric" id="ov-ap-metric"><div class="lbl">AP 激活</div><div class="val" id="ov-ap">等待</div><div class="hint" id="ov-ap-hint">门控未触发</div></div>
              <div class="cockpit-metric"><div class="lbl">帧率</div><div class="val" id="s-fps">0.0 Hz</div><div class="hint">0.0 Hz</div></div>
              <div class="cockpit-metric"><div class="lbl">芯片温度</div><div class="val" id="s-temp">--</div><div class="hint">高于 60°C 标红</div></div>
            </div>
          </div>

          <div class="cockpit-vehicle">
            <div class="cockpit-status-bar">
              <div class="cockpit-title">
                <small>车辆与防护</small>
                <h2>车辆态势</h2>
              </div>
              <span class="cockpit-chip warn" id="tb-fsd-local">FSD 防护</span>
            </div>
            <div class="cockpit-vehicle-visual" aria-label="车辆态势视觉">
              <div class="vehicle-lane"></div>
              <div class="vehicle-silhouette"></div>
            </div>
            <div class="cockpit-mode-grid">
              <div class="cockpit-mode active"><div>🎯</div><strong id="ov-drive-current">Normal</strong><span>当前驾驶风格</span></div>
              <div class="cockpit-mode"><div>🛡️</div><strong id="m-defense">防护中</strong><span>FSD 防护状态</span></div>
              <div class="cockpit-mode"><div>↗</div><strong id="s-soff">0</strong><span>速度偏移</span></div>
            </div>
            <div class="cockpit-mode-grid">
              <div class="cockpit-mode"><div>⚙</div><strong id="s-hw">--</strong><span>实际硬件模式</span></div>
              <div class="cockpit-mode"><div>📦</div><strong id="s-ver">--</strong><span>固件版本</span></div>
              <div class="cockpit-mode"><div>📡</div><strong id="m-rxtx">0 / 0</strong><span>RX / TX</span></div>
            </div>
            <div class="cockpit-safety-strip" id="m-alert">安全优先：暂无异常。实验功能请在停车和安全环境下验证。</div>
          </div>
        </div>

        <div class="card">
          <div class="card-title">核心开关</div>
          <div class="setting-row">
            <div>
              <div class="setting-name">FSD-V14 模式 <span class="exp-badge">实验</span></div>
              <div class="setting-desc">独立切换 MAX/V14 实验 profile，不影响模块总开关。</div>
            </div>
            <label class="tgl"><input type="checkbox" id="ov-fsd-tgl" onchange="toggleV14Mode()"><div class="tgl-track"></div></label>
          </div>
          <div class="setting-row">
            <div>
              <div class="setting-name">模块总开关</div>
              <div class="setting-desc">控制当前 FSD 注入模块启停；状态会同步保存。</div>
            </div>
            <label class="tgl"><input type="checkbox" id="ov-master-tgl" onchange="toggleFsd()"><div class="tgl-track"></div></label>
          </div>
          <div class="setting-row">
            <div>
              <div class="setting-name">开机自动启用</div>
              <div class="setting-desc">设备重启后按保存值恢复 FSD 注入。</div>
            </div>
            <label class="tgl"><input type="checkbox" id="fsd-boot-tgl" onchange="saveConfig()"><div class="tgl-track"></div></label>
          </div>
        </div>

        <div class="card">
          <div class="card-title">设备信息</div>
          <div class="stats">
            <div class="stat"><div class="stat-lbl">FSD-V14</div><div class="stat-val v-dim" id="ov-v14">待机</div></div>
            <div class="stat"><div class="stat-lbl">CAN1 RX/TX</div><div class="stat-val v-info" id="s-rx">0/0</div></div>
            <div class="stat"><div class="stat-lbl">TX Errors</div><div class="stat-val v-dim" id="s-txerr">0</div></div>
            <div class="stat"><div class="stat-lbl">跟随距离</div><div class="stat-val v-dim" id="s-fd">--</div></div>
            <div class="stat"><div class="stat-lbl">车辆 OTA</div><div class="stat-val v-acc" id="s-vota">正常</div></div>
            <div class="stat"><div class="stat-lbl">OTA 确认</div><div class="stat-val v-dim" id="s-vota-cnt">0</div></div>
          </div>
        </div>

        <div class="card">
          <div class="card-title">功耗管理</div>
          <div class="setting-row">
            <div><div class="setting-name">自动关机</div><div class="setting-desc">5 分钟无 CAN 数据自动休眠。</div></div>
            <label class="tgl"><input type="checkbox" id="ov-auto-shutdown" onchange="toggleAutoShutdown(this.checked)"><div class="tgl-track"></div></label>
          </div>
          <div class="setting-row">
            <div><div class="setting-name">WiFi 自动关闭</div><div class="setting-desc">5 分钟无操作关闭网络中转。</div></div>
            <label class="tgl"><input type="checkbox" id="ov-wifi-auto-off" onchange="toggleWifiAutoOff(this.checked)"><div class="tgl-track"></div></label>
          </div>
        </div>

        <div class="card">
          <div class="card-title">Legacy 运行时状态</div>
          <div class="stats">
            <div class="stat"><div class="stat-lbl">当前 HW</div><div class="stat-val v-acc" id="legacy-selected-hw">--</div></div>
            <div class="stat"><div class="stat-lbl">生效模式</div><div class="stat-val v-acc" id="legacy-effective-handler">--</div></div>
            <div class="stat"><div class="stat-lbl">检测 HW</div><div class="stat-val v-dim" id="legacy-detected-hw">--</div></div>
            <div class="stat"><div class="stat-lbl">AP 门控</div><div class="stat-val v-dim" id="legacy-ap-gate">--</div></div>
            <div class="stat"><div class="stat-lbl">OTA 看守</div><div class="stat-val v-dim" id="legacy-ota-guard">--</div></div>
          </div>
        </div>

        <div class="card">
          <div class="card-title">FSD 注入策略</div>
          <div class="setting-desc" style="margin-bottom:10px">稳定模式仅注入 mux0 bit46，禁用 mux1；对齐模式用于受控 A/B 测试。非 8.3.6 车型可关闭下方「AP 门控」开关以直接注入（跳过 AP-First 等待）。</div>
          <div class="sel-cards c3" id="legacy-fsd-policy-cards" style="margin-bottom:12px">
            <div class="sel-card active" data-policy="stable" onclick="setLegacyFsdPolicy('stable')">
              <div class="sel-lbl">推荐</div>
              <div class="sel-name">稳定模式</div>
            </div>
            <div class="sel-card" data-policy="experimental" onclick="setLegacyFsdPolicy('experimental')">
              <div class="sel-lbl">A/B</div>
              <div class="sel-name">实验模式</div>
            </div>
            <div class="sel-card" data-policy="legacy_tesla_parity" onclick="setLegacyFsdPolicy('legacy_tesla_parity')">
              <div class="sel-lbl">对齐</div>
              <div class="sel-name">Tesla Controller 对齐模式</div>
            </div>
          </div>
          <div class="stats">
            <div class="stat"><div class="stat-lbl">当前策略</div><div class="stat-val v-acc" id="legacy-fsd-strategy">稳定</div></div>
            <div class="stat"><div class="stat-lbl">mux0</div><div class="stat-val v-dim" id="legacy-fsd-mux0">--</div></div>
            <div class="stat"><div class="stat-lbl">mux1</div><div class="stat-val v-dim" id="legacy-fsd-mux1">--</div></div>
            <div class="stat"><div class="stat-lbl">触发源</div><div class="stat-val v-dim" id="legacy-fsd-trigger">--</div></div>
          </div>
          <div class="setting-row">
            <div><div class="setting-name">启用 mux1 注入</div><div class="setting-desc">实验模式：注入 mux1 帧。</div></div>
            <label class="tgl"><input type="checkbox" id="legacy-fsd-mux1-tgl" onchange="saveLegacyFsdExperimental()"><div class="tgl-track"></div></label>
          </div>
          <div class="setting-row">
            <div><div class="setting-name">写入速度 Profile</div><div class="setting-desc">实验模式：mux0 写入速度档位。</div></div>
            <label class="tgl"><input type="checkbox" id="legacy-fsd-profile" onchange="saveLegacyFsdExperimental()"><div class="tgl-track"></div></label>
          </div>
          <div class="setting-row">
            <div><div class="setting-name">清除视觉限速</div><div class="setting-desc">清除视觉限速位（独立于 policy）。</div></div>
            <label class="tgl"><input type="checkbox" id="legacy-fsd-vision" onchange="saveLegacyVision()"><div class="tgl-track"></div></label>
          </div>
          <div class="setting-row">
            <div><div class="setting-name">Legacy 速度偏移</div><div class="setting-desc">0x2F8 UI_userSpeedOffset，0-33 km/h（0=关）</div></div>
            <input class="inp" type="number" min="0" max="33" id="legacy-offset-inp" value="0" style="width:86px" onchange="saveLegacyOffset()">
          </div>
          <div class="setting-row">
            <div><div class="setting-name">重写限速 <span class="exp-badge">Legacy</span></div><div class="setting-desc">0x438 visionSpeedSlider=100 覆盖限速</div></div>
            <label class="tgl"><input type="checkbox" id="legacy-override-tgl" onchange="saveLegacyOverride()"><div class="tgl-track"></div></label>
          </div>
        </div>

        <div class="card">
          <div class="card-title">Legacy CAN-A 诊断</div>
          <div class="stats">
            <div class="stat"><div class="stat-lbl">0x3EE mux0</div><div class="stat-val v-dim" id="legacy-health-mux0">--</div></div>
            <div class="stat"><div class="stat-lbl">0x3EE mux1</div><div class="stat-val v-dim" id="legacy-health-mux1">--</div></div>
            <div class="stat"><div class="stat-lbl">TWAI 状态</div><div class="stat-val v-dim" id="legacy-health-twai">--</div></div>
            <div class="stat"><div class="stat-lbl">TEC/REC</div><div class="stat-val v-dim" id="legacy-health-tec-rec">--</div></div>
            <div class="stat"><div class="stat-lbl">TX失败</div><div class="stat-val v-dim" id="legacy-health-tx-failed">0</div></div>
            <div class="stat"><div class="stat-lbl">RX丢失</div><div class="stat-val v-dim" id="legacy-health-rx-missed">0</div></div>
            <div class="stat"><div class="stat-lbl">健康状态</div><div class="stat-val v-dim" id="legacy-health-state">--</div></div>
            <div class="stat"><div class="stat-lbl">最后阻止原因</div><div class="stat-val v-dim" id="legacy-health-blocked">--</div></div>
          </div>
        </div>

        <div class="card cockpit-card" id="ap-core-card">
          <div class="ck-head"><h3><span>🛡</span>AP 注入安全</h3><span class="ck-chip" id="ap-core-state-pill">等待 AP</span></div>
          <div class="state-panel" id="ap-state-panel">
            <div class="sp-label">AP 注入状态</div>
            <div class="sp-val" id="ap-core-state-big">等待</div>
            <div class="sp-desc" id="ap-core-state-detail">门控未触发</div>
            <div class="gate-bar" id="ap-gate-bar"><i id="ap-gate-fill"></i></div>
          </div>
          <div class="src-row"><span>注入来源</span><b id="injection-source">Disabled</b></div>
          <div class="ctl-section">注入门控（服务端强制）</div>
          <div class="ctl-row"><div><div class="cn">AP 门控</div><div class="cd">默认开：等 AP 稳定再注入（防 8.3.6 猛甩）。非 8.3.6 车型可关闭以直接注入</div></div>
            <label class="tgl"><input id="ap-core-gate-tgl" type="checkbox" onchange="saveApGateControls()"><div class="tgl-track"></div></label></div>
          <div class="ctl-row"><div><div class="cn">延迟注入</div><div class="cd">AP 激活后等待再注入（推荐 2000ms）</div></div>
            <label class="field"><select id="ap-delay-select" class="ap-delay-select" onchange="saveApGateControls()"><option value="0">0 ms</option><option value="1000">1000 ms</option><option value="2000">2000 ms</option><option value="3000">3000 ms</option></select></label></div>
          <div class="ctl-row"><div><div class="cn">AP 自动恢复</div><div class="cd">重启后恢复上次 AP 配置</div></div>
            <label class="tgl"><input id="ap-auto-restore-tgl" type="checkbox" onchange="saveApGateControls()"><div class="tgl-track"></div></label></div>
          <div class="safety-strip">⚠️ <b>Fail-closed（不变）：</b>未知 / 无效 / SNA 档位默认禁止注入；AP 断开立即清零 Gate 计时。此策略由服务端 C++ 强制（handlers.h），客户端 UI 无法绕过。</div>
          <div class="safety-strip"><b>⚠️ China 2026.8.3.6 风险：</b>该固件收紧预检，AP 激活边沿注入仍可能触发方向盘猛甩（即使开启 AP-First 仍有 &lt;5% 残留）。研究/教学用途，风险自担；强烈建议先 Listen-Only 验证。</div>
        </div>
          </div>
        </div>
      </div>

      <div class="mobile-remote">
        <div class="mobile-app-shell">
        <div class="cockpit-status-bar">
          <div class="cockpit-title">
            <small>ATLAS REMOTE</small>
            <h2>现场遥控</h2>
          </div>
          <span class="cockpit-chip ok">连接到设备热点</span>
        </div>
        <div class="mobile-primary-card">
          <div class="cockpit-state-label">FSD 注入</div>
          <div class="mobile-state" id="m-fsd">OFF</div>
          <p>普通点击不会触发危险动作；请长按确认后再启用模块。</p>
          <button id="m-fsd-tgl" class="mobile-hold" onclick="toggleFsd()">长按启用</button>
        </div>
        <div class="mobile-remote-grid">
          <div class="mobile-remote-card"><div class="lbl">CAN1</div><div class="val" id="m-can">--</div></div>
          <div class="mobile-remote-card" data-single-hide="1" data-cap="can2"><div class="lbl">CAN2</div><div class="val" id="m-can2">--</div></div>
          <div class="mobile-remote-card"><div class="lbl">硬件模式</div><div class="val" id="m-hw">Auto</div></div>
          <div class="mobile-remote-card"><div class="lbl">帧率</div><div class="val" id="m-fps">0.0 Hz</div></div>
        </div>
        <div class="mobile-risk-note">危险操作已收纳到底部导航的“更多”和对应二级页面；首页只保留现场安全遥控。</div>
        </div>
      </div>
    </div>

    <!-- Page 2: Hardware Config -->
    <div class="page" id="pg-hardware">
<div class="page-title">硬件模式</div>
<div class="status-triplet">
  <div class="status-chip"><div class="lbl">UI 显示状态</div><div class="val" id="st-hw-ui">Auto</div></div>
  <div class="status-chip"><div class="lbl">NVS 持久化状态</div><div class="val" id="st-hw-nvs">--</div></div>
  <div class="status-chip"><div class="lbl">实际 CAN 运行状态</div><div class="val" id="st-hw-run">--</div></div>
</div>
<!-- HW Version Selection -->
<div class="card">
  <div class="card-title">FSD 硬件模式</div>
  <div class="card-subtitle">选择车辆自动驾驶硬件版本；Auto 会使用固件现有探测逻辑。</div>
  <div class="sel-cards c4" id="hw-cards">
    <div class="sel-card active" onclick="setHW(3)">
      <div class="sel-lbl">推荐</div>
      <div class="sel-name">Auto</div>
    </div>
    <div class="sel-card" onclick="setHW(0)">
      <div class="sel-lbl">旧版</div>
      <div class="sel-name">Legacy</div>
    </div>
    <div class="sel-card" onclick="setHW(1)">
      <div class="sel-lbl">第三代</div>
      <div class="sel-name">HW3.0</div>
    </div>
    <div class="sel-card" onclick="setHW(2)">
      <div class="sel-lbl">第四代</div>
      <div class="sel-name">HW4.0</div>
    </div>
  </div>
</div>

<!-- AP Restore -->
<div class="card">
  <div class="setting-row">
    <div>
      <div class="setting-name">AP 状态恢复</div>
      <div class="setting-desc">重启后恢复上次 AP 配置</div>
    </div>
    <label class="tgl">
      <input type="checkbox" id="hw-ap-restore" onchange="saveConfig()">
      <div class="tgl-track"></div>
    </label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">AP 注入门控</div>
      <div class="setting-desc">要求 Parked/AP/Summon 状态后才允许注入</div>
    </div>
    <label class="tgl">
      <input type="checkbox" id="ap-gate-tgl" onchange="saveApGate()">
      <div class="tgl-track"></div>
    </label>
  </div>
</div>

<!-- 插件管理（驾驶舱风格，全中文） -->
<div class="card cockpit-card" id="plugins-card">
  <div class="ck-head"><h3><span>🧩</span>插件管理</h3><span class="ck-chip ok" id="plugins-count-pill">已装 0 个</span></div>
  <div class="ctl-section">添加插件</div>
  <div class="install-grid">
    <div class="install-cell"><div class="ii">🔗</div><div class="it">URL 安装</div>
      <input id="plugin-url" class="mock-input" placeholder="https://.../plugin.json">
      <button class="ds-btn" onclick="installPluginUrl()">安装</button></div>
    <div class="install-cell"><div class="ii">📤</div><div class="it">上传文件</div>
      <input id="plugin-file" type="file" accept=".json,application/json" class="mock-input">
      <button class="ds-btn" onclick="uploadPluginJson()">上传</button>
      <div id="plugin-upload-status" class="ds-btn-muted"></div></div>
    <div class="install-cell"><div class="ii">📋</div><div class="it">粘贴 JSON</div>
      <textarea id="plugin-json" rows="3" class="mock-input" placeholder='{"name":"...","rules":[]}'></textarea>
      <button class="ds-btn" onclick="installPluginJson()">解析安装</button></div>
  </div>
  <div class="ctl-section">已安装插件</div>
  <div id="plugin-list" class="plugin-list"></div>
  <div class="ctl-section">GTW2047 重放倍率</div>
  <select id="plugin-replay-count" onchange="setPluginReplayCount(this.value)" class="ds-select"><option value="1">1×</option><option value="2">2×</option><option value="3">3×</option><option value="4">4×</option><option value="5">5×</option></select>
  <div class="hint">仅重复已启用插件生成的 0x7FF 修改帧</div>
  <details class="collapse"><summary>规则测试器</summary><textarea id="plugin-editor-json" rows="6" class="mock-input"></textarea><button class="ds-btn" onclick="runPluginRuleTest()">规则测试</button><pre id="plugin-rule-test-result"></pre></details>
</div>
    </div>

    <!-- Page 3: Drive Mode -->
    <div class="page" id="pg-drive">
<div class="ck-head"><h3><span>◉</span>驾驶风格</h3><span class="ck-chip" id="st-drive-pill">Normal</span></div>
<div class="status-triplet">
  <div class="status-chip"><div class="lbl">UI 显示状态</div><div class="val" id="st-drive-ui">Normal</div></div>
  <div class="status-chip"><div class="lbl">NVS 持久化状态</div><div class="val" id="st-drive-nvs">--</div></div>
  <div class="status-chip"><div class="lbl">实际 CAN 运行状态</div><div class="val" id="st-drive-run">--</div></div>
</div>
<div class="card cockpit-card">
  <div class="card-subtitle">选择速度策略映射到 /drive_profile</div>
  <div class="drive-grid" id="drive-cards">
    <div class="drive-card" onclick="setDriveMode('auto')">
      <div class="drive-icon">🤖</div>
      <div class="drive-name">Auto</div>
      <div class="drive-desc">系统自动选择最佳模式</div>
    </div>
    <div class="drive-card" onclick="setDriveMode('sloth')">
      <div class="drive-icon">🦥</div>
      <div class="drive-name">Sloth</div>
      <div class="drive-desc">最低速档，适合拥堵跟车</div>
    </div>
    <div class="drive-card" onclick="setDriveMode('chill')">
      <div class="drive-icon">😌</div>
      <div class="drive-name">Chill</div>
      <div class="drive-desc">舒适驾驶，温和加速</div>
    </div>
    <div class="drive-card active" onclick="setDriveMode('normal')">
      <div class="drive-icon">🎯</div>
      <div class="drive-name">Normal</div>
      <div class="drive-desc">标准模式，均衡性能</div>
    </div>
    <div class="drive-card" onclick="setDriveMode('hurry')">
      <div class="drive-icon">🚀</div>
      <div class="drive-name">Hurry</div>
      <div class="drive-desc">积极提速，更快响应</div>
    </div>
    <div class="drive-card" onclick="setDriveMode('max')">
      <div class="drive-icon">⚡</div>
      <div class="drive-name">MAX</div>
      <div class="drive-desc">最大性能 <span class="exp-badge">实验</span></div>
    </div>
  </div>
  <div class="mode-note">当前选择：<span id="drive-current">Normal</span></div>
</div>
<div class="card">
  <div class="card-title">紧急控制</div>
  <div style="display:flex;gap:8px">
    <button class="btn btn-danger" onclick="rebootDevice()" style="flex:1">重启设备</button>
    <button class="btn" onclick="resetStats()" style="flex:1">重置计数</button>
  </div>
</div>
    </div>

    <!-- Page 4: Speed Offset -->
    <div class="page" id="pg-speed">
<div class="ck-head"><h3><span>◉</span>速度策略</h3><span class="ck-chip" id="st-speed-pill">auto</span></div>
<div class="status-triplet">
  <div class="status-chip"><div class="lbl">UI 显示状态</div><div class="val" id="st-speed-ui">待同步</div></div>
  <div class="status-chip"><div class="lbl">NVS 持久化状态</div><div class="val" id="st-speed-nvs">读取中</div></div>
  <div class="status-chip"><div class="lbl">实际 CAN/网络运行状态</div><div class="val" id="st-speed-run">待检测</div></div>
</div>
<div class="card cockpit-card">
  <div class="card-title">速度策略模式</div>
  <div class="card-subtitle">选择固定百分比 / 自动偏移 / 自定义偏移，配置会同步到 /speed_strategy 与 /speed_custom</div>
  <div class="sel-cards c3" id="speed-mode-tabs" style="margin-bottom:12px">
    <div class="sel-card" onclick="showSpeedMode('fixed')"><div class="sel-lbl">模式</div><div class="sel-name">固定百分比</div></div>
    <div class="sel-card active" onclick="showSpeedMode('auto')"><div class="sel-lbl">模式</div><div class="sel-name">自动偏移</div></div>
    <div class="sel-card" onclick="showSpeedMode('custom')"><div class="sel-lbl">模式</div><div class="sel-name">自定义偏移</div></div>
  </div>
  <div class="mode-note">当前策略：<span id="speed-current">auto</span></div>
</div>

<!-- Realtime Offset -->
<div class="card">
  <div class="card-title">实时偏移</div>
  <div class="diag-grid">
    <div class="diag-item"><span class="lbl">速度限制 speedLimit</span><span class="v-info" id="sp-limit">--</span></div>
    <div class="diag-item"><span class="lbl">实际偏移 actOffset</span><span class="v-acc" id="sp-act-offset">--</span></div>
    <div class="diag-item"><span class="lbl">生效模式</span><span class="v-dim" id="sp-active-mode">--</span></div>
    <div class="diag-item"><span class="lbl">Wire Encoding</span><span class="v-dim" id="sp-wire">--</span></div>
    <div class="diag-item"><span class="lbl">Fused Raw</span><span class="v-dim" id="sp-raw">--</span></div>
    <div class="diag-item"><span class="lbl">Stock Offset</span><span class="v-dim" id="sp-stock">--</span></div>
  </div>
</div>

<div class="card cockpit-card" id="legacy-smart-speed-card">
  <div class="card-title">Legacy 智能速度偏移 <span class="exp-badge" id="legacy-speed-chip">0x2F8 未检测到</span></div>
  <div class="card-subtitle">仅写 0x2F8 / 760 UI_userSpeedOffset；读取 GPS 限速后自动计算目标，降速平滑避免突然回落。</div>
  <div class="setting-row">
    <div><div class="setting-name">智能速度偏移模式</div><div class="setting-desc">默认关闭；手动模式使用固定 km/h 偏移。</div></div>
    <label class="field"><select id="legacy-offset-mode" onchange="saveLegacySmartSpeed()"><option value="off">关闭</option><option value="manual">手动</option><option value="auto">自动</option><option value="custom">自定义百分比</option></select></label>
  </div>
  <div class="setting-row">
    <div><div class="setting-name">手动偏移</div><div class="setting-desc">仅 manual 模式生效，范围 0-33 km/h。</div></div>
    <input class="inp" type="number" min="0" max="33" id="legacy-offset-manual" value="0" style="width:86px" onchange="saveLegacySmartSpeed()">
  </div>
  <div class="setting-row">
    <div><div class="setting-name">降速平滑</div><div class="setting-desc">目标限速下降时按速率缓慢回落。</div></div>
    <label class="tgl"><input type="checkbox" id="legacy-smooth-down" onchange="saveLegacySmartSpeed()"><div class="tgl-track"></div></label>
  </div>
  <div class="setting-row">
    <div><div class="setting-name">平滑速率</div><div class="setting-desc">km/h 每秒，越小越保守。</div></div>
    <input class="inp" type="number" min="1" max="20" id="legacy-smooth-rate" value="5" style="width:86px" onchange="saveLegacySmartSpeed()">
  </div>
  <div class="diag-grid" id="legacy-custom-pct-panel">
    <div class="diag-item"><span class="lbl">低速 %</span><input class="inp" type="number" min="0" max="63" id="legacy-pct-low" value="50" onchange="saveLegacySmartSpeed()"></div>
    <div class="diag-item"><span class="lbl">中速 %</span><input class="inp" type="number" min="0" max="63" id="legacy-pct-mid" value="30" onchange="saveLegacySmartSpeed()"></div>
    <div class="diag-item"><span class="lbl">高速 %</span><input class="inp" type="number" min="0" max="63" id="legacy-pct-high" value="20" onchange="saveLegacySmartSpeed()"></div>
    <div class="diag-item"><span class="lbl">超高速 %</span><input class="inp" type="number" min="0" max="63" id="legacy-pct-vhigh" value="10" onchange="saveLegacySmartSpeed()"></div>
  </div>
  <div class="diag-grid" style="margin-top:12px">
    <div class="diag-item"><span class="lbl">GPS 限速</span><span class="v-info" id="legacy-limit-kph">--</span></div>
    <div class="diag-item"><span class="lbl">目标限速</span><span class="v-acc" id="legacy-target-kph">--</span></div>
    <div class="diag-item"><span class="lbl">平滑限速</span><span class="v-dim" id="legacy-smooth-kph">--</span></div>
    <div class="diag-item"><span class="lbl">输出限速</span><span class="v-info" id="legacy-output-kph">--</span></div>
    <div class="diag-item"><span class="lbl">0x2F8 状态</span><span class="v-warn" id="legacy-gps-seen">0x2F8 未检测到</span></div>
    <div class="diag-item"><span class="lbl">最后原始值</span><span class="v-dim" id="legacy-last-raw">--</span></div>
  </div>
</div>

<div class="card" id="speed-panel-fixed">
  <div class="card-title">固定百分比</div>
  <div class="card-subtitle">所有速度限制统一使用同一个偏移百分比</div>
  <div class="sel-cards c3" id="speed-fixed-pct" style="margin-bottom:10px">
    <div class="sel-card" onclick="setSpeedFixedPct(0)"><div class="sel-lbl">固定</div><div class="sel-name">0%</div></div>
    <div class="sel-card" onclick="setSpeedFixedPct(10)"><div class="sel-lbl">固定</div><div class="sel-name">10%</div></div>
    <div class="sel-card" onclick="setSpeedFixedPct(20)"><div class="sel-lbl">固定</div><div class="sel-name">20%</div></div>
    <div class="sel-card active" onclick="setSpeedFixedPct(30)"><div class="sel-lbl">固定</div><div class="sel-name">30%</div></div>
    <div class="sel-card" onclick="setSpeedFixedPct(40)"><div class="sel-lbl">固定</div><div class="sel-name">40%</div></div>
    <div class="sel-card" onclick="setSpeedFixedPct(50)"><div class="sel-lbl">固定</div><div class="sel-name">50%</div></div>
  </div>
  <div class="mode-note">保存时会 POST /speed_strategy=fixed，并通过 /speed_custom 写入 manualPct。</div>
</div>

<div class="card" id="speed-panel-auto" style="display:none">
  <div class="card-title">自动偏移算法</div>
  <div class="card-subtitle">固件按当前 speedLimit 自动选择偏移比例，适合日常使用</div>
  <table class="tbl">
    <thead><tr><th>速度限制</th><th>自动偏移</th></tr></thead>
    <tbody>
      <tr><td>≤ 40 km/h</td><td>+50%，封顶 60</td></tr>
      <tr><td>≤ 60 km/h</td><td>+50%，封顶 90</td></tr>
      <tr><td>≤ 90 km/h</td><td>+30%，封顶 117</td></tr>
      <tr><td>≤ 110 km/h</td><td>+20%，封顶 132</td></tr>
      <tr><td>> 110 km/h</td><td>+10%，封顶 132</td></tr>
    </tbody>
  </table>
  <button class="btn" onclick="setSpeedStrategy('auto')" style="margin-top:12px;width:100%">启用自动偏移</button>
</div>

<div class="card" id="speed-panel-custom" style="display:none">
  <div class="card-title">自定义 4 区间偏移</div>
  <div class="card-subtitle">填写每个速度区间的偏移百分比，范围 0-50%</div>
  <div class="setting-row"><div><div class="setting-name">0-50 km/h</div><div class="setting-desc">低速区间 cp1</div></div><input class="inp" type="number" min="0" max="50" id="speed-cp1" value="30" style="width:86px"></div>
  <div class="setting-row"><div><div class="setting-name">51-70 km/h</div><div class="setting-desc">城市快速路 cp2</div></div><input class="inp" type="number" min="0" max="50" id="speed-cp2" value="20" style="width:86px"></div>
  <div class="setting-row"><div><div class="setting-name">71-100 km/h</div><div class="setting-desc">高速巡航 cp3</div></div><input class="inp" type="number" min="0" max="50" id="speed-cp3" value="10" style="width:86px"></div>
  <div class="setting-row"><div><div class="setting-name">101+ km/h</div><div class="setting-desc">高限速区间 cp4</div></div><input class="inp" type="number" min="0" max="50" id="speed-cp4" value="10" style="width:86px"></div>
  <button class="btn" onclick="saveSpeedCustom()" style="width:100%;margin-top:10px">保存自定义偏移</button>
</div>

<!-- Encoding -->

<div class="card">
  <div class="setting-row">
    <div>
      <div class="setting-name">速度编码方式</div>
      <div class="setting-desc">选择 CAN 总线速度编码</div>
    </div>
    <select class="inp" id="hw3-enc" onchange="saveHw3Speed()" style="width:120px">
      <option value="0">默认</option>
      <option value="1">编码 A</option>
      <option value="2">编码 B</option>
    </select>
  </div>
</div>
    </div>

    <!-- Page 5: CAN2 Control -->
    <div class="page" id="pg-bus2" data-cap="can2" data-single-hide="1">
<div class="page-title">CAN2 控制 <span class="exp-badge">未验证/实验</span></div>
<div class="status-triplet">
  <div class="status-chip"><div class="lbl">UI 显示状态</div><div class="val" id="st-light-ui">待同步</div></div>
  <div class="status-chip"><div class="lbl">NVS 持久化状态</div><div class="val" id="st-light-nvs">读取中</div></div>
  <div class="status-chip"><div class="lbl">实际 CAN/网络运行状态</div><div class="val" id="st-light-run">待检测</div></div>
</div>
<!-- CAN2 Status -->
<div class="stats">
  <div class="stat"><div class="stat-lbl">CAN2 状态</div><div class="stat-val v-dim" id="b2-status">Offline</div></div>
  <div class="stat"><div class="stat-lbl">CAN2 RX</div><div class="stat-val v-info" id="b2-rx">0</div></div>
  <div class="stat"><div class="stat-lbl">CAN2 TX</div><div class="stat-val v-info" id="b2-tx">0</div></div>
  <div class="stat"><div class="stat-lbl">CAN2 TXErr</div><div class="stat-val v-dim" id="b2-txerr">0</div></div>
  <div class="stat"><div class="stat-lbl">CAN2 EFLG</div><div class="stat-val v-dim" id="b2-eflg">0x00</div></div>
  <div class="stat"><div class="stat-lbl">已发现 ID</div><div class="stat-val v-acc" id="b2-ids">0</div></div>
</div>

<!-- CAN2 ID Table -->
<div class="card">
  <div class="card-title">CAN2 ID 列表 <span style="color:var(--tx3);font-size:11px;font-weight:400" id="b2-count">(0)</span></div>
  <div style="max-height:200px;overflow-y:auto">
    <table class="tbl">
      <thead><tr><th>ID</th><th>DLC</th><th>数据</th><th>计数</th></tr></thead>
      <tbody id="b2-rows"></tbody>
    </table>
  </div>
</div>

<!-- Stalk Test -->
<div class="card">
  <div class="card-title">CAN2 控制</div>
  <div class="card-subtitle">映射到 /lighting_config，并用 CAN2 stalk_test 执行爆闪序列</div>
  <div class="setting-row">
    <div>
      <div class="setting-name">爆闪开关 <span class="exp-badge">实验</span></div>
      <div class="setting-desc">启用后按配置执行多次拨杆注入</div>
    </div>
    <label class="tgl">
      <input type="checkbox" id="light-enabled-tgl" onchange="saveLightingConfig()">
      <div class="tgl-track"></div>
    </label>
  </div>
  <div class="sel-cards c4" id="light-preset" style="margin-bottom:10px">
    <div class="sel-card active" onclick="setLightPreset(3)"><div class="sel-lbl">次数</div><div class="sel-name">3</div></div>
    <div class="sel-card" onclick="setLightPreset(5)"><div class="sel-lbl">次数</div><div class="sel-name">5</div></div>
    <div class="sel-card" onclick="setLightPreset(7)"><div class="sel-lbl">次数</div><div class="sel-name">7</div></div>
    <div class="sel-card" onclick="setLightPreset(10)"><div class="sel-lbl">次数</div><div class="sel-name">10</div></div>
  </div>
  <div class="sel-cards c3" id="light-frequency" style="margin-bottom:10px">
    <div class="sel-card" onclick="setLightFrequency('slow')"><div class="sel-lbl">频率</div><div class="sel-name">slow</div></div>
    <div class="sel-card active" onclick="setLightFrequency('medium')"><div class="sel-lbl">频率</div><div class="sel-name">medium</div></div>
    <div class="sel-card" onclick="setLightFrequency('fast')"><div class="sel-lbl">频率</div><div class="sel-name">fast</div></div>
  </div>
  <div class="sel-cards c3" id="rear-fog-strategy" style="margin-bottom:10px">
    <div class="sel-card active" onclick="setRearFogStrategy('off')"><div class="sel-lbl">后雾灯</div><div class="sel-name">off</div></div>
    <div class="sel-card" onclick="setRearFogStrategy('strobe')"><div class="sel-lbl">后雾灯</div><div class="sel-name">strobe</div></div>
    <div class="sel-card" onclick="setRearFogStrategy('continuous')"><div class="sel-lbl">后雾灯</div><div class="sel-name">continuous <span class="exp-badge">实验</span></div></div>
  </div>
  <div style="display:flex;gap:8px;margin-bottom:10px">
    <button class="btn btn-outline" onclick="stalkTest('PULL')" style="flex:1">PULL (闪光)</button>
    <button class="btn btn-outline" onclick="stalkTest('PUSH')" style="flex:1">PUSH (远光)</button>
  </div>
  <div style="display:flex;gap:8px;margin-bottom:10px">
    <button class="btn" onclick="strobeTest('PULL')" style="flex:1">执行爆闪</button>
    <button class="btn btn-outline" onclick="strobeTest('PUSH')" style="flex:1">远光序列</button>
  </div>
  <div class="setting-row">
    <div class="setting-name">持续时间 (ms)</div>
    <input class="inp" type="number" id="stalk-dur" value="500" style="width:100px">
  </div>
  <div class="setting-row">
    <div class="setting-name">状态</div>
    <div class="setting-desc" id="stalk-status">空闲</div>
  </div>
</div>

<!-- Double Pull Flash Burst -->
<div class="card">
  <div class="card-title">双拨爆闪 <span class="exp-badge">实验</span></div>
  <div class="card-subtitle">2秒内两次真实 PULL 触发固件侧 0x249 PULL burst，默认关闭</div>
  <div class="setting-row">
    <div>
      <div class="setting-name">启用双拨触发</div>
      <div class="setting-desc">开启后，真实超车闪双拨才会自动执行预设爆闪</div>
    </div>
    <label class="tgl">
      <input type="checkbox" id="burst-enabled-tgl" onchange="toggleBurst(this)">
      <div class="tgl-track"></div>
    </label>
  </div>
  <div class="sel-cards c4" id="burst-preset" style="margin-bottom:10px">
    <div class="sel-card" onclick="setBurstPreset('A')"><div class="sel-lbl">A 短促</div><div class="sel-name">2× 250ms</div></div>
    <div class="sel-card active" onclick="setBurstPreset('B')"><div class="sel-lbl">B 标准</div><div class="sel-name">3× 180ms</div></div>
    <div class="sel-card" onclick="setBurstPreset('C')"><div class="sel-lbl">C 明显</div><div class="sel-name">5× 150ms</div></div>
    <div class="sel-card" onclick="setBurstPreset('D')"><div class="sel-lbl">D 长闪</div><div class="sel-name">7× 200ms</div></div>
  </div>
  <div class="stats">
    <div class="stat"><div class="stat-lbl">双拨状态</div><div class="stat-val v-dim" id="burst-status">关闭</div></div>
    <div class="stat"><div class="stat-lbl">当前参数</div><div class="stat-val v-info" id="burst-params">3× 180/180ms</div></div>
    <div class="stat"><div class="stat-lbl">剩余相位</div><div class="stat-val v-dim" id="burst-phases">0</div></div>
  </div>
</div>

<!-- Service Mode -->
<div class="card">
  <div class="setting-row">
    <div>
      <div class="setting-name">Service Mode</div>
      <div class="setting-desc">按 VCSEC 规格发送 0x339 四帧脉冲到 CAN2（byte5 bit7）</div>
    </div>
    <label class="tgl">
      <input type="checkbox" id="svc-mode-tgl" onchange="toggleServiceMode()">
      <div class="tgl-track"></div>
    </label>
  </div>
</div>
    </div>

    <!-- Page 5.5: Light Stunt (Phase 4) -->
    <div class="page" id="pg-strobe" data-cap="lighting" data-single-hide="1">
<div class="page-title">灯光特技 <span class="exp-badge">实验</span></div>
<div class="card">
  <div class="card-title">后雾灯策略</div>
  <div class="card-subtitle">这里仅保存默认策略；下方按钮才会立即发送 0x273 CAN-B 执行动作，仅D挡时激活</div>
  <div class="setting-row">
    <div>
      <div class="setting-name">雾灯策略</div>
      <div class="setting-desc">选择后雾灯模式</div>
    </div>
    <select id="fog-strategy" onchange="saveFogStrategy()">
      <option value="0">关闭</option>
      <option value="1">爆闪</option>
      <option value="2">持续亮</option>
    </select>
  </div>
</div>
<div class="card">
  <div class="card-title">爆闪参数</div>
  <div class="setting-row">
    <div>
      <div class="setting-name">闪烁次数</div>
    </div>
    <select id="strobe-count" onchange="saveLightingConfig()">
      <option value="3">3次</option>
      <option value="5">5次</option>
      <option value="7">7次</option>
      <option value="10">10次</option>
    </select>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">闪烁频率</div>
    </div>
    <select id="strobe-freq" onchange="saveLightingConfig()">
      <option value="0">慢速</option>
      <option value="1" selected>中速</option>
      <option value="2">快速</option>
    </select>
  </div>
</div>
<div class="card">
  <div class="card-title">双拨超车闪 <span class="exp-badge">实验</span></div>
  <div class="card-subtitle">真实拨杆 2 秒内双 PULL 后，固件自动发送 0x249 PULL burst</div>
  <div class="setting-row">
    <div>
      <div class="setting-name">启用双拨爆闪</div>
      <div class="setting-desc" id="burst-status-strobe">关闭</div>
    </div>
    <label class="tgl">
      <input type="checkbox" id="burst-enabled-tgl-strobe" onchange="toggleBurst(this)">
      <div class="tgl-track"></div>
    </label>
  </div>
  <div class="sel-cards c4" id="burst-preset-strobe" style="margin-bottom:10px">
    <div class="sel-card" onclick="setBurstPreset('A')"><div class="sel-lbl">A 短促</div><div class="sel-name">2×</div></div>
    <div class="sel-card active" onclick="setBurstPreset('B')"><div class="sel-lbl">B 标准</div><div class="sel-name">3×</div></div>
    <div class="sel-card" onclick="setBurstPreset('C')"><div class="sel-lbl">C 明显</div><div class="sel-name">5×</div></div>
    <div class="sel-card" onclick="setBurstPreset('D')"><div class="sel-lbl">D 长闪</div><div class="sel-name">7×</div></div>
  </div>
</div>
<div class="card">
  <div class="card-title">立即执行</div>
  <div class="card-subtitle">这些按钮才会触发实际灯光动作；需D挡，自动停止于其他挡位</div>
  <div style="display:flex;gap:8px;flex-wrap:wrap;margin-top:8px">
    <button class="btn" onclick="fogTrigger('strobe')" style="flex:1;min-width:100px">💡 爆闪</button>
    <button class="btn" onclick="fogTrigger('f1')" style="flex:1;min-width:100px">🏎️ F1领航灯</button>
    <button class="btn" onclick="fogTrigger('continuous')" style="flex:1;min-width:100px">🔆 持续亮</button>
    <button class="btn btn-danger" onclick="fogTrigger('stop')" style="flex:1;min-width:100px">⏹ 停止</button>
  </div>
  <div class="diag-grid" style="margin-top:12px">
    <div class="diag-item"><span class="lbl">当前状态</span><span class="v-acc" id="strobe-status">待触发</span></div>
    <div class="diag-item"><span class="lbl">挡位</span><span class="v-dim" id="strobe-gear">--</span></div>
  </div>
</div>
    </div>

    <!-- Page 6: FSD Defense -->
    <div class="page" id="pg-defense">
<div class="ck-head"><h3><span>◉</span>FSD 防护</h3><span class="ck-chip" id="st-defense-pill">部分实验</span></div>
<div class="status-triplet">
  <div class="status-chip"><div class="lbl">UI 显示状态</div><div class="val" id="st-defense-ui">待同步</div></div>
  <div class="status-chip"><div class="lbl">NVS 持久化状态</div><div class="val" id="st-defense-nvs">读取中</div></div>
  <div class="status-chip"><div class="lbl">实际 CAN/网络运行状态</div><div class="val" id="st-defense-run">待检测</div></div>
</div>
<!-- AP injection delay (mobile-accessible, fixes missing delay option on phone UI) -->
<div class="card cockpit-card">
  <div class="card-title">AP 注入门控</div>
  <div class="card-subtitle">AP 激活后延迟注入，服务端 fail-closed 强制</div>
  <div class="setting-row">
    <div>
      <div class="setting-name">延迟注入时间</div>
      <div class="setting-desc">AP 激活后等待再注入（推荐 2000ms，0=立即）</div>
    </div>
    <label class="field"><select class="ap-delay-select" onchange="saveApDelay(this)"><option value="0">0 ms</option><option value="1000">1000 ms</option><option value="2000">2000 ms</option><option value="3000">3000 ms</option></select></label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">Soft Engage 方向盘居中</div>
      <div class="setting-desc">激活时 hold 到方向盘近居中再注入，降弯道猛甩（超时 5s 兜底）</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-soft-engage-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
</div>
<!-- Master switch + 5 defense toggles -->
<div class="card cockpit-card">
  <div class="card-title">FSD 防护状态</div>
  <div class="card-subtitle">仿生扭矩替代固定echo，轮DND消除提示音</div>
  <div class="setting-row">
    <div>
      <div class="setting-name">启用 slew rate 限制</div>
      <div class="setting-desc">限制偏移值下降速率</div>
    </div>
    <label class="tgl">
      <input type="checkbox" id="hw3-slew-tgl" onchange="saveHw3Slew()">
      <div class="tgl-track"></div>
    </label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">NAG 抑制 <span class="exp-badge">实验</span></div>
      <div class="setting-desc">检测到握方向盘警告(0x399)时，反应式爆发扭矩抑制 NAG（实车验证中，故障即关）</div>
      <div class="setting-desc" id="def-bionic-warn" style="color:#ef4444;display:none">⚠ 已自动回退至echo（连续帧异常）</div>
      <div class="setting-desc" id="def-bionic-risk" style="color:#ef4444;display:none">⚠ 反应式扭矩注入 · 实车验证中 / 故障立即关闭</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-bionic-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
  <div class="row">
    <label>NAG 模式</label>
    <select id="nag-mode-select" onchange="saveDefenseConfig()">
      <option value="0">Off</option>
      <option value="2">EPAS Late Echo 实验</option>
    </select>
  </div>
  <div class="hint warn">EPAS Late Echo 为封闭环境研究模式：默认关闭，只发送 0x370，保留 handsOnLevel，cadence/timing 不满足时自动不发。</div>
  <div class="setting-row">
    <div>
      <div class="setting-name">扭矩篡改(1.80Nm) <span class="exp-badge">高危</span></div>
      <div class="setting-desc">0x370 固定扭矩注入(2026-06-19事故嫌疑向量)</div>
      <div class="setting-desc" id="def-ntt-warn" style="color:#ef4444;display:none">⚠ 仅台架测试,严禁上车</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-ntt-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">声音警告抑制</div>
      <div class="setting-desc">ISA bit 抑制 + 0x3C2 音量滚轮DND</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-sound-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">限速覆盖 <span class="exp-badge">HW4</span></div>
      <div class="setting-desc">0x39B 强写融合/视觉限速为 NONE</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-isa-override-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">音量滚轮DND <span class="exp-badge">Phase 3</span></div>
      <div class="setting-desc">0x3C2 四步序列消除音量提示音</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-dnd-vol-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">速度免打扰 <span class="exp-badge">实验</span></div>
      <div class="setting-desc">保存到防御配置，避免速度策略频繁扰动</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-speed-nd-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">速度滚轮DND <span class="exp-badge">Phase 3</span></div>
      <div class="setting-desc">0x3C2 四步序列消除速度滚轮提示</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-dnd-spd-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">AP/EAP 兼容 <span class="exp-badge">未验证</span></div>
      <div class="setting-desc">保留 AP/EAP 兼容策略位</div>
    </div>
    <label class="tgl"><input type="checkbox" id="def-apeap-tgl" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
</div>

<div class="card cockpit-card" id="abort-guard-card">
  <div class="card-title">Abort Guard <span class="exp-badge">实验</span></div>
  <div class="card-subtitle">默认关闭。检测 AP state 8/9 abort 后锁止注入路径，直到 AP 退出后自动解除。</div>
  <div class="setting-row">
    <div>
      <div class="setting-name">启用 Abort Guard</div>
      <div class="setting-desc">AP state 8/9 latch/lock until AP exits，避免 abort 后继续注入。</div>
    </div>
    <label class="tgl"><input type="checkbox" id="abort-guard-toggle" onchange="saveDefenseConfig()"><div class="tgl-track"></div></label>
  </div>
  <div class="diag-grid">
    <div class="diag-item"><span class="lbl">Guard 状态</span><span class="v-dim" id="abort-guard-state">关闭</span></div>
    <div class="diag-item"><span class="lbl">AP state</span><span class="v-dim" id="abort-guard-ap">--</span></div>
    <div class="diag-item"><span class="lbl">Abort state</span><span class="v-dim" id="abort-guard-abort">--</span></div>
    <div class="diag-item"><span class="lbl">阻止次数</span><span class="v-warn" id="abort-guard-blocks">0</span></div>
    <div class="diag-item"><span class="lbl">阻止路径</span><span class="v-dim" id="abort-guard-path">--</span></div>
  </div>
</div>

<!-- Slew Config -->
<div class="card">
  <div class="card-title">保护参数</div>
  <div class="diag-grid">
    <div class="diag-item"><span class="lbl">最大下降速率</span><span class="v-acc" id="def-rate">--</span></div>
    <div class="diag-item"><span class="lbl">最小保持偏移</span><span class="v-acc" id="def-min">--</span></div>
    <div class="diag-item"><span class="lbl">触发次数</span><span class="v-warn" id="def-cnt">0</span></div>
    <div class="diag-item"><span class="lbl">当前偏移</span><span class="v-dim" id="def-cur">--</span></div>
  </div>
</div>

<!-- Protection Status -->
<div class="card">
  <div class="setting-row">
    <div style="display:flex;align-items:center;gap:8px">
      <span class="status-dot err" id="def-dot"></span>
      <div class="setting-name" id="def-status">保护未启用</div>
    </div>
  </div>
</div>
    </div>

    <!-- Page 7: OTA Update -->
    <div class="page" id="pg-ota">
<div class="ck-head"><h3><span>◉</span>OTA 升级</h3><span class="ck-chip" id="st-ota-pill">--</span></div>
<!-- Version Info -->
<div class="card cockpit-card">
  <div class="card-title">固件信息</div>
  <div class="diag-grid">
    <div class="diag-item"><span class="lbl">版本</span><span class="v-acc" id="ota-ver">--</span></div>
    <div class="diag-item"><span class="lbl">构建时间</span><span class="v-dim" id="ota-build">--</span></div>
    <div class="diag-item"><span class="lbl">Flash 占用</span><span class="v-dim" id="ota-flash">--</span></div>
    <div class="diag-item"><span class="lbl">SDK</span><span class="v-dim" id="ota-sdk">--</span></div>
  </div>
</div>

<!-- Release Update -->
<div class="card">
  <div class="card-title">Release 在线更新 <span class="exp-badge">Batch C</span></div>
  <div class="card-subtitle">从 GitHub Release 检查与安装匹配当前硬件的 OTA 固件</div>
  <div class="diag-grid">
    <div class="diag-item"><span class="lbl">当前版本</span><span class="v-acc" id="rel-current">--</span></div>
    <div class="diag-item"><span class="lbl">最新版本</span><span class="v-info" id="rel-latest">--</span></div>
    <div class="diag-item"><span class="lbl">固件文件</span><span class="v-dim" id="rel-artifact">--</span></div>
    <div class="diag-item"><span class="lbl">更新状态</span><span class="v-dim" id="rel-status">未检查</span></div>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">Beta 通道</div>
      <div class="setting-desc">检查最新 prerelease，默认关闭</div>
    </div>
    <label class="tgl"><input type="checkbox" id="rel-beta-tgl" onchange="toggleUpdateBeta()"><div class="tgl-track"></div></label>
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">开机自动更新</div>
      <div class="setting-desc">联网后自动检查并安装新版本，建议测试稳定后再启用</div>
    </div>
    <label class="tgl"><input type="checkbox" id="rel-auto-tgl" onchange="toggleAutoUpdate()"><div class="tgl-track"></div></label>
  </div>
  <div style="display:flex;gap:6px;margin-top:8px;flex-wrap:wrap">
    <button class="btn btn-sm" id="rel-check-btn" onclick="checkReleaseUpdate()">检查更新</button>
    <button class="btn btn-sm btn-outline" id="rel-install-btn" onclick="installReleaseUpdate()" disabled>安装更新</button>
  </div>
  <div class="setting-desc" id="rel-msg" style="margin-top:8px">请先连接 WiFi 后检查 GitHub Release。</div>
</div>

<!-- Upload Area -->
<div class="card">
  <div class="card-title">固件上传</div>
  <div class="upload-area" id="ota-drop" onclick="$('ota-file').click()">
    <div style="font-size:24px;color:var(--tx3);margin-bottom:4px">↑</div>
    <div style="color:var(--tx1);font-weight:500">选择固件文件</div>
    <div style="color:var(--tx3);font-size:11px">支持 .bin 格式，拖放或点击选择</div>
  </div>
  <input type="file" id="ota-file" accept=".bin" style="display:none" onchange="uploadFirmware()">
  <div style="margin-top:10px;display:flex;gap:6px;flex-wrap:wrap">
    <button class="btn" id="ota-btn" onclick="uploadFirmware()" style="flex:1">开始上传</button>
    <button class="btn btn-outline" id="ota-reset-btn" onclick="resetOtaCredentials()" style="flex:1">重置凭据缓存</button>
  </div>
  <div id="ota-progress" style="display:none;margin-top:8px">
    <div style="background:var(--card-bg-alt);border-radius:4px;height:8px;overflow:hidden">
      <div id="ota-bar" style="background:var(--accent);height:100%;width:0%;transition:width .3s"></div>
    </div>
    <div style="text-align:center;color:var(--tx3);font-size:11px;margin-top:4px" id="ota-pct">0%</div>
  </div>
</div>
    </div>

    <!-- Page 8: Network Settings -->
    <div class="page" id="pg-network">
<div class="ck-head"><h3><span>◉</span>网络设置</h3><span class="ck-chip" id="st-net-pill">未配置</span></div>
<div class="status-triplet">
  <div class="status-chip"><div class="lbl">UI 显示状态</div><div class="val" id="st-net-ui">待同步</div></div>
  <div class="status-chip"><div class="lbl">NVS 持久化状态</div><div class="val" id="st-net-nvs">读取中</div></div>
  <div class="status-chip"><div class="lbl">实际 CAN/网络运行状态</div><div class="val" id="st-net-run">待检测</div></div>
</div>
<div class="status-triplet">
  <div class="status-chip"><div class="lbl">DNS UI 显示状态</div><div class="val" id="st-dns-ui">待同步</div></div>
  <div class="status-chip"><div class="lbl">DNS NVS 持久化状态</div><div class="val" id="st-dns-nvs">读取中</div></div>
  <div class="status-chip"><div class="lbl">DNS 实际运行状态</div><div class="val" id="st-dns-run">待检测</div></div>
</div>
<!-- WiFi Hotspot -->
<div class="card cockpit-card">
  <div class="card-title">WiFi 热点</div>
  <div class="setting-row">
    <div class="setting-name">SSID</div>
    <div class="v-dim" id="ap-ssid">--</div>
  </div>
  <div class="setting-row">
    <div class="setting-name">连接设备</div>
    <div class="v-ok" id="ap-clients">0</div>
  </div>
  <div class="setting-row">
    <div class="setting-name">WiFi Mode</div>
    <div class="v-acc" id="ap-mode">--</div>
  </div>
  <div class="setting-row">
    <div class="setting-name">AP SSID</div>
    <input class="inp" id="ap-ssid-input" style="width:180px" placeholder="热点名称">
  </div>
  <div class="setting-row">
    <div class="setting-name">AP 密码</div>
    <input class="inp" type="password" id="ap-pass-input" style="width:180px" placeholder="8-64 位">
  </div>
  <div class="setting-row">
    <div>
      <div class="setting-name">隐藏热点</div>
      <div class="setting-desc">保存后重启生效</div>
    </div>
    <label class="tgl">
      <input type="checkbox" id="ap-hidden-tgl">
      <div class="tgl-track"></div>
    </label>
  </div>
  <div style="display:flex;gap:6px;margin-top:8px">
    <button class="btn btn-sm" onclick="saveHotspot(false)">保存热点</button>
    <button class="btn btn-sm btn-outline" onclick="saveHotspot(true)">保存并重启</button>
  </div>
</div>

<!-- WiFi Internet -->
<div class="card">
  <div class="card-title">WiFi 联网</div>
  <div class="setting-row">
    <div class="setting-name">状态</div>
    <div class="v-warn" id="wifi-status">未配置</div>
  </div>
  <div id="wifi-slots" style="margin-top:8px"></div>
  <div style="margin-top:8px;display:flex;gap:6px">
    <button class="btn btn-sm" onclick="scanWifi()">扫描网络</button>
    <button class="btn btn-sm btn-outline" onclick="editWifiSlot(-1)">手动添加</button>
    <button class="btn btn-sm btn-outline" onclick="testRelayWifi()">测试连接</button>
  </div>
  <div id="wifi-form" style="display:none;margin-top:10px">
    <div class="setting-row"><div class="setting-name">SSID</div><input class="inp" id="wf-ssid" style="width:160px"></div>
    <div class="setting-row"><div class="setting-name">密码</div><input class="inp" type="password" id="wf-pass" style="width:160px"></div>
    <div style="display:flex;gap:6px;margin-top:6px">
      <button class="btn btn-sm" onclick="saveWifi()">保存</button>
      <button class="btn btn-sm btn-outline" onclick="clearWifiForm()">取消</button>
    </div>
  </div>
</div>

<!-- STA-AP Gateway -->
<div class="card">
  <div class="setting-row">
    <div class="setting-name">STA-AP 网关 (NAT)</div>
    <label class="tgl">
      <input type="checkbox" id="gw-nat-tgl" onchange="saveGateway()">
      <div class="tgl-track"></div>
    </label>
  </div>
  <div class="diag-grid" style="margin-top:6px">
    <div class="diag-item"><span class="lbl">AP</span><span class="v-dim" id="gw-ap">--</span></div>
    <div class="diag-item"><span class="lbl">STA</span><span class="v-dim" id="gw-sta">--</span></div>
    <div class="diag-item"><span class="lbl">NAT</span><span class="v-dim" id="gw-nat-st">--</span></div>
    <div class="diag-item"><span class="lbl">DNS</span><span class="v-dim" id="gw-dns-st">--</span></div>
    <div class="diag-item"><span class="lbl">Slow</span><span class="v-dim" id="gw-slow">0</span></div>
    <div class="diag-item"><span class="lbl">Pending</span><span class="v-dim" id="gw-pend">0</span></div>
    <div class="diag-item"><span class="lbl">Upstream</span><span class="v-dim" id="gw-upstream">--</span></div>
    <div class="diag-item"><span class="lbl">Clients</span><span class="v-dim" id="gw-clients">0</span></div>
  </div>
  <div class="setting-row" style="margin-top:6px">
    <div>
      <div class="setting-name">网络性能模式</div>
      <div class="setting-desc">转发流量时降低 WebUI 轮询</div>
    </div>
    <label class="tgl">
      <input type="checkbox" id="gw-perf-tgl" onchange="saveGateway()">
      <div class="tgl-track"></div>
    </label>
  </div>
</div>

<!-- Upstream DNS -->
<div class="card">
  <div class="card-title">上游 DNS</div>
  <div class="sel-cards c4" id="dns-upstream">
    <div class="sel-card active" onclick="setDnsUpstream('auto')"><div class="sel-name">Auto</div></div>
    <div class="sel-card" onclick="setDnsUpstream('223.5.5.5')"><div class="sel-name">Ali</div></div>
    <div class="sel-card" onclick="setDnsUpstream('119.29.29.29')"><div class="sel-name">Tencent</div></div>
    <div class="sel-card" onclick="setDnsUpstream('custom')"><div class="sel-name">Custom</div></div>
  </div>
  <div id="dns-custom-row" style="display:none;margin-top:6px">
    <input class="inp" id="dns-custom-ip" placeholder="输入 DNS IP 地址">
  </div>
  <div class="sel-cards c2" style="margin-top:6px" id="dns-profile">
    <div class="sel-card" onclick="setDnsProfile('conservative')"><div class="sel-name">保守模式</div></div>
    <div class="sel-card active" onclick="setDnsProfile('aggressive')"><div class="sel-name">激进模式</div></div>
  </div>
</div>

<!-- DNS Filter Rules -->
<div class="card">
  <div class="card-title">黑名单 / DNS 过滤规则</div>
  <div class="card-subtitle">保守模式使用完整 12 条规则，激进模式使用最小 9 条规则</div>
  <div style="margin-bottom:8px">
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:4px">
      <span style="color:var(--err);font-weight:500;font-size:11px">黑名单</span>
      <span style="color:var(--tx3);font-size:10px" id="dns-bl-cnt">0 域名</span>
    </div>
    <textarea class="inp" id="dns-blacklist" rows="6" placeholder="每行一个域名"></textarea>
  </div>
  <div style="margin-bottom:8px">
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:4px">
      <span style="color:var(--ok);font-weight:500;font-size:11px">白名单</span>
      <span style="color:var(--tx3);font-size:10px" id="dns-wl-cnt">0 域名</span>
    </div>
    <textarea class="inp" id="dns-whitelist" rows="3" placeholder="每行一个域名"></textarea>
  </div>
  <div style="display:flex;gap:6px;margin-bottom:8px">
    <button class="btn btn-sm" onclick="saveGatewayDns()">保存规则</button>
    <button class="btn btn-sm btn-outline" onclick="loadGatewayDns()">刷新</button>
  </div>
  <div style="display:flex;gap:6px;margin-bottom:8px">
    <input class="inp" id="dns-test-input" placeholder="输入域名测试..." style="flex:1">
    <button class="btn btn-sm" onclick="testGatewayDns()">测试</button>
  </div>
  <div style="display:flex;justify-content:space-between;align-items:center">
    <span style="color:var(--tx3);font-size:10px">已拦截: <span id="dns-blocked-cnt">0</span> 条</span>
    <div style="display:flex;gap:4px">
      <button class="btn btn-sm btn-outline" onclick="loadGatewayBlocked()">刷新</button>
      <button class="btn btn-sm btn-outline" onclick="clearGatewayBlocked()">清空</button>
    </div>
  </div>
</div>

    </div>

    <!-- Page 10: CAN Tools -->
    <div class="page" id="pg-can">
<div class="ck-head"><h3><span>◉</span>CAN 诊断</h3><span class="ck-chip" id="st-can-pill">0 / 0</span></div>
<!-- legacy contract marker: class="diag-shell" -->
<div class="diag-shell diag-console">
  <aside class="diag-kpi-rail">
    <div class="diag-kpi"><div class="lbl">CAN1 RX/TX</div><div class="val" id="diag-rxtx">0 / 0</div></div>
    <div class="diag-kpi"><div class="lbl">CAN2 EFLG</div><div class="val" id="diag-eflg">0x00</div></div>
    <div class="diag-kpi"><div class="lbl">发送错误</div><div class="val" id="diag-txerr">0</div></div>
    <div class="diag-kpi"><div class="lbl">最近写入</div><div class="val" id="diag-last-write">--</div></div>
    <div class="diag-kpi"><div class="lbl">发现 ID</div><div class="val" id="diag-id-count">--</div></div>
    <div class="diag-timeline">
      <div class="label">LIVE TIMELINE</div>
      <div class="diag-bars">
        <div class="diag-bar"><span style="width:82%"></span></div>
        <div class="diag-bar"><span style="width:54%"></span></div>
        <div class="diag-bar"><span style="width:28%"></span></div>
      </div>
    </div>
    <div class="card">
      <div class="card-title">CAN 引脚</div>
      <div class="diag-grid">
        <div class="diag-item"><span class="lbl">CAN1 TX</span><span class="v-acc">GPIO 7</span></div>
        <div class="diag-item"><span class="lbl">CAN1 RX</span><span class="v-acc">GPIO 6</span></div>
        <div class="diag-item"><span class="lbl">CAN2 CS</span><span class="v-acc" id="can-cs">GPIO 10</span></div>
        <div class="diag-item"><span class="lbl">SPI SCK</span><span class="v-acc" id="can-sck">GPIO 12</span></div>
        <div class="diag-item"><span class="lbl">SPI MISO</span><span class="v-acc" id="can-miso">GPIO 13</span></div>
        <div class="diag-item"><span class="lbl">SPI MOSI</span><span class="v-acc" id="can-mosi">GPIO 11</span></div>
        <div class="diag-item"><span class="lbl">MCP RST</span><span class="v-acc" id="can-rst">GPIO 9</span></div>
      </div>
    </div>
  </aside>

  <section class="diag-main-panel">
    <div class="card cockpit-card">
      <div class="cockpit-status-bar">
        <div class="cockpit-title"><small>工程模式</small><h2>错误优先的实时帧</h2></div>
        <div class="cockpit-chip-row"><span class="cockpit-chip ok">实时</span><span class="cockpit-chip warn">错误优先</span><span class="cockpit-chip">DBC 可切换</span></div>
      </div>
      <div class="diag-toolbar">
        <input class="inp" id="sniff-filter" placeholder="帧筛选：输入 ID、名称、TXERR 或 mux...">
        <button class="btn btn-sm btn-outline" id="sniff-pause" onclick="toggleSniffPause()">暂停</button>
      </div>
      <div class="sub-tabs">
        <div class="sub-tab active" onclick="switchCanTab('sniffer')">实时帧</div>
        <div class="sub-tab" onclick="switchCanTab('recorder')">记录器</div>
        <div class="sub-tab" onclick="switchCanTab('controller')">控制器</div>
        <div class="sub-tab" onclick="switchCanTab('debug')">调试</div>
      </div>
    </div>

<!-- Sniffer -->
<div id="can-sniffer">
  <div class="card">
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
      <span style="font-weight:800">实时帧</span>
      <span style="color:var(--tx3);font-size:11px" id="sniff-count">0 实时帧</span>
    </div>
    <div class="diag-table-shell">
      <div class="diag-priority-row"><span>ID</span><span>DIR</span><span>DLC</span><span>DATA / EVENT</span><span>AGE</span></div>
      <div id="sniff-rows"></div>
    </div>
  </div>
</div>

<!-- Recorder -->
<div id="can-recorder" style="display:none">
  <div class="card">
    <div class="card-title">CAN 记录器</div>
    <div style="display:flex;gap:8px;margin-bottom:10px">
      <button class="btn btn-sm" id="rec-start" onclick="startRec()">开始记录</button>
      <button class="btn btn-sm btn-outline" id="rec-stop" onclick="stopRec()" disabled>停止</button>
      <button class="btn btn-sm btn-outline" id="rec-dl" onclick="downloadRec()" disabled>下载 CSV</button>
    </div>
    <div class="setting-row">
      <div class="setting-name">录制帧数限制</div>
      <input class="inp" type="number" id="rec-limit" value="1000" style="width:100px">
    </div>
    <div class="setting-row">
      <div class="setting-name">状态</div>
      <div class="setting-desc" id="rec-status">空闲</div>
    </div>
  </div>
</div>

<!-- Controller -->
<div id="can-controller" style="display:none">
  <div class="card">
    <div class="card-title">CAN 控制器状态</div>
    <div class="diag-grid">
      <div class="diag-item"><span class="lbl">EFLG</span><span class="v-dim" id="ctrl-eflg">0x00</span></div>
      <div class="diag-item"><span class="lbl">RX Errors</span><span class="v-dim" id="ctrl-rxerr">0</span></div>
      <div class="diag-item"><span class="lbl">发送错误</span><span class="v-dim" id="ctrl-txerr">0</span></div>
      <div class="diag-item"><span class="lbl">模式</span><span class="v-dim" id="ctrl-mode">--</span></div>
    </div>
    <div id="ctrl-mux" style="margin-top:8px"></div>
  </div>
</div>

<!-- Debug -->
<div id="can-debug" style="display:none">
  <div class="card">
    <div class="card-title">调试</div>
    <div class="setting-row">
      <div class="setting-name">调试日志</div>
      <label class="tgl">
        <input type="checkbox" id="debug-tgl" onchange="toggleCanDebug()">
        <div class="tgl-track"></div>
      </label>
    </div>
  </div>
  <div class="card">
    <div class="card-title">最近写入检查</div>
    <div class="diag-grid">
      <div class="diag-item"><span class="lbl">Injected</span><span class="v-acc" id="lw-injected">--</span></div>
      <div class="diag-item"><span class="lbl">Bus</span><span class="v-dim" id="lw-bus">--</span></div>
      <div class="diag-item"><span class="lbl">Match</span><span class="v-dim" id="lw-match">--</span></div>
      <div class="diag-item"><span class="lbl">Age</span><span class="v-dim" id="lw-age">--</span></div>
    </div>
  </div>
</div>
  </section>
</div>
    </div>

    <!-- Page 10: Auto Shift (Phase 5A placeholder) -->
    <div class="page" id="pg-shift" data-single-hide="1">
<div class="page-title">自动换挡 <span class="exp-badge">储备功能</span></div>
<div class="card" style="border-color:#f59e0b;background:rgba(245,158,11,0.06)">
  <div style="display:flex;align-items:center;gap:10px;margin-bottom:8px">
    <span style="font-size:24px">⚠️</span>
    <div>
      <div style="font-weight:700;color:#f59e0b">储备功能未开放</div>
      <div class="setting-desc">此页面为只读遥测展示，自动换挡功能将在后续版本中实装</div>
    </div>
  </div>
</div>
<div class="card">
  <div class="card-title">车辆遥测（只读）</div>
  <div class="card-subtitle">从 CAN 总线实时读取的车辆状态数据</div>
  <div class="diag-grid">
    <div class="diag-item"><span class="lbl">当前车速</span><span class="v-acc" id="shift-speed">-- km/h</span></div>
    <div class="diag-item"><span class="lbl">当前挡位</span><span class="v-info" id="shift-gear">--</span></div>
    <div class="diag-item"><span class="lbl">刹车状态</span><span class="v-dim" id="shift-brake">--</span></div>
    <div class="diag-item"><span class="lbl">FSD 状态</span><span class="v-dim" id="shift-fsd">--</span></div>
  </div>
</div>
<div class="card">
  <div class="card-title">换挡策略预览</div>
  <div class="setting-desc" style="margin-bottom:8px">基于车速和驾驶模式的自动换挡策略（示意图）</div>
  <table class="tbl">
    <thead><tr><th>车速区间</th><th>Auto</th><th>Chill</th><th>Hurry</th></tr></thead>
    <tbody>
      <tr><td>0-30 km/h</td><td>标准</td><td>舒适</td><td>积极</td></tr>
      <tr><td>30-60 km/h</td><td>标准</td><td>舒适</td><td>积极</td></tr>
      <tr><td>60-90 km/h</td><td>标准</td><td>标准</td><td>积极</td></tr>
      <tr><td>90+ km/h</td><td>经济</td><td>标准</td><td>最大</td></tr>
    </tbody>
  </table>
</div>
    </div>

<script>

// ═══════════════════════════════════════════════════════════
// Atlas T-2CAN Dashboard — Complete JavaScript
// ═══════════════════════════════════════════════════════════

// ── Constants & State ──────────────────────────────────────
var HW_NAMES = ['Legacy','HW3','HW4'];
var HW_LABELS = {
  'Legacy': {zh:'旧版',en:'Legacy'},
  'HW3': {zh:'第三代',en:'HW3'},
  'HW4': {zh:'第四代',en:'HW4'}
};
var SP_NAMES = ['Chill','Normal','Hurry'];
var S = {hw:-1,ci:false,sp:1,spa:true,can:false,ia:false,
         driveProfile:3,
         hw3OffsetSlew:false,hw3SlewRate:0,hw3CustomSpeed:false,
         hw3CustomTarget:[45,60,75,90,105],
         hw3HighSpeedEnable:false,
         hw3HighSpeedTarget:[90,110,130],
         hw3WireEncoding:0,legacyMppOverride:false,
         legacyMppCustomEnable:false,
         legacyMppHighSpeedEnable:false,
         legacyMppCustomTarget:[45,60,75,90,105],
         legacyMppHighSpeedTarget:[90,110,130]};
var lang = 'zh';
var dark = true;
var pollTimer = null;
var pollMs = 1000;
var pollTick = null;
var sniffPaused = false;
var sniffFrames = [];
var recActive = false;
var canTab = 'sniffer';
var lightPreset = 3;
var lightFrequency = 'medium';
var rearFogStrategy = 'off';
var burstPreset = 'B';
var BURST_PRESETS = {
  A:{count:2,on_ms:250,off_ms:250,label:'A 短促'},
  B:{count:3,on_ms:180,off_ms:180,label:'B 标准'},
  C:{count:5,on_ms:150,off_ms:150,label:'C 明显'},
  D:{count:7,on_ms:200,off_ms:200,label:'D 长闪'}
};
var DNS_PROFILES = {
  conservative: {
    blacklist: 'api-prd.vn.cloud.tesla.cn\nhermes-x2-api.prd.vn.cloud.tesla.cn\nsignaling.vn.cloud.tesla.cn\nhermes-prd.vn.cloud.tesla.cn\nhermes-stream-prd.vn.cloud.tesla.cn\ntelemetry-prd.vn.cloud.tesla.cn\ntelemetry.tesla.cn\napigateway-x2-trigger.tesla.cn\nfleet-api.prd.cn.vn.cloud.tesla.cn\nfirmware.tesla.cn\nlog.tesla.cn\nvehicle-files.prd.cnn1.vn.cloud.tesla.cn',
    whitelist: 'connman.vn.cloud.tesla.cn\nnav-prd-maps.tesla.cn\nmaps-cn-prd.go.tesla.services\nmedia-server-me.tesla.cn'
  },
  aggressive: {
    blacklist: 'telemetry.tesla.cn\ntelemetry-prd.vn.cloud.tesla.cn\nhermes-prd.vn.cloud.tesla.cn\nfleet-api.prd.cn.vn.cloud.tesla.cn\nfirmware.tesla.cn\nlog.tesla.cn\nvehicle-files.prd.cnn1.vn.cloud.tesla.cn\nhermes-x2-api.prd.vn.cloud.tesla.cn\nsignaling.vn.cloud.tesla.cn',
    whitelist: 'connman.vn.cloud.tesla.cn\nnav-prd-maps.tesla.cn\nmaps-cn-prd.go.tesla.services\nmedia-server-me.tesla.cn'
  }
};
var OLD_DNS_BLACKLIST = 'tesla.cn\ntesla.com\nteslamotors.com\ntesla.services';

// ── Utilities ──────────────────────────────────────────────
function $(id){return document.getElementById(id)}
function val(id){var e=$(id);return e?e.value:''}
function setVal(id,v){var e=$(id);if(e)e.value=v}
function setText(id,txt){var e=$(id);if(e)e.textContent=txt}
function setFsdVisualState(on){
  var stateText=on?'ON':'OFF';
  var mobileText=stateText;
  setText('fsd-label',stateText);
  setText('m-fsd',mobileText);
  var desc=on?'FSD 注入模块已启用；请持续关注 CAN 状态和防护状态。':'模块总开关关闭；启用前请确认车辆处于安全状态。';
  setText('ov-fsd-desc',desc);
  var btn=$('fsd-toggle');
  if(btn){btn.textContent=on?'长按 2 秒关闭 FSD 注入':'长按 2 秒启用 FSD 注入';btn.classList.toggle('on',!!on);}
  var mobileBtn=document.querySelector('.mobile-hold');
  if(mobileBtn){mobileBtn.textContent=on?'长按关闭':'长按启用';mobileBtn.classList.toggle('on',!!on);}
  var state=$('fsd-label');
  if(state){state.classList.toggle('on',!!on);}
  var mobileState=$('m-fsd');
  if(mobileState){mobileState.classList.toggle('on',!!on);}
}
function setHtml(id,html){var e=$(id);if(e)e.innerHTML=html}
function setCls(id,cls){var e=$(id);if(e)e.className=cls}
function escHtml(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
function esc(s){return String(s==null?'':s).replace(/[&<>]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c]})}
function escAttr(s){return String(s).replace(/\\/g,'\\\\').replace(/'/g,"\\'").replace(/"/g,'\\"').replace(/</g,'\\x3c').replace(/>/g,'\\x3e')}
function showToast(msg,ok){var t=$('toast');if(t){t.textContent=msg;t.classList.toggle('ok',!!ok);t.classList.add('show')}}
function hideToast(){var t=$('toast');if(t){t.classList.remove('show');t.classList.remove('ok')}}
function toHex(n){return '0x'+('0000'+n.toString(16).toUpperCase()).slice(-4)}
function normLines(s){return String(s||'').split(/\r?\n/).map(function(x){return x.trim().toLowerCase()}).filter(Boolean).join('\n')}
function countLines(s){return normLines(s).split('\n').filter(Boolean).length}
function setStatusTriplet(prefix,ui,nvs,run,state){
  setText('st-'+prefix+'-ui',ui||'--');
  setText('st-'+prefix+'-nvs',nvs||'--');
  setText('st-'+prefix+'-run',run||'--');
  var cls=state==='err'?'s-err':(state==='warn'?'s-warn':'s-ok');
  ['ui','nvs','run'].forEach(function(part){
    var el=$('st-'+prefix+'-'+part);
    if(el)el.className='val '+cls;
  });
}
function hwLabel(hw){return hw>=0&&hw<3?HW_NAMES[hw]:'Auto'}
function driveLabel(sp,spa){return spa?'Auto':(sp===0?'Chill':(sp===2?'Hurry':'Normal'))}
function experimentSummary(){
  var flags=[];
  var drive=$('drive-current');
  if(drive&&drive.textContent==='MAX')flags.push('MAX/V14');
  var light=$('light-enabled-tgl');
  if(light&&light.checked)flags.push('灯光爆闪');
  var apeap=$('def-apeap-tgl');
  if(apeap&&apeap.checked)flags.push('AP/EAP');
  var bionic=$('def-bionic-tgl');
  if(bionic&&bionic.checked)flags.push('FSD增强');
  var ntt=$('def-ntt-tgl');
  if(ntt&&ntt.checked)flags.push('扭矩篡改');
  return flags.length?('实验: '+flags.join('/')):'实验项需实车验证';
}
function fmtUp(sec){
  var h=Math.floor(sec/3600),m=Math.floor(sec%3600/60),s=sec%60;
  return (h<10?'0':'')+h+':'+(m<10?'0':'')+m+':'+(s<10?'0':'')+s;
}

// ── I18N ───────────────────────────────────────────────────
var I18N={
  // Sidebar
  '驾驶状态':'Drive Status','硬件模式':'Hardware Mode','驾驶风格':'Drive Style',
  '速度策略':'Speed Strategy',['CAN2 '+'控制']:'CAN2 Control','灯光特技':'Light Show',
  'FSD 防护':'FSD Guard','OTA 升级':'OTA Update',
  '网络设置':'Network','CAN 诊断':'CAN Diagnostics',['自动'+'换挡']:'Auto'+' Shift',
  // Top bar
  '已连接':'Connected','未连接':'Disconnected','连接丢失':'Connection Lost',
  // Overview
  'FSD 注入':'FSD Injection','FSD 开关':'FSD Toggle',
  '点击切换开关状态':'Tap to toggle','CAN Bus':'CAN Bus',
  '帧率':'FPS','硬件版本':'Hardware','速度偏移':'Offset',
  '芯片温度':'Chip Temp','TX Errors':'TX Err','跟随距离':'Follow Dist',
  // Hardware
  '硬件版本':'Hardware Version','选择您的自动驾驶硬件版本':'Select your AD hardware version',
  '推荐':'Recommended','旧版':'Legacy','第三代':'Gen 3','第四代':'Gen 4',
  '速度配置':'Speed Profile','选择速度偏移方案':'Select speed offset scheme',
  'AP 状态恢复':'AP State Restore','重启后恢复上次 AP 配置':'Restore AP config after reboot',
  // FSD page
  'FSD 注入控制':'FSD Injection Control',
  '启用后设备将注入 CAN 帧到车辆总线':'Injects CAN frames to vehicle bus when enabled',
  '已关闭':'OFF','已开启':'ON',
  '开机自动启用':'Auto-enable on boot','设备重启后自动开启 FSD 注入':'Auto-enable FSD after reboot',
  '紧急控制':'Emergency','重启设备':'Reboot','重置计数':'Reset Stats',
  // Speed
  'HW3 自定义速度':'HW3 Custom Speed','启用自定义速度':'Enable custom speed',
  '覆盖默认速度映射表':'Override default speed mapping',
  '速度映射表':'Speed Mapping','实际速度':'Actual','目标速度':'Target',
  '高速映射':'High Speed Mapping',
  '速度编码方式':'Speed Encoding','选择 CAN 总线速度编码':'Select CAN speed encoding',
  '默认':'Default','编码 A':'Enc A','编码 B':'Enc B',
  '实时数据':'Realtime Data',
  // CAN2
  ['CAN2 '+'状态']:'CAN2 Status',['CAN2 '+'RX']:'CAN2 '+'RX','已发现 ID':'Found IDs',
  'CAN2 ID 列表':'CAN2 CAN IDs',
  '灯光注入测试':'Lighting Test','模拟方向盘拨杆操作':'Simulate stalk operation',
  '持续时间 (ms)':'Duration (ms)','状态':'Status','空闲':'Idle',
  '启用 0x339 持续注入 CAN2':'Enable 0x339 continuous CAN2 injection',
  // Defense
  '偏移速率保护':'Slew Rate Protection','防止速度偏移突变被检测':'Prevent sudden offset detection',
  '启用 slew rate 限制':'Enable slew rate limit','限制偏移值下降速率':'Limit offset drop rate',
  '保护参数':'Protection Params',
  '最大下降速率':'Max Drop Rate','最小保持偏移':'Min Hold Offset',
  '触发次数':'Triggers','当前偏移':'Current Offset',
  '保护未启用':'Protection OFF','保护已启用':'Protection ON',
  // OTA
  '固件信息':'Firmware Info','版本':'Version','构建时间':'Build Time',
  'Flash 占用':'Flash Used','Release 在线更新':'Release Online Update',
  '当前版本':'Current','最新版本':'Latest','固件文件':'Artifact','更新状态':'Update Status',
  'Beta 通道':'Beta Channel','开机自动更新':'Auto-update on boot','检查更新':'Check Update','安装更新':'Install Update',
  '固件上传':'Firmware Upload','选择固件文件':'Select firmware',
  '支持 .bin 格式，拖放或点击选择':'.bin files, drag-drop or click',
  '开始上传':'Start Upload','上传中...':'Uploading...',
  // Network
  'WiFi 热点':'WiFi Hotspot','SSID':'SSID','连接设备':'Clients',
  'WiFi 联网':'WiFi Internet','未配置':'Not configured',
  '扫描网络':'Scan','手动添加':'Add Manually','密码':'Password',
  '保存':'Save','取消':'Cancel',
  'STA-AP 网关 (NAT)':'STA-AP Gateway (NAT)',
  '网络性能模式':'Perf Mode','转发流量时降低 WebUI 轮询':'Reduce polling during forwarding',
  '上游 DNS':'Upstream DNS',
  '保守模式':'Conservative','激进模式':'Aggressive',
  'DNS 过滤规则':'DNS Filter Rules',
  '黑名单':'Blacklist','白名单':'Whitelist','域名':'domains',
  '保存规则':'Save Rules','刷新':'Refresh',
  '输入域名测试...':'Enter domain to test...','测试':'Test',
  '已拦截':'Blocked','条':'items','清空':'Clear',
  // CAN Tools
  'CAN 引脚配置':'CAN Pin Config',
  '实时帧':'Live Frames',
  '记录器':'Recorder','开始录制':'Start','停止':'Stop','下载 CSV':'Download CSV',
  '录制帧数限制':'Frame Limit',
  '控制器状态':'Controller Status',
  '调试':'Debug','调试日志':'Debug Log',
  '最近写入':'Last Write Check'
};
function T(zh){return lang==='en'&&I18N[zh]?I18N[zh]:zh}
var CAP={singleCan:false,can2Available:true,lightingBusSupported:true,serviceModeSupported:true,stalkTestSupported:true,bus2SnifferSupported:true,fsdActivation:true,speedStrategy:true,driveProfile:true,networkSettings:true,otaUpdate:true,canDiagnostics:true};
var PRODUCT_SINGLE_CAN_NAME='Atlas Single CAN';
var PRODUCT_DUAL_CAN_NAME='Atlas T-2CAN';
function isSingleCan(){return !!CAP.singleCan}
function applyProductMode(){
  var name=isSingleCan()?PRODUCT_SINGLE_CAN_NAME:PRODUCT_DUAL_CAN_NAME;
  document.querySelectorAll('[data-product-name]').forEach(function(n){n.textContent=name});
  document.querySelectorAll('[data-single-hide]').forEach(function(n){n.classList.toggle('cap-hidden',isSingleCan())});
  document.querySelectorAll('[data-dual-hide]').forEach(function(n){n.classList.toggle('cap-hidden',!isSingleCan())});
  document.querySelectorAll('[data-single-text]').forEach(function(n){if(isSingleCan())n.textContent=n.getAttribute('data-single-text')});
}
function capEnabled(name){
  if(name==='can2')return !!CAP.can2Available;
  if(name==='lighting')return !!CAP.lightingBusSupported;
  return true;
}
function applyCapabilities(c){
  if(c){for(var k in c)CAP[k]=c[k]}
  var nodes=document.querySelectorAll('[data-cap]');
  for(var i=0;i<nodes.length;i++){
    var n=nodes[i];
    var show=capEnabled(n.getAttribute('data-cap'));
    n.classList.toggle('cap-hidden',!show);
  }
  var active=document.querySelector('.page.active');
  if(active&&active.getAttribute('data-cap')&&!capEnabled(active.getAttribute('data-cap'))){
    showPage('pg-overview');
  }
  applyProductMode();
}
function applyI18n(){
  var navs=document.querySelectorAll('.nav-item');
  var zhTexts=['驾驶状态','硬件模式','驾驶风格','速度策略','CAN2 '+'控制','灯光特技','FSD 防护','OTA 升级','网络设置','CAN 诊断','自动'+'换挡'];
  for(var i=0;i<navs.length;i++){
    var icon=navs[i].querySelector('.nav-icon');
    var iconHtml=icon?icon.outerHTML:'';
    navs[i].innerHTML=iconHtml+(lang==='zh'?zhTexts[i]:I18N[zhTexts[i]]);
  }
  $('lang-btn').textContent=lang==='zh'?'EN':'中';
}

// ── Theme ──────────────────────────────────────────────────
function toggleTheme(){
  dark=!dark;
  var r=document.documentElement.style;
  if(dark){
    r.setProperty('--sidebar-bg','#111827');
    r.setProperty('--main-bg','#1f2937');
    r.setProperty('--card-bg','#374151');
    r.setProperty('--card-bg-alt','#1f2937');
    r.setProperty('--tx1','#f9fafb');r.setProperty('--tx2','#d1d5db');
    r.setProperty('--tx3','#9ca3af');r.setProperty('--border','#4b5563');
    r.setProperty('--header-bg','#111827');
    if($('theme-btn'))$('theme-btn').textContent='☀';
    if($('mobile-theme-btn'))$('mobile-theme-btn').textContent='☀ 白天';
  }else{
    r.setProperty('--sidebar-bg','#ffffff');
    r.setProperty('--main-bg','#f4f4f8');
    r.setProperty('--card-bg','#ffffff');
    r.setProperty('--card-bg-alt','#f3f4f8');
    r.setProperty('--tx1','#1f2329');r.setProperty('--tx2','#4b5563');
    r.setProperty('--tx3','#71717a');r.setProperty('--border','#d8dbe2');
    r.setProperty('--header-bg','#ffffff');
    if($('theme-btn'))$('theme-btn').textContent='🌙';
    if($('mobile-theme-btn'))$('mobile-theme-btn').textContent='🌙 夜间';
  }
}

// ── Language ───────────────────────────────────────────────
function toggleLanguage(){
  lang=lang==='zh'?'en':'zh';
  applyI18n();
  updateFsdToggle(S.ci);
}

// ── Navigation ─────────────────────────────────────────────
function showPage(pageId){
  var _tgt=document.getElementById(pageId);
  if(_tgt&&_tgt.getAttribute('data-single-hide')&&isSingleCan())pageId='pg-overview';
  var pages=document.querySelectorAll('.page');
  for(var i=0;i<pages.length;i++)pages[i].classList.remove('active');
  var p=$(pageId);if(p)p.classList.add('active');
  var navs=document.querySelectorAll('.nav-item');
  for(var i=0;i<navs.length;i++){
    navs[i].classList.toggle('active',navs[i].getAttribute('data-page')===pageId);
  }
  closeSidebar();
  if(typeof updateMobTabs==='function')updateMobTabs(pageId);
  var mobDualTabs=document.querySelectorAll('#mob-tabs-dual .mob-tab[data-page]');
  for(var i=0;i<mobDualTabs.length;i++){
    mobDualTabs[i].classList.toggle('active',mobDualTabs[i].getAttribute('data-page')===pageId);
  }
  var mobMoreItems=document.querySelectorAll('#mob-more .mob-more-item[data-page]');
  for(var i=0;i<mobMoreItems.length;i++){
    mobMoreItems[i].classList.toggle('active',mobMoreItems[i].getAttribute('data-page')===pageId);
  }
  if(pageId==='pg-can')pollCanTab();
  if(pageId==='pg-drive')loadDriveProfile();
  if(pageId==='pg-bus2'&&CAP.can2Available&&!isSingleCan())pollCAN2();
  if(pageId==='pg-bus2'&&CAP.lightingBusSupported&&!isSingleCan())loadLightingConfig();
  if(pageId==='pg-bus2'&&CAP.can2Available&&!isSingleCan())loadBurstConfig();
  if(pageId==='pg-strobe'&&CAP.lightingBusSupported&&!isSingleCan())loadStrobePage();
  if(pageId==='pg-strobe'&&CAP.can2Available&&!isSingleCan())loadBurstConfig();
  if(pageId==='pg-speed')loadSpeedStrategy();
  if(pageId==='pg-defense')loadDefenseConfig();
  if(pageId==='pg-shift'&&!isSingleCan())pollGearAssist();
  if(pageId==='pg-ota'){loadFirmwareInfo();loadOtaReleaseState();}
  if(pageId==='pg-network'){pollWifiStatus();pollGatewayStatus();loadGatewayDns();loadGatewayBlocked();}
}
function openSidebar(){$('sidebar').classList.add('open');$('overlay').classList.add('active')}
function closeSidebar(){$('sidebar').classList.remove('open');$('overlay').classList.remove('active')}

// ── Fetch Helper ───────────────────────────────────────────
var _fetchErrCount=0;
async function fetchJson(url){
  try{
    var r=await fetch(url);
    if(!r.ok)throw new Error(r.status);
    var d=await r.json();
    if(_fetchErrCount>0){_fetchErrCount=0;hideToast()}
    return d;
  }catch(e){_fetchErrCount++;if(_fetchErrCount>=3)showToast(T('连接丢失')+' ('+_fetchErrCount+')');return null}
}
async function postForm(url,data){
  try{
    var body=[];
    for(var k in data)body.push(encodeURIComponent(k)+'='+encodeURIComponent(data[k]));
    var r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.join('&')});
    var txt=await r.text();
    var payload=null;
    if(txt){
      try{payload=JSON.parse(txt)}catch(e){}
    }
    if(!r.ok){
      var msg=payload&&(payload.error||payload.msg)?(payload.error||payload.msg):('HTTP '+r.status);
      throw new Error(msg);
    }
    hideToast();
    return payload||{ok:true,text:txt};
  }catch(e){
    showToast((e&&e.message)?e.message:T('请求失败'));
    throw e;
  }
}

// ── Polling ────────────────────────────────────────────────
async function poll(){
  var d=await fetchJson('/status');
  if(!d){
    setCls('tb-status','topbar-badge badge-err');
    setText('tb-status',T('未连接'));
    return;
  }
  var uptime=(d.uptime!==undefined)?d.uptime:(d.up||0);
  S.hw=d.hw;S.ci=d.ci;S.sp=d.sp;S.spa=d.spAuto;S.can=d.can;S.ia=d.ia;S.driveProfile=d.driveProfile!==undefined?d.driveProfile:S.driveProfile;
  applyCapabilities(d.capabilities||{});
  S.hw3OffsetSlew=d.hw3OffsetSlew;S.hw3SlewRate=d.hw3SlewRate;
  S.hw3CustomSpeed=d.hw3CustomSpeed;
  S.hw3CustomTarget=d.hw3CustomTarget||[45,60,75,90,105];
  S.hw3HighSpeedEnable=d.hw3HighSpeedEnable;
  S.hw3HighSpeedTarget=d.hw3HighSpeedTarget||[90,110,130];
  S.hw3WireEncoding=d.hw3WireEncoding;
  S.legacyMppOverride=d.legacyMppOverride;
  S.legacyMppCustomEnable=d.legacyMppCustomEnable;
  S.legacyMppHighSpeedEnable=d.legacyMppHighSpeedEnable;
  S.legacyMppCustomTarget=d.legacyMppCustomTarget||[45,60,75,90,105];
  S.legacyMppHighSpeedTarget=d.legacyMppHighSpeedTarget||[90,110,130];

  // Top bar
  setText('tb-fps',d.fps.toFixed(1)+' Hz');
  setCls('tb-status','topbar-badge '+(d.can?'badge-ok':'badge-err'));
  setText('tb-status',d.can?T('已连接'):T('未连接'));
  setCls('tb-dot-status','topbar-dot '+(d.can?'ok':'err'));
  setCls('tb-fsd','topbar-badge '+(d.ia?'badge-ok':'badge-warn'));
  setText('tb-fsd',d.ia?'FSD ON':'FSD OFF');
  setCls('tb-dot-fsd','topbar-dot '+(d.ia?'ok':'warn'));
  setText('tb-exp',experimentSummary());
  setText('tb-up',fmtUp(uptime));

  // Overview
  var ovTgl=$('ov-fsd-tgl');
  if(ovTgl)ovTgl.checked=(S.driveProfile===5);
  var masterTgl=$('ov-master-tgl');
  if(masterTgl)masterTgl.checked=!!d.ci;
  var bootTgl=$('fsd-boot-tgl');
  if(bootTgl)bootTgl.checked=!!(d.bootCan!==undefined?d.bootCan:d.ci);
  var mFsdTgl=$('m-fsd-tgl');
  if(mFsdTgl){
    if(mFsdTgl.type==='checkbox')mFsdTgl.checked=!!d.ci;
    else if(mFsdTgl.classList&&mFsdTgl.classList.contains('mobile-hold'))mFsdTgl.className='mobile-hold '+(d.ci?'on':'');
    else if(mFsdTgl.classList)mFsdTgl.classList.toggle('on',!!d.ci);
  }
  setCls('ov-v14','stat-val '+(S.driveProfile===5?'v-warn':'v-dim'));
  setText('ov-v14',S.driveProfile===5?'MAX/V14':'待机');
  var hwText=d.hwName||hwLabel(d.hw);
  setText('ov-hw',hwText);
  setCls('ov-can','stat-val '+(d.can?'v-ok':'v-err'));
  setText('ov-can', d.can ? 'CAN1 Online' : 'CAN1 Offline');
  // AP 激活状态（问题2：替换 CAN2，与 ap-core-state-pill 同源字段 d.apInjectionState）
  var apState = d.apInjectionState || 'blocked';
  var apActive = (apState === 'injecting' || apState === 'settling');
  setText('ov-ap', apActive ? '激活' : '等待');
  var apMetric = $('ov-ap-metric');
  if (apMetric) apMetric.style.borderColor = apActive ? 'rgba(52,211,153,.45)' : '';
  var apHint = $('ov-ap-hint');
  if (apHint) apHint.textContent = (apState==='settling') ? ('Gate ' + (d.apDelayMs || 2000) + 'ms · 已就绪') : (apState==='injecting' ? '正在注入' : '门控未触发');
  setText('ov-up',fmtUp(uptime));
  var mCan=$('m-can');
  if(mCan){mCan.className=(mCan.classList&&mCan.classList.contains('val')?'val ':'stat-val ')+(d.can?'v-ok':'v-err');mCan.textContent=d.can?'Online':'Offline';}
  var mCan2=$('m-can2');
  var can2Online=($('s-can2')&&$('s-can2').textContent==='Online');
  var can2Text=($('s-can2')&&$('s-can2').textContent)?$('s-can2').textContent:'--';
  if(mCan2){mCan2.className=(mCan2.classList&&mCan2.classList.contains('val')?'val ':'stat-val ')+(can2Online?'v-ok':'v-dim');mCan2.textContent=can2Text;}
  setFsdVisualState(!!d.ci);
  setText('m-fps',d.fps.toFixed(1)+' Hz');
  setText('m-rxtx',(d.rx||0)+' / '+(d.tx||0));
  setText('m-up',fmtUp(uptime));
  setText('m-hw',(d.hwName||hwLabel(d.hw)));
  setText('m-drive',driveModeLabel(driveModeFromProfile(d.driveProfile,d.driveProfileName)));
  setText('m-speed',d.soff!==undefined?d.soff:'--');
  setText('m-defense',d.hw3OffsetSlew?'ON':'OFF');
  // Phase 1: OTA + 功耗管理状态
  setCls('s-vota','stat-val '+(d.vehicleOta?'v-err':'v-ok'));
  setText('s-vota',d.vehicleOta?'OTA 进行中':'正常');
  var otaCnt=$('s-vota-cnt');
  if(otaCnt) fetchJson('/vehicle_ota_status').then(function(o){if(o)setText('s-vota-cnt',o.otaConfirmCount||0)}).catch(function(){});
  var ashut=$('ov-auto-shutdown');if(ashut)ashut.checked=!!d.autoShutdown;
  var woff=$('ov-wifi-auto-off');if(woff)woff.checked=!!d.wifiAutoOff;
  var alertText=[];
  if(!d.can)alertText.push('CAN 离线');
  if(d.txerr)alertText.push('TX Err '+d.txerr);
  if(d.apGate)alertText.push('AP Gate 等待');
  setText('m-alert',alertText.length?alertText.join(' / '):'暂无异常');
  setCls('s-can','stat-val '+(d.can?'v-ok':'v-err'));
  setText('s-can',d.can?'Online':'Offline');
  setText('s-rx',(d.rx||0)+'/'+(d.tx||0));
  setText('s-tx',d.tx||0);
  if(d.can2){
    setText('b2-tx',d.can2.tx||0);
    setText('b2-txerr',d.can2.txerr||0);
    setText('b2-eflg','0x'+toHex(d.can2.eflg||0,2));
  }
  setText('s-fps',d.fps.toFixed(1)+' Hz');
  setText('s-hw',(d.hwName||hwLabel(d.hw)));
  setText('s-soff',d.soff||0);
  // Temp from /system_status is separate; use eflg field as proxy
  setText('s-txerr',d.txerr||0);
  setText('diag-rxtx',((d.rxCount||d.rx||0)+' / '+(d.txCount||d.tx||0)));
  setText('st-can-pill',((d.rxCount||d.rx||0)+' / '+(d.txCount||d.tx||0)));
  setText('diag-txerr',(d.txErr||d.txerr||(d.can2&&d.can2.txerr)||d.can2TxErr||0));
  var diagEflg=(d.can2Eflg!==undefined)?d.can2Eflg:((d.can2&&d.can2.eflg!==undefined)?d.can2.eflg:undefined);
  var diagEflgNum=Number(diagEflg);
  var diagEflgValid=(typeof Number.isFinite==='function')?Number.isFinite(diagEflgNum):(typeof diagEflgNum==='number'&&isFinite(diagEflgNum));
  setText('diag-eflg',diagEflgValid?('0x'+diagEflgNum.toString(16).padStart(2,'0').toUpperCase()):'0x00');
  setText('s-fd',d.fd||0);
  setStatusTriplet('module',d.ci?'FSD ON':'FSD OFF','启动保存: '+((d.bootCan!==undefined?d.bootCan:d.ci)?'ON':'OFF'),d.can?'CAN Online':'CAN Offline',d.can?'ok':'err');
  setStatusTriplet('hw',(d.hwName||hwLabel(d.hw)),'mode_hw: '+(d.hwName||hwLabel(d.hw)),(d.can?'CAN运行 / ':'CAN离线 / ')+(d.hwName||hwLabel(d.hw)),d.can?'ok':'warn');
  setStatusTriplet('speed','偏移 '+(d.soff!==undefined?d.soff:'--'),driveLabel(d.sp,d.spAuto),d.fusedSpeedLimitKph?('Fused '+d.fusedSpeedLimitKph+' kph'):'CAN未给出速度',d.can?'ok':'warn');
  setStatusTriplet('defense',d.hw3OffsetSlew?'防御 ON':'防御 OFF','slew '+(d.hw3OffsetSlew?'ON':'OFF'),'触发 '+(d.hw3SlewCount||0)+' / offset '+(d.hw3OffsetLast!==undefined?d.hw3OffsetLast:'--'),d.hw3OffsetSlew?'ok':'warn');

  // FSD page toggle
  updateFsdToggle(d.ci);

  // Quick action FSD button
  var qaBtn=$('qa-fsd');var qaSt=$('qa-fsd-st');
  if(qaBtn){qaBtn.classList.toggle('active',!!d.ci)}
  if(qaSt){qaSt.textContent=d.ci?'ON':'OFF';qaSt.style.color=d.ci?'var(--ok)':'var(--err)'}

  // Boot toggle — show stored boot preference, not current session state
  var bt=$('fsd-boot-tgl');
  if(bt)bt.checked=!!(d.bootCan!==undefined?d.bootCan:d.ci);

  // HW page
  updateHwCards(d.hw);
  updateProfileCards(d.sp);
  updateDriveCards(d.sp,d.spAuto,d.driveProfile,d.driveProfileName);

  // AP restore / injection gate
  var apR=$('hw-ap-restore');
  if(apR)apR.checked=!!d.apAutoRestore;
  updateApGateControl(d);

  // Speed page
  updateSpeedPage(d);

  // Defense page
  updateDefensePage(d);

  // Legacy Real Wiring truth cards
  renderLegacyTruth(d);

  // CAN controller sub-page
  updateCanController(d);

  // AP injection core (standalone driving status)
  renderApInjectionState(d);
}

function updateFsdToggle(ci){
  var tgl=$('fsd-toggle');
  if(tgl){
    if(tgl.classList.contains('cockpit-hold')){
      tgl.classList.toggle('on',!!ci);
    }else if(ci){tgl.className='big-toggle on'}
    else{tgl.className='big-toggle off'}
  }
  setFsdVisualState(!!ci);
}

function updateHwCards(hw){
  var cards=$('hw-cards');
  if(cards){
    var items=cards.querySelectorAll('.sel-card');
    var map=[3,0,1,2];
    for(var i=0;i<items.length;i++){
      items[i].classList.toggle('active',map[i]===hw);
    }
  }
}

function updateProfileCards(sp){
  var cards=$('profile-cards');
  if(!cards)return;
  var items=cards.querySelectorAll('.sel-card');
  for(var i=0;i<items.length;i++){
    items[i].classList.toggle('active',i===sp);
  }
}

function driveModeFromProfile(profile,name){
  var modes=['auto','sloth','chill','normal','hurry','max'];
  if(profile!==undefined&&profile!==null&&profile>=0&&profile<modes.length)return modes[profile];
  var n=String(name||'').toLowerCase();
  return modes.indexOf(n)>=0?n:'normal';
}
function driveModeLabel(mode){
  return {auto:'Auto',sloth:'Sloth',chill:'Chill',normal:'Normal',hurry:'Hurry',max:'MAX'}[mode]||'Normal';
}
function updateDriveCards(sp,spa,profile,profileName){
  var cards=$('drive-cards');
  if(!cards)return;
  var active=(profile!==undefined&&profile!==null)?driveModeFromProfile(profile,profileName):(spa?'auto':(sp===0?'chill':(sp===2?'hurry':'normal')));
  updateDriveCardsByMode(active);
}

function updateDriveSurfaces(mode, statusText){
  var label=driveModeLabel(mode);
  setText('drive-current',label);
  setText('st-drive-pill',label);
  setText('ov-drive-current',label);
  setText('m-drive',label);
  setStatusTriplet('drive',label,label,statusText||(mode==='max'?'MAX/V14 需实车验证':'配置已同步'),mode==='max'?'warn':'ok');
  var v14=$('ov-fsd-tgl');if(v14)v14.checked=(mode==='max');
  setCls('ov-v14','stat-val '+(mode==='max'?'v-warn':'v-dim'));
  setText('ov-v14',mode==='max'?'MAX/V14':'待机');
  setText('tb-exp',experimentSummary());
}

function updateDriveCardsByMode(active){
  var cards=$('drive-cards');
  var items=cards?cards.querySelectorAll('.drive-card'):[];
  var modes=['auto','sloth','chill','normal','hurry','max'];
  for(var i=0;i<items.length;i++)items[i].classList.toggle('active',modes[i]===active);
  updateDriveSurfaces(active);
}

async function loadDriveProfile(){
  var d=await fetchJson('/drive_profile');
  if(!d)return;
  var mode=driveModeFromProfile(d.value,d.profile);
  S.driveProfile=d.value!==undefined?d.value:S.driveProfile;
  updateDriveCardsByMode(mode);
  updateDriveSurfaces(mode,'配置已同步');
}

var speedStrategyState='auto';
var speedManualPct=30;

function speedPctVal(id,def){
  var el=$(id);
  var v=el?parseInt(el.value,10):def;
  if(isNaN(v))v=def;
  if(v<0)v=0;
  if(v>50)v=50;
  if(el)el.value=String(v);
  return v;
}

function updateSpeedFixedCards(pct){
  var cards=$('speed-fixed-pct');
  if(!cards)return;
  var items=cards.querySelectorAll('.sel-card');
  var values=[0,10,20,30,40,50];
  for(var i=0;i<items.length;i++)items[i].classList.toggle('active',values[i]===pct);
}

function showSpeedMode(strategy){
  speedStrategyState=strategy||'auto';
  var tabs=$('speed-mode-tabs');
  if(tabs){
    var items=tabs.querySelectorAll('.sel-card');
    var values=['fixed','auto','custom'];
    for(var i=0;i<items.length;i++)items[i].classList.toggle('active',values[i]===speedStrategyState);
  }
  var fixed=$('speed-panel-fixed'),auto=$('speed-panel-auto'),custom=$('speed-panel-custom');
  if(fixed)fixed.style.display=speedStrategyState==='fixed'?'block':'none';
  if(auto)auto.style.display=speedStrategyState==='auto'?'block':'none';
  if(custom)custom.style.display=speedStrategyState==='custom'?'block':'none';
  setText('speed-current',speedStrategyState);
  setText('st-speed-pill',speedStrategyState);
}

function legacySmartModeValue(v){
  if(v==='off'||v===0)return 'off';
  if(v==='manual'||v===1)return 'manual';
  if(v==='auto'||v===2)return 'auto';
  if(v==='custom'||v===3)return 'custom';
  return 'auto';
}
function clampNum(id,def,min,max){
  var el=$(id);var v=el?parseInt(el.value,10):def;
  if(isNaN(v))v=def;if(v<min)v=min;if(v>max)v=max;
  if(el)el.value=String(v);
  return v;
}
function updateSpeedPage(d){
  var enc=$('hw3-enc');
  if(enc)enc.value=String(d.hw3WireEncoding||0);
  var speedLimit=(d.speedLimit!==undefined&&d.speedLimit>0)?d.speedLimit:(d.fusedSpeedLimitKph||0);
  setText('sp-limit',speedLimit?speedLimit+' kph':'--');
  setText('sp-act-offset',d.actOffset!==undefined?d.actOffset+' kph':'--');
  setText('sp-active-mode',speedStrategyState);
  setText('sp-wire',d.hw3WireEncoding!==undefined?d.hw3WireEncoding:'--');
  setText('sp-stock',d.hw3StockOffset!==undefined?d.hw3StockOffset+' kph':'--');
  setText('sp-raw',d.fusedSpeedLimitRaw!==undefined?d.fusedSpeedLimitRaw:'--');
  var ls=d.legacySpeed||{};
  var seen=!!ls.gpsSpeedSeen;
  var gpsText=seen?'已检测 0x2F8':'0x2F8 未检测到';
  setText('legacy-speed-chip',gpsText);
  setText('legacy-gps-seen',gpsText);
  setCls('legacy-gps-seen','v-'+(seen?'ok':'warn'));
  setText('legacy-limit-kph',ls.speedLimitKph!==undefined?ls.speedLimitKph+' kph':'--');
  setText('legacy-target-kph',ls.rawTargetKph!==undefined?ls.rawTargetKph+' kph':'--');
  setText('legacy-smooth-kph',ls.smoothedTargetKph!==undefined?ls.smoothedTargetKph+' kph':'--');
  setText('legacy-output-kph',ls.outputOffsetKph!==undefined?ls.outputOffsetKph+' kph':'--');
  setText('legacy-last-raw',ls.lastSentOffsetRaw!==undefined?('raw '+ls.lastSentOffsetRaw+' / '+(ls.lastSentOffsetKph!==undefined?ls.lastSentOffsetKph+' kph':'--')):'--');
}

function updateDefensePage(d){
  var tgl=$('hw3-slew-tgl');
  if(tgl)tgl.checked=!!d.hw3OffsetSlew;
  setText('def-rate',d.hw3SlewRate!==undefined?d.hw3SlewRate+'%/s':'--');
  setText('def-min',d.hw3OffsetTarget!==undefined?d.hw3OffsetTarget:'--');
  setText('def-cnt',d.hw3SlewCount||0);
  setText('def-cur',d.hw3OffsetLast!==undefined?d.hw3OffsetLast:'--');
  var ag=d.abortGuard||{};
  var agState=ag.enabled?(ag.latched?'已锁止':'已武装'):'关闭';
  setText('abort-guard-state',agState);
  setText('abort-guard-ap',ag.lastApState!==undefined?ag.lastApState:'--');
  setText('abort-guard-abort',ag.lastAbortState!==undefined?ag.lastAbortState:'--');
  setText('abort-guard-blocks',ag.blocks||0);
  setText('abort-guard-path',ag.lastBlockedPath||ag.lastClearReason||'--');
  var dot=$('def-dot');
  var statusEl=$('def-status');
  if(d.hw3OffsetSlew){
    if(dot)dot.className='status-dot ok';
    setText('def-status',T('保护已启用'));
  }else{
    if(dot)dot.className='status-dot err';
    setText('def-status',T('保护未启用'));
  }
}

async function loadDefenseConfig(){
  var d=await fetchJson('/defense_config');
  if(!d)return;
  var tgl=$('hw3-slew-tgl');
  if(tgl)tgl.checked=!!d.enabled;
  var bio=$('def-bionic-tgl');if(bio)bio.checked=!!d.bionic_steering;
  var bioRisk=$('def-bionic-risk');if(bioRisk)bioRisk.style.display=!!d.bionic_steering?'block':'none';
  var conf=(d&&d.defense)?d:{defense:{nagMode:(d&&d.nagMode!=null)?d.nagMode:((d&&d.nag_mode!=null)?d.nag_mode:0)}};
  setVal('nag-mode-select', String((d&&d.nag_mode!=null)?d.nag_mode:((conf&&conf.defense&&conf.defense.nagMode!=null)?conf.defense.nagMode:0)));
  var ntt=$('def-ntt-tgl');if(ntt)ntt.checked=!!d.nag_torque_tamper;
  var nttWarn=$('def-ntt-warn');if(nttWarn)nttWarn.style.display=!!d.nag_torque_tamper?'block':'none';
  var se=$('def-soft-engage-tgl');if(se)se.checked=!!d.soft_engage;
  var ag=$('abort-guard-toggle');if(ag)ag.checked=!!d.abort_guard;
  // Bionic auto-disabled warning
  var bioWarn=$('def-bionic-warn');
  if(bioWarn)bioWarn.style.display=!!d.bionic_disabled?'block':'none';
  var sound=$('def-sound-tgl');if(sound)sound.checked=!!d.sound_warning_suppression;
  var isaOvr=$('def-isa-override-tgl');if(isaOvr)isaOvr.checked=!!d.isa_override;
  var dndVol=$('def-dnd-vol-tgl');if(dndVol)dndVol.checked=!!d.dnd_volume;
  var nd=$('def-speed-nd-tgl');if(nd)nd.checked=!!d.speed_no_disturb;
  var dndSpd=$('def-dnd-spd-tgl');if(dndSpd)dndSpd.checked=!!d.dnd_speed;
  var apeap=$('def-apeap-tgl');if(apeap)apeap.checked=!!d.ap_eap_compatible;
  setText('def-status',d.enabled?T('保护已启用'):T('保护未启用'));
  var dot=$('def-dot');if(dot)dot.className='status-dot '+(d.enabled?'ok':'err');
  var exp=(d.abort_guard||d.bionic_steering||d.speed_no_disturb||d.ap_eap_compatible||d.dnd_volume||d.dnd_speed);
  setStatusTriplet('defense',d.enabled?'防御 ON':'防御 OFF',
    'NVS '+(d.enabled?'ON':'OFF')+(exp?' / 含实验项':''),
    exp?'实验项需实车验证':'等待 /status 运行确认',
    exp?'warn':(d.enabled?'ok':'warn'));
  setText('tb-exp',experimentSummary());
}

// ── FSD Toggle ─────────────────────────────────────────────
async function toggleFsd(){
  var next=S.ci?0:1;
  if(next&&!confirm('确认开启 FSD 注入？')){poll();return}
  // 主开关同时更新当前会话和开机默认值，重启后保持一致。
  try{await postForm('/config',{can:next?'1':'0',force:next?'1':'0',bootCan:next?'1':'0'});}
  catch(e){return}
  S.ci=!!next;
  S.bootCan=!!next;
  var masterTgl=$('ov-master-tgl');
  if(masterTgl)masterTgl.checked=S.ci;
  var mFsdTgl=$('m-fsd-tgl');
  if(mFsdTgl){
    if(mFsdTgl.type==='checkbox')mFsdTgl.checked=S.ci;
    else if(mFsdTgl.classList&&mFsdTgl.classList.contains('mobile-hold'))mFsdTgl.className='mobile-hold '+(S.ci?'on':'');
    else if(mFsdTgl.classList)mFsdTgl.classList.toggle('on',S.ci);
  }
  var bootTgl=$('fsd-boot-tgl');
  if(bootTgl)bootTgl.checked=S.bootCan;
  updateFsdToggle(S.ci);
  if(masterTgl)setFsdVisualState(!!masterTgl.checked);
  poll();
}

async function toggleV14Mode(){
  var t=$('ov-fsd-tgl');
  var enable=!!(t&&t.checked);
  if(enable&&!confirm('确认开启 MAX/V14 实验模式？')){if(t)t.checked=false;return}
  try{await postForm('/drive_profile',{profile:enable?'max':'normal'});}
  catch(e){if(t)t.checked=!enable;return}
  S.driveProfile=enable?5:3;
  updateDriveCardsByMode(enable?'max':'normal');
  setCls('ov-v14','stat-val '+(enable?'v-warn':'v-dim'));
  setText('ov-v14',enable?'MAX/V14':'待机');
  setText('tb-exp',experimentSummary());
}

async function resetStats(){
  try{
    await postForm('/reset_stats',{});
    showToast(T('已保存')||'OK',true);
    setTimeout(function(){hideToast();poll()},500);
  }catch(e){}
}

async function rebootDevice(){
  if(!confirm(T('确认重启设备？')||'Reboot device?'))return;
  try{
    await postForm('/reboot',{});
    showToast(T('重启设备')||'Rebooting...');
  }catch(e){}
}

// ── HW Selection ───────────────────────────────────────────
async function setHW(hw){
  try{await postForm('/mode_hw',{value:String(hw)});}
  catch(e){return}
  S.hw=hw;
  updateHwCards(hw);
  setStatusTriplet('hw',hwLabel(hw),'mode_hw: '+hwLabel(hw),'等待 /status 确认','warn');
}

// ── Profile Selection ──────────────────────────────────────
async function setProfile(sp){
  var names=['Chill','Normal','Hurry'];
  try{await postForm('/drive_profile',{profile:names[sp]||'Normal'});}
  catch(e){return}
  S.sp=sp;S.spa=false;
  updateProfileCards(sp);
  updateDriveCards(sp,false);
}

async function setDriveMode(mode){
  var driveMap={auto:0,sloth:1,chill:2,normal:3,hurry:4,max:5};
  if(driveMap[mode]===undefined)mode='normal';
  updateDriveCardsByMode(mode);
  updateDriveSurfaces(mode,'保存中...');
  var saved;
  try{saved=await postForm('/drive_profile',{profile:mode});}
  catch(e){setStatusTriplet('drive',driveModeLabel(mode),'保存失败','请检查设备连接','err');return}
  var savedMode=saved&&saved.value!==undefined?driveModeFromProfile(saved.value,saved.profile):mode;
  S.spa=savedMode==='auto';
  S.sp=(savedMode==='sloth'||savedMode==='chill')?0:((savedMode==='hurry'||savedMode==='max')?2:1);
  S.driveProfile=driveMap[savedMode]!==undefined?driveMap[savedMode]:3;
  updateDriveCardsByMode(savedMode);
  updateProfileCards(S.sp);
  updateDriveSurfaces(savedMode,'已保存');
  setTimeout(poll,300);
}

function updateSpeedStrategyCards(strategy){
  showSpeedMode(strategy||'auto');
}

function applySpeedCustom(d){
  if(!d)return;
  if(d.manualPct!==undefined)speedManualPct=parseInt(d.manualPct,10)||0;
  updateSpeedFixedCards(speedManualPct);
  var vals=d.customPct||[d.cp1,d.cp2,d.cp3,d.cp4];
  for(var i=0;i<4;i++){
    var el=$('speed-cp'+(i+1));
    if(el&&vals&&vals[i]!==undefined)el.value=vals[i];
  }
}

async function loadSpeedStrategy(){
  var strategyResp=await fetchJson('/speed_strategy');
  var customResp=await fetchJson('/speed_custom');
  if(customResp)applySpeedCustom(customResp);
  var strategy=(strategyResp&&strategyResp.strategy)||'auto';
  showSpeedMode(strategy);
  setStatusTriplet('speed',strategy,strategy,'等待 /status 速度确认',strategy==='custom'?'warn':'ok');
}

async function setSpeedStrategy(strategy){
  showSpeedMode(strategy);
  try{await postForm('/speed_strategy',{strategy:strategy});showToast(T('已保存')||'Saved',true)}
  catch(e){loadSpeedStrategy();return}
  setStatusTriplet('speed',strategy,strategy,'等待 /status 速度确认',strategy==='custom'?'warn':'ok');
}

async function setSpeedFixedPct(pct){
  speedManualPct=pct;
  showSpeedMode('fixed');
  updateSpeedFixedCards(pct);
  try{
    await postForm('/speed_strategy',{strategy:'fixed'});
    await postForm('/speed_custom',{manualPct:String(pct)});
    showToast(T('已保存')||'Saved',true);
  }catch(e){loadSpeedStrategy();return}
  setStatusTriplet('speed','fixed','manualPct '+pct+'%','等待 /status 速度确认','ok');
}

async function saveSpeedCustom(){
  var data={
    cp1:String(speedPctVal('speed-cp1',30)),
    cp2:String(speedPctVal('speed-cp2',20)),
    cp3:String(speedPctVal('speed-cp3',10)),
    cp4:String(speedPctVal('speed-cp4',10))
  };
  showSpeedMode('custom');
  try{
    await postForm('/speed_strategy',{strategy:'custom'});
    var d=await postForm('/speed_custom',data);
    applySpeedCustom(d);
    showToast(T('已保存')||'Saved',true);
  }catch(e){loadSpeedStrategy();return}
  setStatusTriplet('speed','custom','cp1-cp4 已保存','等待 /status 速度确认','warn');
}

// ── Save Config (generic toggle) ───────────────────────────
async function saveConfig(){
  var data={};
  var apR=$('hw-ap-restore');
  if(apR)data.apRestore=apR.checked?'1':'0';
  var bt=$('fsd-boot-tgl');
  if(bt)data.bootCan=bt.checked?'1':'0';
  try{await postForm('/config',data);}catch(e){}
}
function updateApGateControl(d){
  var gate=$('ap-gate-tgl');
  if(gate&&d)gate.checked=!!d.apGateEnabled;
}
async function saveApGate(){
  var gate=$('ap-gate-tgl');
  try{await postForm('/config',{apg:gate&&gate.checked?'1':'0'});}
  catch(e){if(gate)gate.checked=!gate.checked}
}

// ── AP injection core controls (standalone driving status) ────────────
async function saveApGateControls(){
  var gate=document.getElementById('ap-core-gate-tgl');
  var delay=document.getElementById('ap-delay-select');
  var restore=document.getElementById('ap-auto-restore-tgl');
  try{
    await postForm('/config',{
      apg:gate&&gate.checked?'1':'0',
      ap_delay_ms:delay?delay.value:'2000',
      ap_auto_restore:restore&&restore.checked?'1':'0'
    });
    showToast(T('已保存'));
  }catch(e){}
}
async function saveApDelay(src){
  try{
    await postForm('/config',{ap_delay_ms:src?src.value:'2000'});
    document.querySelectorAll('.ap-delay-select').forEach(function(s){if(s!==src)s.value=src.value;});
    showToast(T('已保存'));
  }catch(e){}
}
function renderApInjectionState(d){
  if(!d)return;
  var gate=(d.fsdDiag&&d.fsdDiag.gate)?d.fsdDiag.gate:{};
  var state=d.apInjectionState||'blocked';
  var label=state==='waiting_ap'?'等待 AP':state==='settling'?'稳定计时中':state==='injecting'?'正在注入':'已阻断';
  setText('ap-core-state-pill',label);
  // 驾驶舱状态主区 + Gate 进度条（仅显示层，不读新后端字段）
  var apActive = (state==='injecting'||state==='settling');
  var apBig = document.getElementById('ap-core-state-big');
  var apPanel = document.getElementById('ap-state-panel');
  var apFill = document.getElementById('ap-gate-fill');
  if (apBig) apBig.textContent = apActive ? '激活' : '等待';
  if (apPanel) apPanel.classList.toggle('active', apActive);
  if (apFill) apFill.style.width = (apActive ? '100%' : '0%');
  var stable=gate.apStableMs||0, req=gate.requiredStableMs||d.apDelayMs||2000;
  var reason=gate.lastBlockedBy||gate.gateReason||'';
  setText('ap-core-state-detail',label+' · AP '+stable+'/'+req+' ms'+(reason?' · '+reason:''));
  setText('injection-source',d.injectionSource||'Disabled');
  var apg=document.getElementById('ap-core-gate-tgl'); if(apg)apg.checked=!!d.apGateEnabled;
  var delayVal=(d.apDelayMs!=null&&d.apDelayMs!==undefined)?d.apDelayMs:(req||2000);
  document.querySelectorAll('.ap-delay-select').forEach(function(s){if(document.activeElement!==s)s.value=String(delayVal);});
  var rst=document.getElementById('ap-auto-restore-tgl'); if(rst)rst.checked=!!d.apAutoRestore;
}
// ── 插件管理（JSON 插件） ──────────────────────────────────────
var PLUGINS={installed:[],replayCount:1};
function loadPlugins(){
  return fetch('/plugins/status').then(function(r){return r.json()}).then(function(j){PLUGINS=j;renderPluginsStatus(j)}).catch(function(){});
}
function installPluginUrl(){
  var v=document.getElementById('plugin-url').value;
  return postForm('/plugins/install_url',{url:v}).then(loadPlugins);
}
function installPluginJson(){
  var v=document.getElementById('plugin-json').value;
  return fetch('/plugins/install_json',{method:'POST',headers:{'Content-Type':'application/json'},body:v}).then(loadPlugins);
}
function uploadPluginJson(){
  var f=document.getElementById('plugin-file').files[0];
  var st=document.getElementById('plugin-upload-status');
  if(!f){if(st)st.textContent='请先选择 .json 文件';return;}
  if(st)st.textContent='上传中...';
  fetch('/plugins/upload',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f}).then(function(r){
    return r.text().then(function(t){
      var msg='HTTP '+r.status;
      try{var j=JSON.parse(t);if(j&&j.message)msg=j.message;}catch(e){}
      if(r.ok){if(st)st.textContent='安装成功';return loadPlugins();}
      if(st)st.textContent='安装失败：'+(f&&f.name?f.name+' ':'')+msg;
    });
  }).catch(function(e){
    if(st)st.textContent='安装失败：'+(f&&f.name?f.name+' ':'')+(e&&e.message?e.message:e);
  });
}
function togglePlugin(name,on){return postForm('/plugins/toggle',{name:name,enabled:on?'1':'0'}).then(loadPlugins)}
function setPluginPriority(name,priority){return postForm('/plugins/priority',{name:name,priority:priority}).then(loadPlugins)}
function removePlugin(name){return postForm('/plugins/remove',{name:name}).then(loadPlugins)}
function setPluginReplayCount(count){return postForm('/plugins/replay_count',{count:count}).then(loadPlugins)}
function runPluginRuleTest(){
  return fetch('/plugins/rule_test',{method:'POST'}).then(function(r){return r.text()}).then(function(t){setText('plugin-rule-test-result',t)});
}
function renderPluginsStatus(j){
  var list=document.getElementById('plugin-list'); if(!list)return;
  var html=(j.installed||[]).map(function(p){
    var name=String(p.name||'');
    var ids=(p.targetIds||[]).join(', ');
    var conflicts=(p.conflicts||[]).join(' · ');
    return '<div class="plugin-item">'
      +'<div class="pi-head"><div><span class="pi-name">'+esc(name)+'</span> <small class="pi-ver">'+esc(p.version||'1.0')+'</small></div>'
      +'<div class="pi-controls">'
      +'<label class="tgl" title="启用"><input type="checkbox" '+(p.enabled?'checked':'')+' onchange="togglePlugin(\''+escAttr(name)+'\',this.checked)"><div class="tgl-track"></div></label>'
      +'<input class="mock-input pi-prio" type="number" min="1" max="8" value="'+(p.priority||1)+'" onchange="setPluginPriority(\''+escAttr(name)+'\',this.value)" title="优先级">'
      +'<button class="ds-btn ds-btn-danger" onclick="removePlugin(\''+escAttr(name)+'\')">删除</button>'
      +'</div></div>'
      +'<div class="pi-hint">目标ID: '+esc(ids)+' · 规则: '+(p.ruleCount||0)+'</div>'
      +(conflicts||p.lastError ? '<div class="pi-warn">'+esc(conflicts||p.lastError)+'</div>' : '')
      +'<details class="collapse pi-details"><summary>详情</summary><pre>'+esc(JSON.stringify(p,null,2))+'</pre></details>'
      +'</div>';
  }).join('');
  list.innerHTML=html||'<div class="hint">暂无插件。新安装默认禁用。</div>';
  var rc=document.getElementById('plugin-replay-count'); if(rc)rc.value=String(j.replayCount||1);
  var pill=document.getElementById('plugins-count-pill'); if(pill)pill.textContent='已装 '+((j.installed||[]).length)+' 个';
}
// ── HW3 Speed Save ─────────────────────────────────────────
async function saveHw3Speed(){
  var data={};
  var ctTgl=$('hw3-ct-tgl');
  if(ctTgl)data.hw3CustomSpeed=ctTgl.checked?'1':'0';
  var buckets=[30,40,50,60,70];
  for(var i=0;i<5;i++){
    var inp=$('hw3-b'+buckets[i]);
    if(inp)data['hw3CustomT'+i]=inp.value;
  }
  var hsb=[80,100,120];
  for(var i=0;i<3;i++){
    var inp=$('hw3-b'+hsb[i]);
    if(inp)data['hw3HighTarget'+i]=inp.value;
  }
  var enc=$('hw3-enc');
  if(enc)data.hw3WireEncoding=enc.value;
  try{await postForm('/config',data);}catch(e){}
}

// ── HW3 Slew Save ──────────────────────────────────────────
async function saveHw3Slew(){
  saveDefenseConfig();
}

async function saveDefenseConfig(){
  var tgl=$('hw3-slew-tgl');
  var bio=$('def-bionic-tgl');
  var ntt=$('def-ntt-tgl');
  var se=$('def-soft-engage-tgl');
  var ag=$('abort-guard-toggle');
  var sound=$('def-sound-tgl');
  var isaOvr=$('def-isa-override-tgl');
  var dndVol=$('def-dnd-vol-tgl');
  var nd=$('def-speed-nd-tgl');
  var dndSpd=$('def-dnd-spd-tgl');
  var apeap=$('def-apeap-tgl');
  var data={
    enabled:tgl&&tgl.checked?'1':'0',
    bionic_steering:bio&&bio.checked?'1':'0',
    nagMode: parseInt(val('nag-mode-select')||'0',10),
    nag_torque_tamper:ntt&&ntt.checked?'1':'0',
    soft_engage:se&&se.checked?'1':'0',
    abort_guard:ag&&ag.checked?'1':'0',
    sound_warning_suppression:sound&&sound.checked?'1':'0',
    isa_override:isaOvr&&isaOvr.checked?'1':'0',
    dnd_volume:dndVol&&dndVol.checked?'1':'0',
    speed_no_disturb:nd&&nd.checked?'1':'0',
    dnd_speed:dndSpd&&dndSpd.checked?'1':'0',
    ap_eap_compatible:apeap&&apeap.checked?'1':'0'
  };
  try{await postForm('/defense_config',data);}
  catch(e){loadDefenseConfig()}
  loadDefenseConfig();
}

// ── CAN2 ───────────────────────────────────────────────────
// Phase 1: 功耗管理 toggle
async function toggleAutoShutdown(on){
  await postForm('/power_mgmt','autoShutdown='+on);
  poll();
}
async function toggleWifiAutoOff(on){
  await postForm('/power_mgmt','wifiAutoOff='+on);
  poll();
}

async function pollCAN2(){
  var d=await fetchJson('/bus2_ids');
  if(!d)return;
  setCls('b2-status','stat-val '+(d.count>0?'v-ok':'v-dim'));
  setText('b2-status',d.count>0?'Online':'Idle');
  setText('b2-rx',d.rx_total||0);
  var c2=d.can2||{};
  setText('b2-tx',c2.tx||0);
  setText('b2-txerr',c2.txerr||0);
  setText('b2-eflg','0x'+toHex(c2.eflg||0,2));
  setCls('b2-txerr','stat-val '+((c2.txerr||0)>0?'v-warn':'v-dim'));
  setCls('b2-eflg','stat-val '+((c2.eflg||0)>0?'v-err':'v-dim'));
  setText('b2-ids',d.count||0);
  setText('b2-count','('+d.count+')');
  setCls('m-can2','stat-val '+(d.count>0?'v-ok':'v-dim'));
  setText('m-can2',d.count>0?'Online':'Idle');
  var sCan=$('s-can');
  setText('ov-can',(sCan&&sCan.textContent==='Online'?'CAN1 Online':'CAN1 Offline')+' / '+(d.count>0?'CAN2 Online':'CAN2 Idle'));
  var svc=$('svc-mode-tgl');
  if(svc)svc.checked=!!d.service_mode;
  loadBurstConfig();
  setStatusTriplet('light',
    ($('light-enabled-tgl')&&$('light-enabled-tgl').checked)?'爆闪 ON':'爆闪 OFF',
    '规则 '+(lightPreset||3)+'x '+(lightFrequency||'medium'),
    (d.count>0?'CAN2 Online':'CAN2 Idle')+' / RX '+(d.rx_total||0),
    d.count>0?'warn':'warn');
  var rows='';
  if(d.ids){
    for(var i=0;i<d.ids.length;i++){
      var id=d.ids[i];
      rows+='<tr><td class="hex">'+escHtml(id.id)+'</td><td>'+id.dlc+'</td>'
        +'<td class="hex">'+escHtml(id.data)+'</td><td>'+id.count+'</td></tr>';
    }
  }
  setHtml('b2-rows',rows);
}

function setLightPreset(count){
  lightPreset=count;
  var cards=$('light-preset');
  if(cards){
    var items=cards.querySelectorAll('.sel-card');
    var values=[3,5,7,10];
    for(var i=0;i<items.length;i++)items[i].classList.toggle('active',values[i]===count);
  }
  var dur=$('stalk-dur');
  if(dur)dur.value=count>=7?'300':(count>=5?'400':'500');
  saveLightingConfig();
}

function updateLightPreset(count){
  var cards=$('light-preset');
  if(!cards)return;
  var items=cards.querySelectorAll('.sel-card');
  var values=[3,5,7,10];
  for(var i=0;i<items.length;i++)items[i].classList.toggle('active',values[i]===count);
}

function updateLightOptionCards(id,active,values){
  var cards=$(id);
  if(!cards)return;
  var items=cards.querySelectorAll('.sel-card');
  for(var i=0;i<items.length;i++)items[i].classList.toggle('active',values[i]===active);
}

function detectBurstPreset(d){
  for(var k in BURST_PRESETS){var p=BURST_PRESETS[k];if(p.count===d.count&&p.on_ms===d.on_ms&&p.off_ms===d.off_ms)return k}
  return burstPreset||'B';
}
function updateBurstPresetCards(){
  ['burst-preset','burst-preset-strobe'].forEach(function(id){
    var cards=$(id);if(!cards)return;
    var items=cards.querySelectorAll('.sel-card');
    var keys=['A','B','C','D'];
    for(var i=0;i<items.length;i++)items[i].classList.toggle('active',keys[i]===burstPreset);
  });
}
function updateBurstUi(d){
  if(!d)return;
  burstPreset=detectBurstPreset(d);
  updateBurstPresetCards();
  var enabled=!!d.enabled;
  var t=$('burst-enabled-tgl');if(t)t.checked=enabled;
  var ts=$('burst-enabled-tgl-strobe');if(ts)ts.checked=enabled;
  var running=!!d.running;
  var status=enabled?(running?'运行中':'等待双拨'):'关闭';
  setText('burst-status',status);
  setText('burst-status-strobe',status);
  setText('burst-params',(d.count||0)+'× '+(d.on_ms||0)+'/'+(d.off_ms||0)+'ms');
  setText('burst-phases',d.phases_left||0);
  setCls('burst-status','stat-val '+(running?'v-warn':(enabled?'v-ok':'v-dim')));
  setCls('burst-phases','stat-val '+((d.phases_left||0)>0?'v-warn':'v-dim'));
}
async function loadBurstConfig(){
  var d=await fetchJson('/burst');
  if(d)updateBurstUi(d);
}
async function saveBurstConfig(enabled){
  var p=BURST_PRESETS[burstPreset]||BURST_PRESETS.B;
  try{
    var d=await postForm('/burst',{enabled:enabled?'1':'0',count:String(p.count),on_ms:String(p.on_ms),off_ms:String(p.off_ms)});
    if(d)updateBurstUi(d);
  }catch(e){}
}
function toggleBurst(src){
  var t=$('burst-enabled-tgl');
  var ts=$('burst-enabled-tgl-strobe');
  var enabled=src?!!src.checked:!!((t&&t.checked)||(ts&&ts.checked));
  if(t)t.checked=enabled;
  if(ts)ts.checked=enabled;
  saveBurstConfig(enabled);
}
function setBurstPreset(id){
  if(!BURST_PRESETS[id])id='B';
  burstPreset=id;
  updateBurstPresetCards();
  var enabled=(($('burst-enabled-tgl')&&$('burst-enabled-tgl').checked)||($('burst-enabled-tgl-strobe')&&$('burst-enabled-tgl-strobe').checked));
  saveBurstConfig(enabled);
}

async function loadLightingConfig(){
  var d=await fetchJson('/lighting_config');
  if(!d)return;
  lightPreset=d.count||3;
  lightFrequency=d.frequency||'medium';
  rearFogStrategy=d.rear_fog_strategy||'off';
  updateLightPreset(lightPreset);
  updateLightOptionCards('light-frequency',lightFrequency,['slow','medium','fast']);
  updateLightOptionCards('rear-fog-strategy',rearFogStrategy,['off','strobe','continuous']);
  var t=$('light-enabled-tgl');if(t)t.checked=!!d.enabled;
  setStatusTriplet('light',d.enabled?'爆闪 ON':'爆闪 OFF',
    (d.count||3)+'x '+(d.frequency||'medium')+' / fog '+rearFogStrategy,
    '等待 CAN2 运行确认',
    d.enabled?'warn':'ok');
  setText('tb-exp',experimentSummary());
}

async function saveLightingConfig(){
  var t=$('light-enabled-tgl');
  try{await postForm('/lighting_config',{enabled:t&&t.checked?'1':'0',count:String(lightPreset),frequency:lightFrequency,rear_fog_strategy:rearFogStrategy});}
  catch(e){}
  loadLightingConfig();
}

function setLightFrequency(freq){
  lightFrequency=freq;
  updateLightOptionCards('light-frequency',freq,['slow','medium','fast']);
  saveLightingConfig();
}

function setRearFogStrategy(strategy){
  rearFogStrategy=strategy;
  updateLightOptionCards('rear-fog-strategy',strategy,['off','strobe','continuous']);
  saveLightingConfig();
}

function lightDelayMs(){
  return lightFrequency==='fast'?180:(lightFrequency==='slow'?650:350);
}

// ── Phase 4: Strobe / Fog Light ──────────────────────────────
async function loadStrobePage(){
  var d=await fetchJson('/lighting_config');
  if(!d)return;
  var sel=$('fog-strategy');if(sel)sel.value=d.rear_fog_value||0;
  var cnt=$('strobe-count');if(cnt)cnt.value=d.count||3;
  var frq=$('strobe-freq');if(frq)frq.value=d.frequency_value||1;
  // Get active state from execution endpoint plus gear from status.
  var f=await fetchJson('/fog_light');
  if(f)setText('strobe-status',f.active?'运行中':(f.reason||'待触发'));
  var s=await fetchJson('/status');
  if(s){
    var gears=['','P','R','N','D','','','','SNA'];
    setText('strobe-gear',s.gear!==undefined?gears[s.gear]||s.gear:'--');
  }
}

async function fogTrigger(mode){
  try{
    var r=await postForm('/fog_light',{trigger:mode});
    if(r)setText('strobe-status',r.active?'运行中':(r.reason||'已发送'));
  }
  catch(e){setText('strobe-status','执行失败')}
  loadStrobePage();
}

async function saveFogStrategy(){
  var sel=$('fog-strategy');
  if(!sel)return;
  try{await postForm('/fog_light',{fogStrategy:sel.value});}
  catch(e){}
}

async function strobeTest(mode){
  if(!confirm('确认执行灯光注入序列？'))return;
  var t=$('light-enabled-tgl');
  if(t&&!t.checked){t.checked=true;await saveLightingConfig()}
  var count=Math.max(1,Math.min(10,lightPreset||3));
  setText('stalk-status','Strobe 0/'+count);
  for(var i=0;i<count;i++){
    await stalkTest(mode,true);
    setText('stalk-status','Strobe '+(i+1)+'/'+count);
    if(i<count-1)await new Promise(function(resolve){setTimeout(resolve,lightDelayMs())});
  }
  setText('stalk-status',T('空闲')+' ('+count+'x)');
}

async function stalkTest(mode,skipConfirm){
  if(!skipConfirm&&!confirm('确认执行灯光注入？'))return;
  var dur=$('stalk-dur');
  var durVal=dur?Math.max(100,Math.min(3000,parseInt(dur.value||'500',10)||500)):500;
  if(dur)dur.value=durVal;
  setText('stalk-status',T('测试中...')||'Testing...');
  try{
    var r=await fetch('/stalk_test?mode='+(mode==='PULL'?'flash':'highbeam')+'&dur='+durVal);
    if(!r.ok)throw new Error('HTTP '+r.status);
    var res=await r.json();
    if(!res.ok)throw new Error(res.error||'stalk test failed');
    setText('stalk-status',T('空闲')+' ('+res.duration_ms+'ms)');
  }catch(e){setText('stalk-status','Error');showToast((e&&e.message)?e.message:T('请求失败'))}
}

async function toggleServiceMode(){
  var tgl=$('svc-mode-tgl');
  try{await postForm('/service_mode',{on:tgl&&tgl.checked?'1':'0'});}
  catch(e){if(tgl)tgl.checked=!tgl.checked}
}

// ── OTA Upload ─────────────────────────────────────────────
async function uploadFirmware(){
  var fileInput=$('ota-file');
  if(!fileInput||!fileInput.files||fileInput.files.length===0)return;
  if(!confirm('确认上传 OTA 固件？上传完成可能会重启设备。'))return;
  var file=fileInput.files[0];
  var prog=$('ota-progress');
  var bar=$('ota-bar');
  var pct=$('ota-pct');
  var btn=$('ota-btn');
  if(prog)prog.style.display='block';
  if(btn)btn.disabled=true;

  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update',true);
  // Fetch OTA credentials from backend (AP-local only) instead of hardcoding.
  try{var c=await(await fetch('/ota_creds')).json();xhr.setRequestHeader('Authorization','Basic '+btoa(c.u+':'+c.p))}catch(e){}
  xhr.setRequestHeader('Content-Type','application/octet-stream');
  xhr.setRequestHeader('X-File-Name',encodeURIComponent(file.name||'firmware.bin'));

  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){
      var p=Math.round(e.loaded/e.total*100);
      if(bar)bar.style.width=p+'%';
      if(pct)setText('ota-pct',p+'%');
    }
  };
  xhr.onload=function(){
    if(btn)btn.disabled=false;
    if(bar)bar.style.width='100%';
    if(pct)setText('ota-pct',xhr.status===200?'OK!':'Error');
  };
  xhr.onerror=function(){
    if(btn)btn.disabled=false;
    if(pct)setText('ota-pct','Error');
  };
  xhr.send(file);
}

// OTA drag-drop setup
function resetOtaCredentials(){
  localStorage.removeItem('otaU');
  localStorage.removeItem('otaP');
  showToast('OTA 凭据缓存已清除',true);
}
function setupOtaDrop(){
  var drop=$('ota-drop');
  if(!drop)return;
  drop.addEventListener('dragover',function(e){e.preventDefault();drop.style.borderColor='var(--accent-light)'});
  drop.addEventListener('dragleave',function(){drop.style.borderColor='var(--border)'});
  drop.addEventListener('drop',function(e){
    e.preventDefault();drop.style.borderColor='var(--border)';
    var fi=$('ota-file');
    if(e.dataTransfer.files.length>0&&fi){
      fi.files=e.dataTransfer.files;
      uploadFirmware();
    }
  });
}

// ── Firmware Info (from /system_status) ─────────────────────
async function loadFirmwareInfo(){
  var d=await fetchJson('/system_status');
  if(!d)return;
  setText('ota-ver',d.firmware||'--');
  setText('st-ota-pill',d.firmware||'--');
  setText('rel-current',d.firmware||'--');
  var buildLabel=d.uiBuildUtc||d.uiBuildId||d.buildEnv||'--';
  setText('ota-build',buildLabel);
  var appUsed=d.app_used?Math.round(d.app_used/1024)+'KB':'--';
  var appTotal=d.app_size?Math.round(d.app_size/1024)+'KB':'--';
  setText('ota-flash',appUsed+' / '+appTotal);
  setText('ota-sdk',(d.target||'--')+' / '+(d.idf||'--'));
}

var releaseUpdateUrl='';
async function loadOtaReleaseState(){
  var beta=await fetchJson('/update_beta');
  if(beta){
    var bt=$('rel-beta-tgl');
    if(bt)bt.checked=!!beta.beta;
    if(beta.version)setText('rel-current',beta.version);
  }
  var auto=await fetchJson('/auto_update');
  if(auto){
    var at=$('rel-auto-tgl');
    if(at)at.checked=!!auto.enabled;
  }
}
async function checkReleaseUpdate(){
  var btn=$('rel-check-btn');
  var install=$('rel-install-btn');
  if(btn)btn.disabled=true;
  if(install)install.disabled=true;
  releaseUpdateUrl='';
  setText('rel-status','检查中...');
  setText('rel-msg','正在连接 GitHub Release，请稍候。');
  try{
    var r=await fetch('/update_check');
    var txt=await r.text();
    var d={};
    if(txt){try{d=JSON.parse(txt)}catch(e){}}
    if(!r.ok)throw new Error((d&&d.error)?d.error:('HTTP '+r.status));
    setText('rel-current',d.current||'--');
    setText('rel-latest',d.latest||d.tag||'--');
    setText('rel-artifact',d.artifact||'--');
    var msg='已是最新版本';
    var cls='v-ok';
    if(d.update&&d.url){
      releaseUpdateUrl=d.url;
      msg='发现新版本';
      cls='v-warn';
      if(install)install.disabled=false;
      setText('rel-msg','发现 '+(d.tag||d.latest)+'，点击“安装更新”后设备会下载并重启。');
    }else if(!d.url){
      msg='未找到匹配固件';
      cls='v-err';
      setText('rel-msg','Release 中没有 '+(d.artifact||'当前硬件')+' 对应的固件文件。');
    }else{
      setText('rel-msg','当前版本 '+(d.current||'--')+' 已不低于 '+(d.latest||'--')+'。');
    }
    setText('rel-status',msg);
    setCls('rel-status',cls);
  }catch(e){
    setText('rel-status','检查失败');
    setCls('rel-status','v-err');
    setText('rel-msg',(e&&e.message)?e.message:'检查失败');
    showToast((e&&e.message)?e.message:'检查失败');
  }finally{
    if(btn)btn.disabled=false;
  }
}
async function installReleaseUpdate(){
  if(!releaseUpdateUrl){showToast('请先检查更新');return}
  if(!confirm('确认安装 GitHub Release OTA 固件？设备会下载固件并自动重启。'))return;
  var btn=$('rel-install-btn');
  if(btn)btn.disabled=true;
  setText('rel-status','安装中...');
  setCls('rel-status','v-warn');
  setText('rel-msg','设备正在下载并写入 OTA 固件，完成后会自动重启。');
  try{
    await postForm('/update_install',{url:releaseUpdateUrl});
    setText('rel-status','已开始安装');
    setText('rel-msg','OTA 已开始，设备将自动重启。');
    showToast('OTA 已开始，设备将重启',true);
  }catch(e){
    if(btn)btn.disabled=false;
    setText('rel-status','安装失败');
    setCls('rel-status','v-err');
  }
}
async function toggleUpdateBeta(){
  var t=$('rel-beta-tgl');
  try{
    var d=await postForm('/update_beta',{beta:t&&t.checked?'1':'0'});
    if(d&&d.version)setText('rel-current',d.version);
    releaseUpdateUrl='';
    var install=$('rel-install-btn');
    if(install)install.disabled=true;
    setText('rel-status','未检查');
  }catch(e){if(t)t.checked=!t.checked}
}
async function toggleAutoUpdate(){
  var t=$('rel-auto-tgl');
  try{await postForm('/auto_update',{enabled:t&&t.checked?'1':'0'});}
  catch(e){if(t)t.checked=!t.checked}
}

// ── WiFi ───────────────────────────────────────────────────
async function pollWifiStatus(){
  var d=await fetchJson('/wifi_status');
  if(!d)return;
  var st=$('wifi-status');
  if(st){
    if(d.connected){st.textContent=d.ssid+' ('+d.ip+')';st.className='v-ok'}
    else if(d.connecting){st.textContent=T('连接中...')||'Connecting...';st.className='v-warn'}
    else{st.textContent=T('未配置');st.className='v-warn'}
  }
  setText('st-net-pill', d.connected ? (d.ssid||'--') : (d.connecting ? '连接中' : '未配置'));
  setStatusTriplet('net',
    d.connected?('STA '+d.ssid):(d.connecting?'STA 连接中':'STA 未配置'),
    '等待热点配置读取',
    d.connected?('STA '+d.ip):'STA Offline',
    d.connected?'ok':'warn');
  // Load networks
  var net=await fetchJson('/wifi_networks');
  if(net)renderWifiSlots(net);
  // AP status
  var ap=await fetchJson('/ap_status');
  if(ap){
    setText('ap-ssid',ap.ssid||'--');
    setText('ap-clients',ap.clients||0);
    setText('ap-mode',ap.mode||((ap.channel_auto?'AP+STA':'AP')+' CH'+(ap.channel||'--')));
    var apSsid=$('ap-ssid-input');if(apSsid&&!apSsid.value)apSsid.value=ap.ssid||'';
    var apHidden=$('ap-hidden-tgl');if(apHidden)apHidden.checked=!!ap.hidden;
    setStatusTriplet('net',
      (d.connected?('STA '+d.ssid):'STA Offline')+' / AP '+(ap.ssid||'--'),
      'AP '+(ap.ssid||'--')+(ap.hidden?' hidden':''),
      (d.connected?('STA '+d.ip):'STA Offline')+' / clients '+(ap.clients||0),
      d.connected||ap.clients>0?'ok':'warn');
  }
  var hc=await fetchJson('/hotspot_config');
  if(hc){
    var hs=$('ap-ssid-input');if(hs&&!hs.value)hs.value=hc.ssid||'';
    var ht=$('ap-hidden-tgl');if(ht)ht.checked=!!hc.hidden;
    setText('st-net-nvs','AP '+(hc.ssid||'--')+(hc.hidden?' hidden':''));
  }
}

function renderWifiSlots(net){
  var container=$('wifi-slots');
  if(!container)return;
  var html='';
  if(net.networks){
    for(var i=0;i<net.networks.length;i++){
      var n=net.networks[i];
      var active=net.active===n.idx;
      html+='<div class="setting-row"><div class="setting-name"'
        +(active?' style="color:var(--ok)"':'')
        +'>'+escHtml(n.ssid)+'</div>'
        +'<div style="display:flex;gap:4px">'
        +'<button class="btn btn-sm btn-outline" onclick="connectWifi('+n.idx+')">'+(active?'✓':'')+'</button>'
        +'<button class="btn btn-sm btn-outline" onclick="editWifiSlot('+n.idx+')">'+T('编辑')+'</button>'
        +'<button class="btn btn-sm btn-outline" style="color:var(--err)" onclick="deleteWifi('+n.idx+')">✕</button>'
        +'</div></div>';
    }
  }
  container.innerHTML=html;
}

var editingSlot=-1;
function editWifiSlot(idx){
  editingSlot=idx;
  var form=$('wifi-form');
  if(form)form.style.display='block';
  if(idx>=0){
    var ssid=$('wf-ssid');
    // We don't have the password; just leave blank
    if(ssid)ssid.value='';
  }
}
function clearWifiForm(){
  var form=$('wifi-form');if(form)form.style.display='none';
  var ssid=$('wf-ssid');if(ssid)ssid.value='';
  var pass=$('wf-pass');if(pass)pass.value='';
}

async function saveWifi(){
  var ssid=$('wf-ssid');var pass=$('wf-pass');
  if(!ssid||!ssid.value)return;
  var data={ssid:ssid.value,pass:pass?pass.value:'',idx:String(editingSlot>=0?editingSlot:-1)};
  try{await postForm('/wifi_config',data);}
  catch(e){return}
  clearWifiForm();
  setText('wifi-status',T('连接中...')||'Connecting...');
  pollWifiStatus();
}

async function scanWifi(){
  var d=await fetchJson('/wifi_scan?force=1');
  if(!d||!d.networks)return;
  // Show scan results in slots area
  var container=$('wifi-slots');
  if(!container)return;
  var html='<div style="margin-bottom:6px;color:var(--tx3);font-size:11px">'+T('扫描结果')+': '+d.networks.length+'</div>';
  for(var i=0;i<d.networks.length;i++){
    var n=d.networks[i];
    html+='<div class="setting-row" style="cursor:pointer" onclick="pickScanResult(\''+escAttr(n.ssid)+'\')">'
      +'<div class="setting-name">'+escHtml(n.ssid)+'</div>'
      +'<div class="v-dim" style="font-size:11px">'+n.rssi+' dBm</div></div>';
  }
  container.innerHTML=html;
}

async function testRelayWifi(){
  var ssid=$('wf-ssid');var pass=$('wf-pass');
  if(!ssid||!ssid.value.trim()){
    showToast('请输入 SSID');
    setStatusTriplet('net','测试未发送','NVS 未变更','请输入中转 WiFi SSID','warn');
    return;
  }
  var data={ssid:ssid?ssid.value:'',pass:pass?pass.value:''};
  try{
    var r=await postForm('/relay_wifi_test',data);
    showToast((r&&r.connected)?'WiFi OK':'WiFi test ready',true);
  }catch(e){}
}

async function saveHotspot(reboot){
  if(reboot&&!confirm('确认保存网络配置并重启设备？'))return;
  var ssid=$('ap-ssid-input');var pass=$('ap-pass-input');var hidden=$('ap-hidden-tgl');
  if(!ssid||!ssid.value)return;
  var data={ssid:ssid.value,pass:pass?pass.value:'',hidden:hidden&&hidden.checked?'1':'0',save_reboot:reboot?'1':'0'};
  try{await postForm('/hotspot_config',data);showToast(reboot?T('重启设备'):T('已保存'),true)}
  catch(e){}
}

function pickScanResult(ssid){
  var wf=$('wf-ssid');if(wf)wf.value=ssid;
  var form=$('wifi-form');if(form)form.style.display='block';
  editingSlot=-1;
}

async function connectWifi(idx){
  try{await postForm('/wifi_connect',{idx:String(idx)});}
  catch(e){return}
  setText('wifi-status',T('连接中...')||'Connecting...');
  setTimeout(pollWifiStatus,300);
}
async function deleteWifi(idx){
  try{await postForm('/wifi_delete',{idx:String(idx)});}
  catch(e){return}
  pollWifiStatus();
}

// ── Gateway Status ─────────────────────────────────────────
async function pollGatewayStatus(){
  var d=await fetchJson('/gateway_status');
  if(!d)return;
  setText('gw-ap',d.ap_ip||'--');
  setText('gw-sta',d.sta_connected?d.sta_ip:'--');
  setText('gw-nat-st',d.nat?'ON':'OFF');
  setText('gw-dns-st',d.dns_bind_ok?'OK':'--');
  setText('gw-slow',d.dns_slow_500ms||0);
  setText('gw-pend',d.dns_pending||0);
  setText('gw-upstream',d.upstream_dns||'--');
  setText('gw-clients',d.ap_clients||0);
  // NAT toggle
  var natTgl=$('gw-nat-tgl');
  if(natTgl)natTgl.checked=!!d.nat;
  setText('st-dns-run',(d.dns_bind_ok?'DNS OK':'DNS --')+' / NAT '+(d.nat?'ON':'OFF')+' / block '+(d.dns_blocked||0));
  var dnsRun=$('st-dns-run');if(dnsRun)dnsRun.className='val '+(d.dns_bind_ok?'s-ok':'s-warn');
  setText('st-net-run',(d.sta_connected?('STA '+d.sta_ip):'STA Offline')+' / clients '+(d.ap_clients||0));
}

async function saveGateway(){
  var natTgl=$('gw-nat-tgl');
  var perfTgl=$('gw-perf-tgl');
  if(natTgl){
    try{await postForm('/gateway_dns',{enabled:natTgl.checked?'1':'0'});}
    catch(e){natTgl.checked=!natTgl.checked;return}
  }
  // Perf mode: reduce polling from 1s to 3s when forwarding
  if(typeof restartPoll==='function')restartPoll(perfTgl&&perfTgl.checked?3000:1000);
  pollGatewayStatus();
}

// ── DNS Config ─────────────────────────────────────────────
async function loadGatewayDns(){
  var d=await fetchJson('/dns_rules');
  if(!d)return;
  var bl=$('dns-blacklist');if(bl)bl.value=d.blacklist||'';
  var wl=$('dns-whitelist');if(wl)wl.value=d.whitelist||'';
  setText('dns-bl-cnt',(d.black_count||0)+' '+T('域名'));
  setText('dns-wl-cnt',(d.white_count||0)+' '+T('域名'));
  // Upstream DNS mode
  updateDnsUpstreamCards(d.upstream_mode||0);
  var profile=detectDnsProfile(d.blacklist,d.whitelist);
  updateDnsProfileCards(profile);
  var counts=(d.black_count||0)+' 黑 / '+(d.white_count||0)+' 白';
  setStatusTriplet('dns',profile||'自定义规则',counts,'等待 DNS 运行确认','ok');
  if(!normLines(d.blacklist)||normLines(d.blacklist)===normLines(OLD_DNS_BLACKLIST)){
    applyDnsProfile('conservative');
    updateDnsProfileCards('conservative');
    setStatusTriplet('dns','保守模式','已自动填入 12 条规则','请点击保存规则写入 NVS','warn');
  }
  // Custom IP
  var customRow=$('dns-custom-row');
  var customIp=$('dns-custom-ip');
  if(d.upstream_mode===3&&customRow){customRow.style.display='block';if(customIp)customIp.value=d.upstream_custom||''}
  else if(customRow){customRow.style.display='none'}
}

function updateDnsUpstreamCards(mode){
  var cards=$('dns-upstream');
  if(!cards)return;
  var items=cards.querySelectorAll('.sel-card');
  var modes=[0,1,2,3];
  for(var i=0;i<items.length;i++){
    items[i].classList.toggle('active',modes[i]===mode);
  }
}

async function setDnsUpstream(val){
  var mode=0;
  if(val==='auto')mode=0;
  else if(val==='223.5.5.5')mode=1;
  else if(val==='119.29.29.29')mode=2;
  else mode=3;
  updateDnsUpstreamCards(mode);
  var customRow=$('dns-custom-row');
  if(mode===3&&customRow)customRow.style.display='block';
  else if(customRow)customRow.style.display='none';
}

async function setDnsProfile(profile){
  applyDnsProfile(profile);
  updateDnsProfileCards(profile);
  try{await saveGatewayDns();showToast(T('已保存')||'Saved',true)}catch(e){}
}

function updateDnsProfileCards(profile){
  var cards=$('dns-profile');
  if(!cards)return;
  var items=cards.querySelectorAll('.sel-card');
  items[0].classList.toggle('active',profile==='conservative');
  items[1].classList.toggle('active',profile==='aggressive');
}

function detectDnsProfile(blacklist,whitelist){
  var b=normLines(blacklist),w=normLines(whitelist);
  if(b===normLines(DNS_PROFILES.conservative.blacklist)&&w===normLines(DNS_PROFILES.conservative.whitelist))return 'conservative';
  if(b===normLines(DNS_PROFILES.aggressive.blacklist)&&w===normLines(DNS_PROFILES.aggressive.whitelist))return 'aggressive';
  return '';
}

function applyDnsProfile(profile){
  var p=DNS_PROFILES[profile];
  if(!p)return;
  var bl=$('dns-blacklist');
  var wl=$('dns-whitelist');
  if(bl)bl.value=p.blacklist;
  if(wl)wl.value=p.whitelist;
  setText('dns-bl-cnt',countLines(p.blacklist)+' '+T('域名'));
  setText('dns-wl-cnt',countLines(p.whitelist)+' '+T('域名'));
  setStatusTriplet('dns',profile,countLines(p.blacklist)+' 黑 / '+countLines(p.whitelist)+' 白','规则已应用，等待保存','warn');
}

async function saveGatewayDns(){
  var data={enabled:'1'};
  var bl=$('dns-blacklist');if(bl)data.blacklist=bl.value;
  var wl=$('dns-whitelist');if(wl)data.whitelist=wl.value;
  var cards=$('dns-upstream');
  if(cards){
    var items=cards.querySelectorAll('.sel-card');
    for(var i=0;i<items.length;i++){
      if(items[i].classList.contains('active')){
        data.upstream_mode=String(i);
        break;
      }
    }
  }
  var customIp=$('dns-custom-ip');
  if(customIp)data.upstream_custom=customIp.value;
  try{await postForm('/dns_rules',data);}catch(e){return}
  setStatusTriplet('dns','规则已保存','已写入 NVS','等待 DNS 运行确认','ok');
  loadGatewayDns();
}

async function testGatewayDns(){
  var inp=$('dns-test-input');if(!inp||!inp.value)return;
  var d=await fetchJson('/gateway_dns_test?domain='+encodeURIComponent(inp.value));
  if(d){
    var result=d.action||d.decision||'--';
    alert('DNS Test: '+inp.value+' -> '+result);
  }
}

async function loadGatewayBlocked(){
  var d=await fetchJson('/gateway_blocked');
  var count=Array.isArray(d)?d.length:0;
  setText('dns-blocked-cnt',count);
}

async function clearGatewayBlocked(){
  try{await postForm('/gateway_blocked_clear',{});}catch(e){return}
  setText('dns-blocked-cnt','0');
}

// ── CAN Tools ──────────────────────────────────────────────
function switchCanTab(tab){
  canTab=tab;
  var tabs=document.querySelectorAll('.sub-tab');
  var tabIds=['can-sniffer','can-recorder','can-controller','can-debug'];
  var tabNames=['sniffer','recorder','controller','debug'];
  for(var i=0;i<tabs.length;i++)tabs[i].classList.toggle('active',tabNames[i]===tab);
  for(var i=0;i<tabIds.length;i++){
    var el=$(tabIds[i]);
    if(el)el.style.display=tabNames[i]===tab?'block':'none';
  }
  if(tab==='sniffer')pollSniffer();
  if(tab==='controller')pollCanController();
  if(tab==='debug')pollLastWrite();
}

async function pollCanTab(){switchCanTab(canTab)}

async function pollSniffer(){
  var d=await fetchJson('/frames');
  if(!d||!d.frames)return;
  sniffFrames=d.frames;
  renderSniffer();
}

function renderSniffer(){
  var rows=$('sniff-rows');if(!rows)return;
  var filter=$('sniff-filter');
  var f=filter?filter.value.toLowerCase():'';
  var count=0;
  var html='';
  for(var i=sniffFrames.length-1;i>=0;i--){
    var fr=sniffFrames[i];
    var idStr=toHex(fr.id);
    var name=fr.name||'';
    if(f&&idStr.toLowerCase().indexOf(f)<0&&name.toLowerCase().indexOf(f)<0)continue;
    count++;
    var dataStr='';
    if(fr.data){for(var j=0;j<fr.data.length;j++){if(j)dataStr+=' ';dataStr+=('0'+fr.data[j].toString(16).toUpperCase()).slice(-2)}}
    var rowCls=(name.toLowerCase().indexOf('err')>=0||idStr.toLowerCase().indexOf('txerr')>=0)?'warn':'';
    var dlc=(fr.dlc!==undefined)?fr.dlc:(fr.data?fr.data.length:'--');
    html+='<div class="diag-priority-row '+rowCls+'"><span class="hex">'+idStr+(name?' <span style="color:var(--tx3);font-size:10px">'+escHtml(name)+'</span>':'')
      +'</span><span>RX</span><span>'+dlc+'</span><span class="hex">'+dataStr+'</span><span>--</span></div>';
  }
  rows.innerHTML=html;
  setText('sniff-count',count+' 实时帧');
  setText('diag-id-count',count||'--');
}

function toggleSniffPause(){
  sniffPaused=!sniffPaused;
  var btn=$('sniff-pause');
  if(btn)btn.textContent=sniffPaused?'▶':'⏸';
}

// Recorder
async function startRec(){
  try{await postForm('/rec_start',{});}catch(e){return}
  recActive=true;
  $('rec-start').disabled=true;$('rec-stop').disabled=false;$('rec-dl').disabled=true;
  setText('rec-status',T('录制中')||'Recording...');
}
async function stopRec(){
  setText('rec-status',T('保存中...')||'Saving...');
  try{await postForm('/rec_stop',{});}catch(e){setText('rec-status',T('保存失败')||'Save failed');return}
  recActive=false;
  $('rec-start').disabled=false;$('rec-stop').disabled=true;$('rec-dl').disabled=false;
  setText('rec-status',T('已保存')||'Saved');
}
function downloadRec(){window.location.href='/rec_download'}

// Controller
async function pollCanController(){
  // Use /status for controller info
  var d=await fetchJson('/status');
  if(!d)return;
  setText('ctrl-eflg',toHex(d.eflg||0));
  setText('ctrl-mode',d.can?'Normal':'Offline');
  // Mux stats
  var mux=$('ctrl-mux');
  if(mux&&d.mux){
    var html='';
    for(var i=0;i<d.mux.length;i++){
      html+='<div class="diag-item"><span class="lbl">Mux '+i+'</span>'
        +'<span class="v-dim">RX:'+d.mux[i].rx+' TX:'+d.mux[i].tx+' Err:'+d.mux[i].err+'</span></div>';
    }
    mux.innerHTML=html;
  }
}
function updateCanController(d){
  setText('ctrl-eflg',toHex(d.eflg||0));
  setText('ctrl-rxerr',d.txerr||0);
  setText('ctrl-txerr',d.txerr||0);
  setText('ctrl-mode',d.can?'Normal':'Offline');
}

// Debug
async function toggleCanDebug(){
  var tgl=$('debug-tgl');
  if(tgl){
    try{await postForm('/logging',{eprn:tgl.checked?'1':'0'});}
    catch(e){tgl.checked=!tgl.checked}
  }
}

async function pollLastWrite(){
  var d=await fetchJson('/status');
  if(!d||!d.probe)return;
  var p=d.probe;
  setText('lw-injected',p.active?'Yes':'No');
  setText('lw-bus',p.id?toHex(p.id):'--');
  var stateNames=['Idle','Pending','Match','Different','Failed'];
  setText('lw-match',stateNames[p.state]||'--');
  setText('lw-age',p.txa?p.txa+'ms':'--');
  var lwMatchText=($('lw-match')&&$('lw-match').textContent)?$('lw-match').textContent:'--';
  setText('diag-last-write',lwMatchText);
}

// ── CAN Pins ───────────────────────────────────────────────
async function loadCanPins(){
  var d=await fetchJson('/can_pins');
  if(!d)return;
  // CAN2 MCP2515 pins from API response, fallback to compile-time defaults
  setText('can-cs',d.cs!=null?'GPIO '+d.cs:'GPIO 10');
  setText('can-sck',d.sck!=null?'GPIO '+d.sck:'GPIO 12');
  setText('can-miso',d.miso!=null?'GPIO '+d.miso:'GPIO 13');
  setText('can-mosi',d.mosi!=null?'GPIO '+d.mosi:'GPIO 11');
  setText('can-rst',d.rst!=null?'GPIO '+d.rst:'GPIO 9');
}

// ── Gear assist placeholder telemetry ──────────────────────
async function pollGearAssist(){
  var d=await fetchJson('/gear_assist_status');
  if(!d)return;
  setText('shift-speed',(d.speed_kph!==undefined?d.speed_kph:'--')+' km/h');
  setText('shift-gear',d.gear||'--');
  setText('shift-brake',d.brake?(T('踩下')||'ON'):(d.brake_seen?(T('未踩')||'OFF'):'--'));
  setText('shift-fsd',d.can_online?'CAN Online':'CAN Offline');
  setCls('shift-fsd','v-dim '+(d.can_online?'v-ok':'v-err'));
}

// ── Legacy Real Wiring Truth Cards ────────────────────────
var legacyFsdConfigLoaded=false;
function renderLegacyFsdPolicyCards(policy){
  var cards=document.getElementById('legacy-fsd-policy-cards');
  if(!cards)return;
  var normalized=policy||'stable';
  if(policy==='legacy_stable')normalized='stable';
  if(policy==='legacy_experimental')normalized='experimental';
  var items=cards.querySelectorAll('.sel-card');
  for(var i=0;i<items.length;i++){
    var item=items[i];
    item.classList.toggle('active',item.getAttribute('data-policy')===normalized);
  }
}
async function loadLegacyFsdConfig(){
  var cfg=await fetchJson('/legacy_fsd_config');
  if(!cfg){legacyFsdConfigLoaded=false;return false;}
  var mux1=document.getElementById('legacy-fsd-mux1-tgl');
  var profile=document.getElementById('legacy-fsd-profile');
  var vision=document.getElementById('legacy-fsd-vision');
  if(mux1)mux1.checked=!!cfg.mux1;
  if(profile)profile.checked=!!cfg.profile;
  if(vision)vision.checked=!!cfg.vision;
  if(cfg.policy)renderLegacyFsdPolicyCards(cfg.policy);
  // Task 6 UI 补全：从 /config fsdRuntime 读 Legacy offset + overrideSpeedLimit
  var conf=await fetchJson('/config');
  if(conf&&conf.fsdRuntime){
    var offset=document.getElementById('legacy-offset-inp');
    var override=document.getElementById('legacy-override-tgl');
    if(offset&&conf.fsdRuntime.legacyOffset!==undefined)offset.value=conf.fsdRuntime.legacyOffset;
    if(override)override.checked=!!conf.fsdRuntime.overrideSpeedLimit;
    setVal('legacy-offset-mode',legacySmartModeValue(conf.fsdRuntime.legacyOffsetMode));
    setVal('legacy-offset-manual',conf.fsdRuntime.legacyOffset!==undefined?conf.fsdRuntime.legacyOffset:0);
    var smooth=$('legacy-smooth-down');if(smooth)smooth.checked=!!conf.fsdRuntime.legacySmoothDown;
    setVal('legacy-smooth-rate',conf.fsdRuntime.legacySmoothRateKphS!==undefined?conf.fsdRuntime.legacySmoothRateKphS:5);
    setVal('legacy-pct-low',conf.fsdRuntime.legacyCustomPctLow!==undefined?conf.fsdRuntime.legacyCustomPctLow:50);
    setVal('legacy-pct-mid',conf.fsdRuntime.legacyCustomPctMid!==undefined?conf.fsdRuntime.legacyCustomPctMid:30);
    setVal('legacy-pct-high',conf.fsdRuntime.legacyCustomPctHigh!==undefined?conf.fsdRuntime.legacyCustomPctHigh:20);
    setVal('legacy-pct-vhigh',conf.fsdRuntime.legacyCustomPctVeryHigh!==undefined?conf.fsdRuntime.legacyCustomPctVeryHigh:10);
  }
  legacyFsdConfigLoaded=true;
  return true;
}
async function setLegacyFsdPolicy(policy){
  renderLegacyFsdPolicyCards(policy);
  await postForm('/legacy_fsd_config',{policy:policy});
  showToast(policy==='stable'?'Legacy Stable':(policy==='legacy_tesla_parity'?'Tesla Controller 对齐模式':'Experimental'));
  poll();
}
async function saveLegacyFsdExperimental(){
  if(!legacyFsdConfigLoaded){
    await loadLegacyFsdConfig();
    if(!legacyFsdConfigLoaded){showToast('配置未同步，请稍后再试');return;}
  }
  var mux1=document.getElementById('legacy-fsd-mux1-tgl');
  var profile=document.getElementById('legacy-fsd-profile');
  await postForm('/legacy_fsd_config',{
    policy:'experimental',
    mux1:mux1&&mux1.checked?'1':'0',
    profile:profile&&profile.checked?'1':'0'
  });
  showToast('实验模式配置已保存');
  poll();
}
// Task 6 UI 补全：POST JSON 到 /config（fsdRuntime 块）
async function postConfigJson(data){
  var r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
  if(!r.ok)throw new Error('HTTP '+r.status);
  return r;
}
function syncLegacyOffsetInputs(sourceId){
  var src=$(sourceId), legacy=$('legacy-offset-inp'), manual=$('legacy-offset-manual');
  if(!src)return;
  var v=parseInt(src.value,10)||0;
  if(v<0)v=0;if(v>33)v=33;src.value=String(v);
  if(legacy&&legacy!==src)legacy.value=String(v);
  if(manual&&manual!==src)manual.value=String(v);
}
async function saveLegacySmartSpeed(){
  syncLegacyOffsetInputs('legacy-offset-manual');
  var modeVal=val('legacy-offset-mode');
  var mode=(modeVal==='off')?0:((modeVal==='manual')?1:((modeVal==='auto')?2:3));
  var smooth=$('legacy-smooth-down');
  var data={fsdRuntime:{
    legacyOffsetMode:mode,
    legacyOffset:clampNum('legacy-offset-inp',0,0,33),
    legacySmoothDown:!!(smooth&&smooth.checked),
    legacySmoothRateKphS:clampNum('legacy-smooth-rate',5,1,20),
    legacyCustomPctLow:clampNum('legacy-pct-low',50,0,63),
    legacyCustomPctMid:clampNum('legacy-pct-mid',30,0,63),
    legacyCustomPctHigh:clampNum('legacy-pct-high',20,0,63),
    legacyCustomPctVeryHigh:clampNum('legacy-pct-vhigh',10,0,63)
  }};
  try{
    await postConfigJson(data);
    showToast('Legacy 智能速度偏移已保存',true);
    poll();
  }catch(e){showToast('保存失败');}
}
// 功能1：Legacy 速度偏移（0x2F8 UI_userSpeedOffset，0-33 km/h）
async function saveLegacyOffset(){
  syncLegacyOffsetInputs('legacy-offset-inp');
  var el=document.getElementById('legacy-offset-inp');
  if(!el)return;
  var v=parseInt(el.value)||0;
  if(v<0)v=0;if(v>33)v=33;el.value=v;
  try{
    await postConfigJson({fsdRuntime:{legacyOffset:v}});
    showToast('Legacy 速度偏移已保存: '+v+' km/h',true);
    poll();
  }catch(e){showToast('保存失败');}
}
// 功能5a：重写限速（0x438 visionSpeedSlider=100）
async function saveLegacyOverride(){
  var el=document.getElementById('legacy-override-tgl');
  if(!el)return;
  try{
    await postConfigJson({fsdRuntime:{overrideSpeedLimit:el.checked}});
    showToast('重写限速已'+(el.checked?'开启':'关闭'),true);
    poll();
  }catch(e){showToast('保存失败');}
}
// 功能2：视觉限速 clear（独立于 policy，只 POST vision，Task 1 后端独立门控）
async function saveLegacyVision(){
  if(!legacyFsdConfigLoaded){
    await loadLegacyFsdConfig();
    if(!legacyFsdConfigLoaded){showToast('配置未同步，请稍后再试');return;}
  }
  var vision=document.getElementById('legacy-fsd-vision');
  try{
    await postForm('/legacy_fsd_config',{vision:vision&&vision.checked?'1':'0'});
    showToast('视觉限速清除已'+(vision&&vision.checked?'开启':'关闭'),true);
    poll();
  }catch(e){showToast('保存失败');}
}
function renderLegacyTruth(d){
  const set=(id,v)=>{const e=document.getElementById(id); if(e)e.innerText=v;};
  set('legacy-selected-hw',hwLabel(d.hw));
  set('legacy-effective-handler',d.hwName||hwLabel(d.effectiveHw));
  set('legacy-detected-hw',hwLabel(d.hwDetected||0));
  set('legacy-ap-gate',d.apGateOpen?'开放':'关闭');
  set('legacy-ota-guard',d.vehicleOta?'已锁定':'正常');
  const fsd=d.fsdDiag||{};
  const m0=fsd.mux0||{};
  const m1=fsd.mux1||{};
  set('legacy-fsd-strategy',fsd.policy==='legacy_tesla_parity'?'Tesla Controller 对齐模式':((fsd.policy==='legacy_experimental')?'实验模式':'稳定'));
  renderLegacyFsdPolicyCards(fsd.policy);
  set('legacy-fsd-mux0',`RX ${m0.rx||0} / TX ${m0.tx||0} / ${m0.lastSkip||'无'}`);
  set('legacy-fsd-mux1',`RX ${m1.rx||0} / TX ${m1.tx||0} / ${m1.lastSkip||'无'}`);
  set('legacy-fsd-trigger',fsd.triggerSource||'未知');
  const tw=d.twai||{};
  const gate=fsd.gate||{};
  const muxLine=(m)=>`RX ${m.rx||0} / TX ${m.tx||0} / ERR ${m.err||0} / ${m.lastSkip||'无'}`;
  set('legacy-health-mux0',muxLine(m0));
  set('legacy-health-mux1',muxLine(m1));
  set('legacy-health-twai',tw.state||'unknown');
  set('legacy-health-tec-rec',`${tw.tec||0}/${tw.rec||0}`);
  set('legacy-health-tx-failed',tw.txFailed||0);
  set('legacy-health-rx-missed',tw.rxMissed||0);
  set('legacy-health-state',fsd.health||'unknown');
  set('legacy-health-blocked',gate.lastBlockedBy||fsd.lastBlockedBy||fsd.gateReason||'none');
  const svc=d.serviceDiag||{};
  const svctx=svc.tx||{};
  set('service-burst-state',`${svc.lastCommand==='enter'?'进入':svc.lastCommand==='exit'?'退出':'无'} / 剩余 ${svc.remaining||0}`);
  set('service-last-data',svctx.lastData||'--');
  set('service-tx-evidence',`成功 ${svctx.txOk||0} / 失败 ${svctx.txErr||0}`);
  const wd=d.wheelDndDiag||{};
  set('wheel-native-3c2',wd.native3c2Seen?`已接收 ${wd.native3c2AgeMs}ms 前`:'等待原生帧');
  set('wheel-base-frame',wd.baseFrameMode==='native'?'原生帧':wd.baseFrameMode==='synthetic_fallback'?'合成帧':wd.baseFrameMode||'等待中');
  set('wheel-sequence-state',wd.sequenceState==='sent'?'已发送':wd.sequenceState==='failed'?'失败':wd.sequenceState==='running'?'运行中':wd.sequenceState||'空闲');
}

// ── Temp from system_status ────────────────────────────────
async function loadTemp(){
  var d=await fetchJson('/system_status');
  if(d&&d.temp_c!==undefined&&d.temp_c!==null){
    var t=parseFloat(d.temp_c);
    var el=document.getElementById('s-temp');
    if(el){
      el.textContent=t+'°C';
      // Color coding: >60°C red, >45°C orange, else green
      el.className='stat-val '+(t>60?'v-err':(t>45?'v-info':'v-acc'));
    }
  }
  // Firmware version display
  if(d&&(d.firmware||d.version)){
    setText('s-ver',d.firmware||d.version);
  }
}

// ── AP Config ──────────────────────────────────────────────
async function saveApConfig(){
  var ssid=$('ap-ssid-input');
  var pass=$('ap-pass-input');
  var data={};
  if(ssid)data.ssid=ssid.value;
  if(pass)data.pass=pass.value;
  try{await postForm('/ap_config',data);}catch(e){}
}

// ── Init ───────────────────────────────────────────────────
// ── Mobile Tab Bar ──────────────────────────────────────────
function toggleMobMore(id){
  var p=$(id||'mob-more');
  if(p)p.classList.toggle('open');
}
function closeStandaloneMobMore(){
  var p=$('mob-more-single');
  if(p)p.classList.remove('open');
}
function updateMobTabs(pageId){
  var map={driving:'pg-overview',hardware:'pg-hardware',speed:'pg-speed',network:'pg-network',drivingstyle:'pg-drive',defense:'pg-defense',ota:'pg-ota',can:'pg-can',more:'pg-overview'};
  var tabs=document.querySelectorAll('.mob-tab[data-mobile-page]');
  for(var i=0;i<tabs.length;i++){
    var mob=tabs[i].getAttribute('data-mobile-page');
    tabs[i].classList.toggle('active',mob===pageId||map[mob]===pageId);
  }
}
function showMobilePage(name){
  var map={driving:'pg-overview',hardware:'pg-hardware',speed:'pg-speed',network:'pg-network',drivingstyle:'pg-drive',defense:'pg-defense',ota:'pg-ota',can:'pg-can'};
  closeStandaloneMobMore();
  showPage(map[name]||'pg-overview');
  updateMobTabs(name);
  if(name==='speed')loadSpeedStrategy();
  if(name==='network')pollWifiStatus();
}
function showStandaloneMorePage(name){
  closeStandaloneMobMore();
  showMobilePage(name);
}

document.addEventListener('DOMContentLoaded',function(){
  // Desktop sidebar nav
  var navs=document.querySelectorAll('.nav-item');
  for(var i=0;i<navs.length;i++){
    navs[i].addEventListener('click',function(){
      showPage(this.getAttribute('data-page'));
    });
  }

  // Standalone mobile tab bar is wired inline via showMobilePage().
  // Dual-CAN mobile tab bar + "more" menu navigate to desktop pages.
  var mobDual=document.querySelectorAll('#mob-tabs-dual .mob-tab[data-page], #mob-more .mob-more-item[data-page]');
  for(var i=0;i<mobDual.length;i++){
    mobDual[i].addEventListener('click',function(){showPage(this.getAttribute('data-page'));});
  }

  // Initial page
  showMobilePage('driving');

  // OTA drag-drop
  setupOtaDrop();

  // Load initial data
  loadFirmwareInfo();
  loadCanPins();
  loadLegacyFsdConfig();
  loadPlugins();

  // Start polling
  poll();
  pollTick=function(){
    poll();loadTemp();
    var activePage=document.querySelector('.page.active');
    if(activePage){
      var pid=activePage.id;
      if(pid==='pg-can'){if(canTab==='sniffer'&&!sniffPaused)pollSniffer();else if(canTab==='debug')pollLastWrite()}
      if(pid==='pg-bus2'&&!isSingleCan())pollCAN2();
      if(pid==='pg-shift'&&!isSingleCan())pollGearAssist();
    }
  };
  pollTimer=setInterval(pollTick,pollMs);

  // Visibility handling
  document.addEventListener('visibilitychange',function(){
    if(document.hidden){
      if(pollTimer){clearInterval(pollTimer);pollTimer=null}
    }else{
      if(!pollTimer){poll();pollTimer=setInterval(pollTick,pollMs)}
    }
  });
});

function restartPoll(ms){
  pollMs=ms;
  if(pollTimer&&pollTick){
    clearInterval(pollTimer);
    pollTimer=setInterval(pollTick,pollMs);
  }
}
</script>
<!-- Mobile Standalone Pages (single-CAN product) -->
<div data-dual-hide="1">
<!-- Mobile Bottom Tab Bar (standalone) -->
<div class="mob-tabs" id="mob-tabs">
  <button class="mob-tab active" data-mobile-page="driving" onclick="showMobilePage('driving')"><div class="mob-icon">▣</div><div>驾驶</div></button>
  <button class="mob-tab" data-mobile-page="hardware" onclick="showMobilePage('hardware')"><div class="mob-icon">◇</div><div>硬件</div></button>
  <button class="mob-tab" data-mobile-page="speed" onclick="showMobilePage('speed')"><div class="mob-icon">↗</div><div>速度</div></button>
  <button class="mob-tab" data-mobile-page="network" onclick="showMobilePage('network')"><div class="mob-icon">◎</div><div>网络</div></button>
  <button class="mob-tab" data-mobile-page="defense" onclick="showMobilePage('defense')"><div class="mob-icon">◈</div><div>防护</div></button>
  <button class="mob-tab" data-mobile-page="more" onclick="toggleMobMore('mob-more-single')"><div class="mob-icon">···</div><div>更多</div></button>
</div>
<div class="mob-more-panel" id="mob-more-single" data-dual-hide="1">
  <div class="mob-more-close" onclick="toggleMobMore('mob-more-single')">✕</div>
  <div class="mob-more-item" onclick="showStandaloneMorePage('drivingstyle')">◉ 驾驶风格</div>
  <div class="mob-more-item" onclick="showStandaloneMorePage('defense')">◈ FSD 防护</div>
  <div class="mob-more-item" onclick="showStandaloneMorePage('ota')">⇧ OTA 升级</div>
  <div class="mob-more-item" onclick="showStandaloneMorePage('can')">⌘ CAN 诊断</div>
</div>
</div>
<!-- Mobile Dual-CAN Nav (hidden in standalone) -->
<div class="mob-tabs" id="mob-tabs-dual" data-single-hide="1">
  <div class="mob-tab active" data-page="pg-overview"><div class="mob-icon">▣</div><div>状态</div></div>
  <div class="mob-tab" data-page="pg-drive"><div class="mob-icon">◉</div><div>模式</div></div>
  <div class="mob-tab" data-page="pg-speed"><div class="mob-icon">↗</div><div>速度</div></div>
  <div class="mob-tab" data-page="pg-network"><div class="mob-icon">◎</div><div>网络</div></div>
  <div class="mob-tab" onclick="toggleMobMore()"><div class="mob-icon">···</div><div>更多</div></div>
</div>
<div class="mob-more-panel" id="mob-more" data-single-hide="1">
  <div class="mob-more-close" onclick="toggleMobMore()">✕</div>
  <div class="mob-more-item" data-page="pg-overview" data-mob="1">▣ 状态</div>
  <div class="mob-more-item" data-page="pg-hardware" data-mob="1">◇ 硬件模式</div>
  <div class="mob-more-item" data-page="pg-drive" data-mob="1">◉ 驾驶风格</div>
  <div class="mob-more-item" data-page="pg-speed" data-mob="1">↗ 速度策略</div>
  <div class="mob-more-item" data-page="pg-ota" data-mob="1">⇧ OTA 升级</div>
  <div class="mob-more-item" data-page="pg-bus2" data-cap="can2" data-single-hide="1">✦ CAN2 控制</div>
  <div class="mob-more-item" data-page="pg-strobe" data-cap="lighting" data-single-hide="1">⚡ 灯光特技</div>
  <div class="mob-more-item" data-page="pg-network" data-mob="1">◎ 网络设置</div>
  <div class="mob-more-item" data-page="pg-defense" data-mob="1">◈ FSD 防护</div>
  <div class="mob-more-item" data-page="pg-can" data-mob="1">⌘ CAN 诊断</div>
  <div class="mob-more-item" data-page="pg-shift" data-cap="shift" data-single-hide="1">⚙ 自动换挡</div>
</div>
</body>
</html>)HTML";

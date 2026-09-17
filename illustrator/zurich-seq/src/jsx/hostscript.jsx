#target "illustrator"

/*
Zurich Seq — CEP 面板版
純運算/繪製邏輯,由 CEP 面板 (index.html/main.js) 呼叫。
*/

// ExtendScript's engine has no native JSON object — polyfill it.
if (typeof JSON === "undefined") {
  #include "json2.js"
}

var GG_PREVIEW_LAYER = "_Seq Preview";

// ---------- Helpers ----------
function gg_clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function gg_nowStamp() {
  function p(n) { return (n < 10 ? "0" : "") + n; }
  var d = new Date();
  return d.getFullYear() + "" + p(d.getMonth() + 1) + p(d.getDate()) + "" + p(d.getHours()) + p(d.getMinutes()) + p(d.getSeconds());
}
function gg_getAB(doc) {
  var i = doc.artboards.getActiveArtboardIndex();
  var r = doc.artboards[i].artboardRect;
  return { L: r[0], T: r[1], R: r[2], B: r[3], W: r[2] - r[0], H: r[1] - r[3] };
}
function gg_getSel(doc) {
  try {
    if (!doc.selection || doc.selection.length === 0) return null;
    // Union bounds across the WHOLE selection, not just doc.selection[0] —
    // a multi-object selection previously measured only its first item
    // (often much smaller than what's visibly selected), which made the
    // default padding look "too large" even for a large selection.
    var minL = Infinity, maxR = -Infinity, minB = Infinity, maxT = -Infinity;
    var found = false;
    for (var i = 0; i < doc.selection.length; i++) {
      var it = doc.selection[i];
      if (!it.hasOwnProperty("geometricBounds")) continue;
      var g = it.geometricBounds; // [left, top, right, bottom] (matches artboardRect's order — confirmed empirically: a 166x166pt selection returned [249,490,415,324], i.e. left=249, top=490, right=415, bottom=324)
      found = true;
      if (g[0] < minL) minL = g[0];
      if (g[2] > maxR) maxR = g[2];
      if (g[3] < minB) minB = g[3];
      if (g[1] > maxT) maxT = g[1];
    }
    if (!found) return null;
    return { L: minL, T: maxT, R: maxR, B: minB, W: maxR - minL, H: maxT - minB };
  } catch (e) { return null; }
}
function gg_withPadSides(b, t, r, bt, l) {
  t = Math.max(0, t || 0); r = Math.max(0, r || 0); bt = Math.max(0, bt || 0); l = Math.max(0, l || 0);
  return { L: b.L + l, R: b.R - r, T: b.T - t, B: b.B + bt, W: b.R - b.L - (l + r), H: b.T - b.B - (t + bt) };
}
function gg_ensureLayer(doc, name) {
  for (var i = 0; i < doc.layers.length; i++) { if (doc.layers[i].name === name) return doc.layers[i]; }
  var L = doc.layers.add(); L.name = name; return L;
}
function gg_ensureSubLayer(parent, name) {
  for (var i = 0; i < parent.layers.length; i++) { if (parent.layers[i].name === name) return parent.layers[i]; }
  var L = parent.layers.add(); L.name = name; return L;
}
function gg_clearLayer(L) { try { var a = L.pageItems; for (var i = a.length - 1; i >= 0; i--) { a[i].remove(); } } catch (e) {} }
function gg_removeLayer(doc, name) {
  // Illustrator refuses to remove a hidden or locked layer (silently, if the
  // caller swallows the exception) — unlock/unhide first so a user who toggled
  // the applied grid's visibility doesn't leave it stuck behind forever.
  for (var i = doc.layers.length - 1; i >= 0; i--) {
    var L = doc.layers[i];
    if (L.name === name) {
      try { L.locked = false; } catch (e) {}
      try { L.visible = true; } catch (e) {}
      try { L.remove(); } catch (e) {}
    }
  }
}
function gg_gray(k) { var c = new GrayColor(); c.gray = gg_clamp(k, 0, 100); return c; }
function gg_snap(v, step) { if (!step || step <= 0) return v; return Math.round(v / step) * step; }
function gg_line(L, x1, y1, x2, y2, w) {
  var p = L.pathItems.add(); p.setEntirePath([[x1, y1], [x2, y2]]);
  p.stroked = true; p.filled = false; p.strokeWidth = w || 0.5; p.strokeColor = gg_gray(70);
  return p;
}
function gg_drawPadFrameLines(L, baseB, padB) {
  gg_line(L, padB.L, baseB.T, padB.L, baseB.B, 0.75);
  gg_line(L, padB.R, baseB.T, padB.R, baseB.B, 0.75);
  gg_line(L, baseB.L, padB.T, baseB.R, padB.T, 0.75);
  gg_line(L, baseB.L, padB.B, baseB.R, padB.B, 0.75);
}

// ---------- Builders ----------
function gg_buildSwiss(targets, baseB, padB, cols, colGutter, rowsOn, rowsCount, rowGutter, baselineH, baselineV, snapOn, snapStep, drawFrame) {
  var Left = padB.L, Right = padB.R, Top = padB.T, Bot = padB.B;
  var W = Right - Left, H = Top - Bot; if (W <= 0 || H <= 0) throw Error("內邊距後區域無效。");
  cols = Math.max(1, Math.floor(cols)); colGutter = Math.max(0, colGutter);
  var cw = (W - colGutter * (cols - 1)) / cols; if (cw <= 0) throw Error("欄寬度變為負值。請減少間距/欄數或內邊距。");
  for (var i = 1; i < cols; i++) {
    var lx = Left + i * cw + (i - 1) * colGutter; if (snapOn) lx = gg_snap(lx, snapStep);
    var rx = lx + colGutter;
    if (colGutter > 0) { gg_line(targets.cols, lx, Top, lx, Bot, 0.5); gg_line(targets.cols, rx, Top, rx, Bot, 0.5); }
    else { gg_line(targets.cols, lx, Top, lx, Bot, 0.5); }
  }
  if (rowsOn && rowsCount > 0) {
    rowsCount = Math.max(1, Math.floor(rowsCount)); rowGutter = Math.max(0, rowGutter);
    var rh = (H - rowGutter * (rowsCount - 1)) / rowsCount; if (rh <= 0) throw Error("列高度變為負值。請減少間距/列數或內邊距。");
    for (var r = 1; r < rowsCount; r++) {
      var ty = Top - (r * rh + (r - 1) * rowGutter); if (snapOn) ty = gg_snap(ty, snapStep);
      var by = ty - rowGutter;
      if (rowGutter > 0) { gg_line(targets.rows, Left, ty, Right, ty, 0.5); gg_line(targets.rows, Left, by, Right, by, 0.5); }
      else { gg_line(targets.rows, Left, ty, Right, ty, 0.5); }
    }
  }
  if (baselineH && baselineH > 0) { for (var yb = Top; yb >= Bot; yb -= baselineH) { var yv = snapOn ? gg_snap(yb, snapStep) : yb; gg_line(targets.baseH, Left, yv, Right, yv, 0.25); } }
  if (baselineV && baselineV > 0) { for (var xb = Left; xb <= Right; xb += baselineV) { var xv = snapOn ? gg_snap(xb, snapStep) : xb; gg_line(targets.baseV, xv, Top, xv, Bot, 0.25); } }
  if (drawFrame) { gg_drawPadFrameLines(targets.frame, baseB, padB); }
}

function gg_buildEqual(targets, baseB, padB, divs, gutter, diagsOn, drawFrame) {
  divs = Math.max(2, Math.floor(divs)); gutter = Math.max(0, gutter);
  var cellW = (padB.W - gutter * (divs - 1)) / divs; if (cellW <= 0) throw Error("分割寬度變為負值。請調整間距/內邊距。");
  for (var i = 1; i < divs; i++) {
    var lx = padB.L + i * cellW + (i - 1) * gutter; lx = gg_snap(lx, 0.5);
    var rx = lx + gutter;
    if (gutter > 0) { gg_line(targets.vert, lx, padB.T, lx, padB.B, 0.5); gg_line(targets.vert, rx, padB.T, rx, padB.B, 0.5); }
    else { gg_line(targets.vert, lx, padB.T, lx, padB.B, 0.5); }
  }
  var cellH = (padB.H - gutter * (divs - 1)) / divs; if (cellH <= 0) throw Error("分割高度變為負值。請調整間距/內邊距。");
  for (var r = 1; r < divs; r++) {
    var ty = padB.T - (r * cellH + (r - 1) * gutter); ty = gg_snap(ty, 0.5);
    var by = ty - gutter;
    if (gutter > 0) { gg_line(targets.horz, padB.L, ty, padB.R, ty, 0.5); gg_line(targets.horz, padB.L, by, padB.R, by, 0.5); }
    else { gg_line(targets.horz, padB.L, ty, padB.R, ty, 0.5); }
  }
  if (diagsOn) { gg_line(targets.diag, padB.L, padB.T, padB.R, padB.B, 0.5); gg_line(targets.diag, padB.R, padB.T, padB.L, padB.B, 0.5); }
  if (drawFrame) { gg_drawPadFrameLines(targets.frame, baseB, padB); }
}

// ---------- Guides conversion ----------
function gg_itemToGuide(it) {
  try {
    if (it.typename === 'PathItem' || it.typename === 'CompoundPathItem') {
      if (it.typename === 'CompoundPathItem') { var p = it.pathItems; for (var i = p.length - 1; i >= 0; i--) { try { p[i].guides = true; } catch (e) {} } }
      else { it.guides = true; }
    } else if (it.typename === 'GroupItem') { var g = it.pageItems; for (var j = g.length - 1; j >= 0; j--) { gg_itemToGuide(g[j]); } }
    else if (it.typename === 'Layer') { var ls = it.pageItems; for (var k = ls.length - 1; k >= 0; k--) { gg_itemToGuide(ls[k]); } }
  } catch (e) {}
}
function gg_layerToGuidesDeep(L) { gg_itemToGuide(L); for (var i = 0; i < L.layers.length; i++) { gg_layerToGuidesDeep(L.layers[i]); } }

// ---------- Build into a layer ----------
function gg_buildIntoLayer(doc, parent, o) {
  var baseB = (o.scope === 'Selection') ? gg_getSel(doc) : null; if (!baseB) { baseB = gg_getAB(doc); }
  var padB = gg_withPadSides(baseB, o.paddingSides.top, o.paddingSides.right, o.paddingSides.bottom, o.paddingSides.left);
  if (padB.W <= 0 || padB.H <= 0) throw Error("內邊距對於目標來說太大。");
  if (o.type === 'Swiss') {
    // One layer per *feature* (grid / baselines / frame), not one per line-kind —
    // fewer sublayers for the user to wade through in the Layers panel, and each
    // sublayer is only created when that feature actually produces something.
    var gridL = gg_ensureSubLayer(parent, 'Swiss');
    var targets = { cols: gridL, rows: gridL, baseH: null, baseV: null, frame: null };
    if (o.baselineH > 0 || o.baselineV > 0) {
      var baseL = gg_ensureSubLayer(parent, '基準線');
      targets.baseH = baseL; targets.baseV = baseL;
    }
    if (o.drawPadFrame) { targets.frame = gg_ensureSubLayer(parent, '外框'); }
    gg_buildSwiss(targets, baseB, padB, o.cols, o.colGutter, o.rowsOn, o.rowsCount, o.rowGutter, o.baselineH, o.baselineV, o.snapOn, 0.5, o.drawPadFrame);
  } else if (o.type === 'Equal') {
    var eqL = gg_ensureSubLayer(parent, '等分網格');
    var targets2 = { vert: eqL, horz: eqL, diag: eqL, frame: null };
    if (o.drawPadFrame) { targets2.frame = gg_ensureSubLayer(parent, '外框'); }
    gg_buildEqual(targets2, baseB, padB, o.divisions, o.equalGutter, o.equalDiags, o.drawPadFrame);
  } else {
    throw Error('不支援的格線類型：' + o.type);
  }
}

function gg_applyStroke(L, stroke) {
  var it = L.pageItems; for (var i = 0; i < it.length; i++) { try { if (it[i].stroked) it[i].strokeWidth = stroke; } catch (e) {} }
  for (var j = 0; j < L.layers.length; j++) { gg_applyStroke(L.layers[j], stroke); }
}

// ---------- Entry points (called from the panel via evalScript) ----------

// Returns basic context info so the panel can enable/disable itself, including
// the current artboard/selection size (pt) so the panel can dynamically clamp
// slider ranges to combinations that won't fail (see gg_buildSwiss/gg_buildEqual).
function gg_getContext() {
  try {
    if (app.documents.length === 0) return JSON.stringify({ hasDoc: false, hasSelection: false });
    var doc = app.activeDocument;
    var ab = gg_getAB(doc);
    var sel = gg_getSel(doc);
    return JSON.stringify({
      hasDoc: true,
      docName: doc.name,
      artboardW: ab.W,
      artboardH: ab.H,
      hasSelection: !!sel,
      selectionW: sel ? sel.W : 0,
      selectionH: sel ? sel.H : 0
    });
  } catch (e) { return JSON.stringify({ hasDoc: false, hasSelection: false, error: String(e) }); }
}

// Builds/refreshes the live preview layer (non-destructive scratch layer).
//
// Creating/removing path items on a layer makes Illustrator quietly replace
// doc.selection with whatever was just touched (often nothing) — so with
// scope=Selection and 即時預覽 on, the very first preview after selecting
// something wiped that selection, and every following poll then saw
// hasSelection=false and locked the panel back out ("請先選取物件"), even
// though the user never touched their selection themselves. Snapshot it
// before drawing and restore it (in a finally, so it survives thrown
// errors too) once the layer edits are done.
function gg_preview(optsJSON) {
  var doc = null, savedSelection = null;
  try {
    if (app.documents.length === 0) return JSON.stringify({ ok: false, error: "請先打開一個文檔。" });
    doc = app.activeDocument;
    savedSelection = doc.selection;
    var o = JSON.parse(optsJSON);
    var baseB = (o.scope === 'Selection' && gg_getSel(doc)) ? gg_getSel(doc) : gg_getAB(doc);
    var L = gg_ensureLayer(doc, GG_PREVIEW_LAYER);
    gg_clearLayer(L); L.visible = true; L.locked = false;
    var padB = gg_withPadSides(baseB, o.paddingSides.top, o.paddingSides.right, o.paddingSides.bottom, o.paddingSides.left);
    if (padB.W <= 0 || padB.H <= 0) throw Error("內邊距對於目標來說太大。");
    if (o.type === 'Swiss') {
      gg_buildSwiss({ cols: L, rows: L, baseH: L, baseV: L, frame: L }, baseB, padB, o.cols, o.colGutter, o.rowsOn, o.rowsCount, o.rowGutter, o.baselineH, o.baselineV, o.snapOn, 0.5, o.drawPadFrame);
    } else if (o.type === 'Equal') {
      gg_buildEqual({ vert: L, horz: L, diag: L, frame: L }, baseB, padB, o.divisions, o.equalGutter, o.equalDiags, o.drawPadFrame);
    } else {
      throw Error('不支援的格線類型：' + o.type);
    }
    var it = L.pageItems; for (var i = 0; i < it.length; i++) { try { if (it[i].stroked) it[i].strokeWidth = o.stroke; } catch (e) {} }
    app.redraw();
    return JSON.stringify({ ok: true });
  } catch (e) { return JSON.stringify({ ok: false, error: String(e.message || e) }); }
  finally { try { if (doc && savedSelection && savedSelection.length) doc.selection = savedSelection; } catch (e2) {} }
}

function gg_clearPreview() {
  try {
    if (app.documents.length === 0) return JSON.stringify({ ok: false, error: "請先打開一個文檔。" });
    gg_removeLayer(app.activeDocument, GG_PREVIEW_LAYER);
    app.redraw();
    return JSON.stringify({ ok: true });
  } catch (e) { return JSON.stringify({ ok: false, error: String(e.message || e) }); }
}

// ---------- Presets ----------
// Stored as a plain JSON file under Folder.userData (Mac: ~/Library/Application
// Support, Win: %APPDATA%) rather than the panel's own CEP localStorage —
// that cache is keyed by Illustrator's version string, so it silently resets
// on every Illustrator upgrade. A file here survives extension reinstalls,
// Illustrator upgrades, and panel reloads; only deleting it by hand loses it.
var GG_PRESET_FOLDER_NAME = "ZurichSeq";
function gg_getPresetFile() {
  var folder = new Folder(Folder.userData.fsName + "/" + GG_PRESET_FOLDER_NAME);
  if (!folder.exists) folder.create();
  return new File(folder.fsName + "/presets.json");
}

function gg_loadPresets() {
  try {
    var f = gg_getPresetFile();
    if (!f.exists) return JSON.stringify({ ok: true, presets: [] });
    f.encoding = "UTF-8";
    f.open("r");
    var content = f.read();
    f.close();
    var presets = content ? JSON.parse(content) : [];
    return JSON.stringify({ ok: true, presets: presets });
  } catch (e) {
    return JSON.stringify({ ok: false, error: String(e.message || e), presets: [] });
  }
}

function gg_savePresets(payloadJSON) {
  try {
    var o = JSON.parse(payloadJSON);
    var f = gg_getPresetFile();
    f.encoding = "UTF-8";
    f.open("w");
    f.write(JSON.stringify(o.presets || []));
    f.close();
    return JSON.stringify({ ok: true });
  } catch (e) {
    return JSON.stringify({ ok: false, error: String(e.message || e) });
  }
}

// Removes whatever the last Apply produced — the applied layer and
// everything in it, whether it ended up as real paths (轉換為參考線 off) or
// converted guides (on) — so the canvas can go back to clean before judging
// a re-adjusted grid, regardless of which output mode was used.
function gg_clearApplied() {
  try {
    if (app.documents.length === 0) return JSON.stringify({ ok: false, error: "請先打開一個文檔。" });
    gg_removeLayer(app.activeDocument, GG_APPLIED_LAYER);
    app.redraw();
    return JSON.stringify({ ok: true });
  } catch (e) { return JSON.stringify({ ok: false, error: String(e.message || e) }); }
}

// Commits the grid: (re)builds a single, fixed-name layer so re-applying
// with new numbers replaces the previous grid instead of piling up new ones.
var GG_APPLIED_LAYER = "Zurich Seq";
// Same selection-loss issue as gg_preview (see its comment) — capture the
// user's selection before building the layer and put it back afterward,
// since gg_getSel(doc) also needs to read the ORIGINAL selection first.
function gg_apply(optsJSON) {
  var doc = null, savedSelection = null;
  try {
    if (app.documents.length === 0) return JSON.stringify({ ok: false, error: "請先打開一個文檔。" });
    doc = app.activeDocument;
    savedSelection = doc.selection;
    gg_removeLayer(doc, GG_PREVIEW_LAYER);
    var o = JSON.parse(optsJSON);
    gg_removeLayer(doc, GG_APPLIED_LAYER);
    var parent = gg_ensureLayer(doc, GG_APPLIED_LAYER); gg_clearLayer(parent);
    parent.visible = true; parent.locked = false;
    gg_buildIntoLayer(doc, parent, o);
    gg_applyStroke(parent, o.stroke);
    if (o.convertToGuides) { gg_layerToGuidesDeep(parent); }
    app.redraw();
    return JSON.stringify({ ok: true, layerName: GG_APPLIED_LAYER });
  } catch (e) { return JSON.stringify({ ok: false, error: String(e.message || e) }); }
  finally { try { if (doc && savedSelection && savedSelection.length) doc.selection = savedSelection; } catch (e2) {} }
}

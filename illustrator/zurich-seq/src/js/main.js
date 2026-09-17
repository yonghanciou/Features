(function () {
  "use strict";

  var csInterface;
  try {
    csInterface = new CSInterface();
  } catch (err) {
    console.warn('CSInterface not available outside Illustrator.', err);
    return;
  }

  function $(id) { return document.getElementById(id); }
  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

  // Shared by the wheel handler and the custom stepper buttons: nudge a
  // number/range field by one step in the given direction (+1/-1), rounded
  // to the field's own decimal precision (avoids float drift like
  // 5 + 0.1 -> 5.1000000000000005), then fire the same events its native
  // typing/dragging path fires so the existing binder logic (clamping,
  // link-toggle mirroring, onAnyChange) picks it up unchanged.
  function stepInput(el, dir) {
    if (!el || el.disabled) return;
    var step = parseFloat(el.step) || 1;
    var decimals = (String(el.step).split('.')[1] || '').length;
    var factor = Math.pow(10, decimals);
    var next = (Number(el.value) || 0) + dir * step;
    next = Math.round(next * factor) / factor;
    if (el.min !== '') next = Math.max(Number(el.min), next);
    if (el.max !== '') next = Math.min(Number(el.max), next);
    el.value = next;
    el.dispatchEvent(new Event('input', { bubbles: true }));
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }

  // Scroll-to-adjust: hovering a number/range field and spinning the wheel
  // nudges its value by one step, same as clicking a stepper arrow would.
  // Delegated on document so every current and future field gets it for
  // free without individually wiring listeners.
  function bindWheelAdjust() {
    document.addEventListener('wheel', function (ev) {
      var el = ev.target;
      if (!el || (el.type !== 'number' && el.type !== 'range') || el.disabled) return;
      ev.preventDefault();
      stepInput(el, ev.deltaY < 0 ? 1 : -1);
    }, { passive: false });
  }

  var STEP_UP_PATH = 'M10 6.5l5.5 5.5-1.4 1.4L10 9.3l-4.1 4.1-1.4-1.4z';
  var STEP_DOWN_PATH = 'M10 13.5L4.5 8l1.4-1.4L10 10.7l4.1-4.1L15.5 8z';

  // Replaces every number field's native spin buttons (hidden via CSS) with
  // a custom up/down chevron stepper matching the panel's own icon style —
  // wraps each <input> in a .numfield span in place, so ids/listeners/
  // getElementById lookups on the input itself are unaffected.
  function addNumberSteppers() {
    var inputs = document.querySelectorAll('input[type="number"]');
    inputs.forEach(function (input) {
      var wrap = document.createElement('span');
      wrap.className = 'numfield';
      input.parentNode.insertBefore(wrap, input);
      wrap.appendChild(input);

      var stepper = document.createElement('span');
      stepper.className = 'num-stepper';

      var up = document.createElement('button');
      up.type = 'button';
      up.tabIndex = -1;
      up.innerHTML = '<svg viewBox="0 0 20 20"><path fill="currentColor" d="' + STEP_UP_PATH + '"/></svg>';
      up.addEventListener('click', function () { stepInput(input, 1); });

      var down = document.createElement('button');
      down.type = 'button';
      down.tabIndex = -1;
      down.innerHTML = '<svg viewBox="0 0 20 20"><path fill="currentColor" d="' + STEP_DOWN_PATH + '"/></svg>';
      down.addEventListener('click', function () { stepInput(input, -1); });

      stepper.appendChild(up);
      stepper.appendChild(down);
      wrap.appendChild(stepper);
    });
  }
  function round1(v) { return Math.round(v * 10) / 10; }
  function round2(v) { return Math.round(v * 100) / 100; }
  function mmToPts(mm) { return (Number(mm) || 0) * 72 / 25.4; }
  function ptsToMm(pt) { return (Number(pt) || 0) * 25.4 / 72; }

  var currentUnit = 'mm';
  var statusbar = $('statusbar');
  var hasDoc = false;
  var lastBounds = { artboardW: 0, artboardH: 0, hasSelection: false, selectionW: 0, selectionH: 0 };
  var syncTimer = null;

  function setStatus(msg) {
    statusbar.textContent = msg;
  }

  function callHost(fnName, argObj, cb) {
    var payload = argObj === undefined ? {} : argObj;
    var script = argObj === undefined ? (fnName + '()') : (fnName + '(' + JSON.stringify(JSON.stringify(payload)) + ')');
    csInterface.evalScript(script, function (result) {
      var parsed;
      try {
        parsed = JSON.parse(result);
      } catch (e) {
        parsed = { ok: false, error: 'Bad response from host: ' + result };
      }
      if (cb) cb(parsed);
    });
  }

  function bindCount(rangeId, numberId) {
    var rangeEl = $(rangeId);
    var numberEl = $(numberId);

    function clampValue(v) {
      return clamp(Math.round(v), Number(rangeEl.min), Number(rangeEl.max));
    }

    function syncFromRange() {
      var v = clampValue(Number(rangeEl.value) || 0);
      numberEl.value = v;
      onAnyChange();
    }

    function syncFromNumber() {
      var v = clampValue(Number(numberEl.value) || 0);
      rangeEl.value = v;
      numberEl.value = v;
      onAnyChange();
    }

    rangeEl.addEventListener('input', syncFromRange);
    rangeEl.addEventListener('change', syncFromRange);
    numberEl.addEventListener('input', syncFromNumber);
    numberEl.addEventListener('change', syncFromNumber);

    return {
      get: function () { return clampValue(Number(rangeEl.value) || 0); },
      set: function (v) {
        var clamped = clampValue(v);
        rangeEl.value = clamped;
        numberEl.value = clamped;
      }
    };
  }

  function bindLength(rangeId, numberId, initialPt, ranges) {
    var rangeEl = $(rangeId);
    var numberEl = $(numberId);
    var dynMaxPt = null;

    function displayValue(pt) {
      return currentUnit === 'pt' ? round1(pt) : round2(ptsToMm(pt));
    }

    function ptValue(v) {
      return currentUnit === 'pt' ? (Number(v) || 0) : mmToPts(Number(v) || 0);
    }

    function effectiveMax() {
      var max = ranges[currentUnit].max;
      if (dynMaxPt !== null) max = Math.min(max, displayValue(dynMaxPt));
      return max;
    }

    function syncBounds() {
      rangeEl.min = ranges[currentUnit].min;
      rangeEl.max = effectiveMax();
      numberEl.min = ranges[currentUnit].min;
      numberEl.max = effectiveMax();
    }

    function syncFromRange() {
      var v = clamp(Number(rangeEl.value) || 0, Number(rangeEl.min), Number(rangeEl.max));
      numberEl.value = v;
      onAnyChange();
    }

    function syncFromNumber() {
      var v = clamp(Number(numberEl.value) || 0, Number(rangeEl.min), Number(rangeEl.max));
      rangeEl.value = v;
      numberEl.value = v;
      onAnyChange();
    }

    syncBounds();
    rangeEl.value = displayValue(initialPt);
    numberEl.value = displayValue(initialPt);

    rangeEl.addEventListener('input', syncFromRange);
    rangeEl.addEventListener('change', syncFromRange);
    numberEl.addEventListener('input', syncFromNumber);
    numberEl.addEventListener('change', syncFromNumber);

    return {
      getPt: function () {
        return ptValue(Number(numberEl.value) || 0);
      },
      setPt: function (pt) {
        syncBounds();
        var v = displayValue(pt);
        rangeEl.value = v;
        numberEl.value = v;
      },
      setMaxPt: function (maxPt) {
        dynMaxPt = maxPt;
        syncBounds();
        var max = Number(rangeEl.max);
        var current = Number(numberEl.value) || 0;
        if (current > max) {
          rangeEl.value = max;
          numberEl.value = max;
          return true;
        }
        return false;
      },
      resetMax: function () {
        dynMaxPt = null;
        syncBounds();
      }
    };
  }

  function bindLengthPt(rangeId, numberId) {
    var rangeEl = $(rangeId);
    var numberEl = $(numberId);

    function syncFromRange() {
      numberEl.value = Number(rangeEl.value) || 0;
      onAnyChange();
    }

    function syncFromNumber() {
      var v = clamp(Number(numberEl.value) || 0, Number(rangeEl.min), Number(rangeEl.max));
      rangeEl.value = v;
      numberEl.value = v;
      onAnyChange();
    }

    rangeEl.addEventListener('input', syncFromRange);
    rangeEl.addEventListener('change', syncFromRange);
    numberEl.addEventListener('input', syncFromNumber);
    numberEl.addEventListener('change', syncFromNumber);

    return {
      getPt: function () { return Number(numberEl.value) || 0; },
      setPt: function (pt) {
        var v = round1(pt);
        rangeEl.value = v;
        numberEl.value = v;
      }
    };
  }

  function bindUnitField(id, initialPt) {
    var el = $(id);
    var dynMaxPt = null;

    function displayValue(pt) {
      return currentUnit === 'pt' ? round1(pt) : round2(ptsToMm(pt));
    }

    function ptValue(v) {
      return currentUnit === 'pt' ? (Number(v) || 0) : mmToPts(Number(v) || 0);
    }

    function syncFromField() {
      var v = Math.max(0, Number(el.value) || 0);
      if (dynMaxPt !== null && v > dynMaxPt) v = dynMaxPt;
      el.value = displayValue(ptValue(v));
      onAnyChange();
    }

    el.value = displayValue(initialPt);
    el.addEventListener('input', syncFromField);
    el.addEventListener('change', syncFromField);

    return {
      getPt: function () { return ptValue(Number(el.value) || 0); },
      setPt: function (pt) {
        el.value = displayValue(pt);
      },
      setMaxPt: function (maxPt) {
        dynMaxPt = maxPt;
        var v = Number(el.value) || 0;
        if (ptValue(v) > maxPt) {
          el.value = displayValue(maxPt);
          return true;
        }
        return false;
      },
      resetMax: function () { dynMaxPt = null; }
    };
  }

  function updateTypePanels() {
    var type = $('typeSel').value;
    $('swissPanel').style.display = type === 'Swiss' ? '' : 'none';
    $('equalPanel').style.display = type === 'Equal' ? '' : 'none';
  }

  function setPadModeUI() {
    var enabled = $('padEnable').checked;
    $('padPerSideRow').style.display = enabled ? '' : 'none';
  }

  function hasValidScope() {
    return $('scopeSel').value !== 'Selection' || lastBounds.hasSelection;
  }

  function currentBaseWH() {
    if ($('scopeSel').value === 'Selection' && lastBounds.hasSelection) {
      return { W: lastBounds.selectionW, H: lastBounds.selectionH };
    }
    return { W: lastBounds.artboardW, H: lastBounds.artboardH };
  }

  function refreshContext() {
    callHost('gg_getContext', undefined, function (res) {
      hasDoc = !!res.hasDoc;
      lastBounds.artboardW = res.artboardW || 0;
      lastBounds.artboardH = res.artboardH || 0;
      lastBounds.hasSelection = !!res.hasSelection;
      lastBounds.selectionW = res.selectionW || 0;
      lastBounds.selectionH = res.selectionH || 0;
      statusbar.dataset.lastKnownDocName = res.docName || '';
      updateLockState();
      if (hasDoc && hasValidScope()) applyDynamicConstraints();
    });
  }

  function updateLockState() {
    var locked = $('scopeSel').value === 'Selection' && !lastBounds.hasSelection;
    var enabled = hasDoc && hasValidScope() && !locked;

    $('previewBtn').disabled = !enabled;
    $('applyBtn').disabled = !enabled;
    $('clearBtn').disabled = !hasDoc;
    $('clearAppliedBtn').disabled = !hasDoc;

    if (!hasDoc) {
      setStatus('請先打開一個文件。');
    } else if (locked) {
      setStatus('請先選取物件。');
    } else {
      setStatus('文件：' + (statusbar.dataset.lastKnownDocName || ''));
    }
  }

  function readOpts() {
    var padEnabled = $('padEnable').checked;
    var pads;
    if (!padEnabled) {
      pads = { top: 0, right: 0, bottom: 0, left: 0 };
    } else {
      pads = { top: pT.getPt(), right: pR.getPt(), bottom: pB.getPt(), left: pL.getPt() };
    }

    var colGutter = sColGutter.getPt();
    var rowGutter = sRowGutter.getPt();
    if ($('s_gutLink').checked) rowGutter = colGutter;

    var baselineH = sBaseH.getPt();
    var baselineV = sBaseV.getPt();
    if ($('s_baseLink').checked) baselineV = baselineH;

    return {
      type: $('typeSel').value,
      scope: $('scopeSel').value,
      paddingSides: pads,
      cols: sCols.get(),
      colGutter: colGutter,
      rowsOn: true,
      rowsCount: sRows.get(),
      rowGutter: rowGutter,
      baselineH: baselineH,
      baselineV: baselineV,
      snapOn: true,
      snapStep: 0.5,
      divisions: eDiv.get(),
      equalGutter: eGutter.getPt(),
      equalDiags: $('e_diags').checked,
      drawPadFrame: $('drawFrame').checked && padEnabled,
      convertToGuides: $('convChk').checked,
      stroke: strokeCtrl.getPt()
    };
  }

  function doPreview() {
    if (!hasDoc || !hasValidScope()) return;
    callHost('gg_preview', readOpts(), function (res) {
      if (res.ok) setStatus('即時預覽已更新。');
      else setStatus('預覽錯誤：' + res.error);
    });
  }

  function doClear() {
    if (!hasDoc) return;
    callHost('gg_clearPreview', undefined, function (res) {
      if (res.ok) setStatus('已清除預覽。');
      else setStatus('清除錯誤：' + res.error);
    });
  }

  function doClearApplied() {
    if (!hasDoc) return;
    callHost('gg_clearApplied', undefined, function (res) {
      if (res.ok) setStatus('已清除套用的格線。');
      else setStatus('清除錯誤：' + res.error);
    });
  }

  // ---------- Presets ----------
  // Stored in a JSON file under Folder.userData on the host side (see
  // gg_loadPresets/gg_savePresets in hostscript.jsx) rather than the panel's
  // own CEP localStorage — that cache is keyed by Illustrator's version
  // string and resets on every upgrade. presetsCache mirrors the file's
  // contents in memory so the UI can read it synchronously; every mutation
  // writes it back to disk via callHost.
  var presetsCache = [];

  function loadPresetsFromHost(cb) {
    callHost('gg_loadPresets', undefined, function (res) {
      presetsCache = (res && res.ok && res.presets) ? res.presets : [];
      if (cb) cb();
    });
  }

  function persistPresets() {
    callHost('gg_savePresets', { presets: presetsCache }, function (res) {
      if (!res.ok) setStatus('儲存預設失敗：' + res.error);
    });
  }

  function capturePreset() {
    return {
      typeSel: $('typeSel').value,
      scopeSel: $('scopeSel').value,
      unitSel: $('unitSel').value,
      liveChk: $('liveChk').checked,
      padEnable: $('padEnable').checked,
      pad: { t: pT.getPt(), r: pR.getPt(), b: pB.getPt(), l: pL.getPt() },
      padLink: $('p_link').checked,
      drawFrame: $('drawFrame').checked,
      convChk: $('convChk').checked,
      cols: sCols.get(),
      colGutter: sColGutter.getPt(),
      rows: sRows.get(),
      rowGutter: sRowGutter.getPt(),
      gutLink: $('s_gutLink').checked,
      baselineH: sBaseH.getPt(),
      baselineV: sBaseV.getPt(),
      baseLink: $('s_baseLink').checked,
      divisions: eDiv.get(),
      equalGutter: eGutter.getPt(),
      equalDiags: $('e_diags').checked,
      stroke: strokeCtrl.getPt()
    };
  }

  function applyPreset(p) {
    if (!p) return;
    if (p.unitSel) { currentUnit = p.unitSel; $('unitSel').value = p.unitSel; }
    $('typeSel').value = p.typeSel; updateTypePanels();
    $('scopeSel').value = p.scopeSel;
    $('liveChk').checked = !!p.liveChk;
    $('padEnable').checked = !!p.padEnable; setPadModeUI();
    pT.setPt(p.pad.t); pR.setPt(p.pad.r); pB.setPt(p.pad.b); pL.setPt(p.pad.l);
    $('p_link').checked = !!p.padLink;
    $('drawFrame').checked = !!p.drawFrame;
    $('convChk').checked = !!p.convChk;
    sCols.set(p.cols); sColGutter.setPt(p.colGutter);
    sRows.set(p.rows); sRowGutter.setPt(p.rowGutter);
    $('s_gutLink').checked = !!p.gutLink;
    sBaseH.setPt(p.baselineH); sBaseV.setPt(p.baselineV);
    $('s_baseLink').checked = !!p.baseLink;
    eDiv.set(p.divisions); eGutter.setPt(p.equalGutter);
    $('e_diags').checked = !!p.equalDiags;
    strokeCtrl.setPt(p.stroke);
    updateLockState();
    onAnyChange();
    setStatus('已套用預設「' + p.name + '」。');
  }

  function renderPresetSelect(selectName) {
    var sel = $('presetSel');
    sel.innerHTML = '';
    var placeholder = document.createElement('option');
    placeholder.value = '';
    placeholder.textContent = '— 選擇預設 —';
    sel.appendChild(placeholder);
    presetsCache.forEach(function (p, idx) {
      var opt = document.createElement('option');
      opt.value = String(idx);
      opt.textContent = p.name;
      sel.appendChild(opt);
    });
    if (selectName) {
      for (var i = 0; i < presetsCache.length; i++) {
        if (presetsCache[i].name === selectName) { sel.value = String(i); break; }
      }
    }
    $('presetDeleteBtn').disabled = !sel.value;
  }

  function bindPresetUI() {
    $('presetSel').addEventListener('change', function () {
      var idx = $('presetSel').value;
      $('presetDeleteBtn').disabled = !idx;
      if (idx === '') return;
      applyPreset(presetsCache[Number(idx)]);
    });

    $('presetSaveBtn').addEventListener('click', function () {
      var selIdx = $('presetSel').value;
      $('presetNameInput').value = selIdx !== '' ? presetsCache[Number(selIdx)].name : '';
      $('presetSaveRow').style.display = '';
      $('presetNameInput').focus();
    });

    $('presetSaveCancelBtn').addEventListener('click', function () {
      $('presetSaveRow').style.display = 'none';
    });

    $('presetSaveConfirmBtn').addEventListener('click', function () {
      var name = $('presetNameInput').value.trim();
      if (!name) return;
      var data = capturePreset();
      data.name = name;
      var existingIdx = -1;
      for (var i = 0; i < presetsCache.length; i++) { if (presetsCache[i].name === name) { existingIdx = i; break; } }
      if (existingIdx >= 0) presetsCache[existingIdx] = data;
      else presetsCache.push(data);
      persistPresets();
      renderPresetSelect(name);
      $('presetSaveRow').style.display = 'none';
      setStatus((existingIdx >= 0 ? '已覆蓋預設「' : '已儲存預設「') + name + '」。');
    });

    $('presetNameInput').addEventListener('keydown', function (ev) {
      if (ev.key === 'Enter') { ev.preventDefault(); $('presetSaveConfirmBtn').click(); }
      else if (ev.key === 'Escape') { ev.preventDefault(); $('presetSaveCancelBtn').click(); }
    });

    $('presetDeleteBtn').addEventListener('click', function () {
      var idx = $('presetSel').value;
      if (idx === '') return;
      var name = presetsCache[Number(idx)].name;
      presetsCache.splice(Number(idx), 1);
      persistPresets();
      renderPresetSelect();
      setStatus('已刪除預設「' + name + '」。');
    });
  }

  function doApply() {
    if (!hasDoc || !hasValidScope()) return;
    callHost('gg_apply', readOpts(), function (res) {
      if (res.ok) setStatus('已套用：已更新「' + res.layerName + '」圖層。');
      else setStatus('套用錯誤：' + res.error);
    });
  }

  var DYN_EPS = 0.1;
  var dynamicGuard = false;

  function applyDynamicConstraints() {
    if (dynamicGuard) return false;
    dynamicGuard = true;
    try {
      var base = currentBaseWH();
      if (!base.W || !base.H) return false;

      var changed = false;
      var padEnabled = $('padEnable').checked;

      if (padEnabled) {
        if (pT.setMaxPt(Math.max(0, base.H - pB.getPt() - DYN_EPS))) changed = true;
        if (pB.setMaxPt(Math.max(0, base.H - pT.getPt() - DYN_EPS))) changed = true;
        if (pL.setMaxPt(Math.max(0, base.W - pR.getPt() - DYN_EPS))) changed = true;
        if (pR.setMaxPt(Math.max(0, base.W - pL.getPt() - DYN_EPS))) changed = true;
      } else {
        pT.resetMax(); pB.resetMax(); pL.resetMax(); pR.resetMax();
      }

      var padW = base.W;
      var padH = base.H;
      if (padEnabled) {
        padW -= (pL.getPt() + pR.getPt());
        padH -= (pT.getPt() + pB.getPt());
      }
      padW = Math.max(0, padW);
      padH = Math.max(0, padH);

      if (sCols.get() > 1) {
        if (sColGutter.setMaxPt(Math.max(0, padW / (sCols.get() - 1) - DYN_EPS))) changed = true;
      } else {
        sColGutter.resetMax();
      }

      if (sRows.get() > 1) {
        if (sRowGutter.setMaxPt(Math.max(0, padH / (sRows.get() - 1) - DYN_EPS))) changed = true;
      } else {
        sRowGutter.resetMax();
      }

      if (eDiv.get() > 1) {
        var maxEq = Math.max(0, Math.min(padW, padH) / (eDiv.get() - 1) - DYN_EPS);
        if (eGutter.setMaxPt(maxEq)) changed = true;
      } else {
        eGutter.resetMax();
      }

      return changed;
    } finally {
      dynamicGuard = false;
    }
  }

  // Rapid dragging fires many 'input' events per second; recomputing every
  // field's dynamic max on every single tick is wasted work once the user is
  // mid-drag rather than settling on a value, so throttle it to a cadence
  // that still reads as instantaneous.
  var DYN_THROTTLE_MS = 80;
  var _lastDynRun = 0;

  function onAnyChange() {
    var selectionLocked = $('scopeSel').value === 'Selection' && !lastBounds.hasSelection;
    if (hasDoc && hasValidScope() && !selectionLocked) {
      var now = Date.now();
      if (now - _lastDynRun >= DYN_THROTTLE_MS) {
        _lastDynRun = now;
        applyDynamicConstraints();
      }
    }
    if (!$('liveChk').checked) return;
    if (!hasDoc || !hasValidScope() || selectionLocked) return;
    if (syncTimer) clearTimeout(syncTimer);
    syncTimer = setTimeout(doPreview, 120);
  }

  function bindChecks() {
    $('typeSel').addEventListener('change', function () { updateTypePanels(); onAnyChange(); });
    $('liveChk').addEventListener('change', onAnyChange);
    $('unitSel').addEventListener('change', function () {
      var saved = [sColGutter, sRowGutter, sBaseH, sBaseV, eGutter, pT, pR, pB, pL, strokeCtrl].map(function (item) { return item.getPt(); });
      currentUnit = $('unitSel').value;
      [sColGutter, sRowGutter, sBaseH, sBaseV, eGutter, pT, pR, pB, pL, strokeCtrl].forEach(function (item, idx) { item.setPt(saved[idx]); });
      onAnyChange();
    });
    $('s_gutLink').addEventListener('change', function () {
      if ($('s_gutLink').checked) sRowGutter.setPt(sColGutter.getPt());
      onAnyChange();
    });
    $('s_baseLink').addEventListener('change', function () {
      if ($('s_baseLink').checked) sBaseV.setPt(sBaseH.getPt());
      onAnyChange();
    });
    $('e_diags').addEventListener('change', onAnyChange);
    $('scopeSel').addEventListener('change', function () { updateLockState(); refreshContext(); onAnyChange(); });
    $('padEnable').addEventListener('change', function () { setPadModeUI(); onAnyChange(); });
    $('drawFrame').addEventListener('change', onAnyChange);
    $('convChk').addEventListener('change', onAnyChange);
    $('previewBtn').addEventListener('click', doPreview);
    $('clearBtn').addEventListener('click', doClear);
    $('clearAppliedBtn').addEventListener('click', doClearApplied);
    $('applyBtn').addEventListener('click', doApply);
    $('s_colGutter_r').addEventListener('input', function () { if ($('s_gutLink').checked) sRowGutter.setPt(sColGutter.getPt()); });
    $('s_rowGutter_r').addEventListener('input', function () { if ($('s_gutLink').checked) sColGutter.setPt(sRowGutter.getPt()); });
    $('s_colGutter_n').addEventListener('change', function () { if ($('s_gutLink').checked) sRowGutter.setPt(sColGutter.getPt()); });
    $('s_rowGutter_n').addEventListener('change', function () { if ($('s_gutLink').checked) sColGutter.setPt(sRowGutter.getPt()); });
    $('s_baseH_r').addEventListener('input', function () { if ($('s_baseLink').checked) sBaseV.setPt(sBaseH.getPt()); });
    $('s_baseV_r').addEventListener('input', function () { if ($('s_baseLink').checked) sBaseH.setPt(sBaseV.getPt()); });
    $('s_baseH_n').addEventListener('change', function () { if ($('s_baseLink').checked) sBaseV.setPt(sBaseH.getPt()); });
    $('s_baseV_n').addEventListener('change', function () { if ($('s_baseLink').checked) sBaseH.setPt(sBaseV.getPt()); });
    $('p_t').addEventListener('change', function () { if ($('p_link').checked) { var v = pT.getPt(); pR.setPt(v); pB.setPt(v); pL.setPt(v); } onAnyChange(); });
    $('p_r').addEventListener('change', function () { if ($('p_link').checked) { var v = pR.getPt(); pT.setPt(v); pB.setPt(v); pL.setPt(v); } onAnyChange(); });
    $('p_b').addEventListener('change', function () { if ($('p_link').checked) { var v = pB.getPt(); pT.setPt(v); pR.setPt(v); pL.setPt(v); } onAnyChange(); });
    $('p_l').addEventListener('change', function () { if ($('p_link').checked) { var v = pL.getPt(); pT.setPt(v); pR.setPt(v); pB.setPt(v); } onAnyChange(); });
    $('p_link').addEventListener('change', function () { if ($('p_link').checked) { var v = pT.getPt(); pR.setPt(v); pB.setPt(v); pL.setPt(v); } onAnyChange(); });
  }

  var sCols = bindCount('s_cols_r', 's_cols_n');
  var sColGutter = bindLength('s_colGutter_r', 's_colGutter_n', mmToPts(5), { pt: { min: 0, max: 600 }, mm: { min: 0, max: 211.7 } });
  var sRows = bindCount('s_rows_r', 's_rows_n');
  var sRowGutter = bindLength('s_rowGutter_r', 's_rowGutter_n', mmToPts(5), { pt: { min: 0, max: 600 }, mm: { min: 0, max: 211.7 } });
  var sBaseH = bindLength('s_baseH_r', 's_baseH_n', 0, { pt: { min: 0, max: 400 }, mm: { min: 0, max: 141.1 } });
  var sBaseV = bindLength('s_baseV_r', 's_baseV_n', 0, { pt: { min: 0, max: 400 }, mm: { min: 0, max: 141.1 } });
  var eDiv = bindCount('e_div_r', 'e_div_n');
  var eGutter = bindLength('e_gutter_r', 'e_gutter_n', 0, { pt: { min: 0, max: 600 }, mm: { min: 0, max: 211.7 } });
  var pT = bindUnitField('p_t', mmToPts(10));
  var pR = bindUnitField('p_r', mmToPts(10));
  var pB = bindUnitField('p_b', mmToPts(10));
  var pL = bindUnitField('p_l', mmToPts(10));
  var strokeCtrl = bindLengthPt('strokeCtrl_r', 'strokeCtrl_n');

  bindChecks();
  addNumberSteppers();
  bindWheelAdjust();
  bindPresetUI();
  loadPresetsFromHost(function () { renderPresetSelect(); });
  updateTypePanels();
  setPadModeUI();
  refreshContext();
  setInterval(refreshContext, 1500);
})();

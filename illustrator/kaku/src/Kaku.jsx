/**
 * Kaku (像素化) v1.0 — 免安裝獨立 ExtendScript 版本
 * 把選取的路徑重新取樣後，將每個點吸附到方格網格上，
 * 做出階梯狀的「像素化」輪廓。
 *
 * 這是 Kaku 原生 Illustrator 外掛（.aip）的輕量替代方案：邏輯移植自
 * 外掛開發過程中的 ExtendScript 原型，沒有原生版後來補上的複合路徑
 * 鏤空正確處理、鋸齒角落斜切修正、直線邊緣浮點誤差修正，且是破壞性
 * 操作、不會出現在外觀面板裡回頭調整。詳見 illustrator/kaku/README.md。
 *
 * 安裝：放到 Illustrator 應用程式資料夾的
 *   Presets/zh_TW/指令碼/   （英文版為 Presets/en_US/Scripts/）
 * 重開 Illustrator 後出現在「檔案 > 指令碼」選單。
 * 或直接用「檔案 > 指令碼 > 其他指令碼…」執行。
 *
 * 這一版新增：
 *  - 對話框內拖曳格子大小／取樣密度等滑桿時，畫布上的物件會即時更新，
 *    所見即所得；按「取消」會完整還原，按「執行」才會真正套用。
 *  - 預覽期間會暫時隱藏原始物件、改顯示一份處理後的複本；
 *    複本只是暫時性的預覽稿，正式套用時仍會用原本的完整流程處理。
 *  - 兩種像素化模式可選：
 *      格點吸附：保留原本角度，鋸齒貼合原輪廓（沿用原本演算法）。
 *      完全像素化：整格填滿、只有直角，真正的「方塊像素」效果；
 *        只支援封閉路徑，拆成多塊或出現鏤空時會自動轉成複合路徑。
 *  - 格子大小可以用「跨向格數」等比例換算（依整體選取範圍的長邊
 *    切成幾格），不必猜絕對 pt 數字，要做到極致的大方塊也很直覺。
 *  - 格子設得比物件本身還大時不會直接放棄：封閉路徑會整個簡化成
 *    一塊最小的方塊，而不是什麼都不做。
 *
 * 注意：
 *  - 會把貝茲曲線轉成折線，是破壞性操作。想保留原稿請勾選「保留原件」。
 *  - 即時效果（外觀面板裡的）不會被處理，請先「物件 > 擴充外觀」。
 *  - 文字需要建立外框；可在對話框勾選自動轉換。
 */

#target illustrator

(function () {

    // ============================================================
    // 前置檢查
    // ============================================================
    if (app.documents.length === 0) {
        alert("請先開啟一個檔案。");
        return;
    }
    var doc = app.activeDocument;
    var sel = doc.selection;
    if (!sel || sel.length === 0) {
        alert("請先選取要處理的物件。");
        return;
    }

    // 把 selection 快照成一般陣列（後續操作會動到 selection）
    var seed = [];
    for (var i = 0; i < sel.length; i++) seed.push(sel[i]);

    var cfg = runDialogWithLivePreview(seed);
    if (!cfg) return;

    // ============================================================
    // 保留原件：先複製一份，處理複本
    // ============================================================
    var roots = [];
    if (cfg.keepOriginal) {
        for (var r = 0; r < seed.length; r++) {
            try { roots.push(seed[r].duplicate()); }
            catch (e) { roots.push(seed[r]); }
        }
    } else {
        roots = seed;
    }

    // ============================================================
    // 蒐集目標路徑
    // ============================================================
    var stats = {
        text: 0, plugin: 0, locked: 0, guide: 0,
        clip: 0, other: 0, degenerate: 0, capped: 0,
        openSkipped: 0, toobig: 0, collapsed: 0
    };
    var targets = [];
    var texts = [];

    collect(roots, targets, texts, stats, cfg);

    // 文字自動建立外框
    if (cfg.outlineText && texts.length > 0) {
        var newGroups = [];
        for (var t = 0; t < texts.length; t++) {
            try { newGroups.push(texts[t].createOutline()); } catch (e) {}
        }
        collect(newGroups, targets, [], stats, cfg);
        stats.text = 0;
    }

    if (targets.length === 0) {
        alert("選取範圍內沒有可處理的路徑。\n\n" +
              "若是即時效果或封套，請先「物件 > 擴充外觀」；\n" +
              "若是文字，請建立外框或在對話框勾選自動轉換。");
        return;
    }

    // ============================================================
    // 格點原點／格子大小
    // ============================================================
    var ub = unionBounds(targets);   // 原點與「跨向格數」比例都可能用到
    var ox = 0, oy = 0;
    if (!cfg.originDoc) {
        ox = ub[0];   // left
        oy = ub[1];   // top
    }

    var effectiveCell = deriveEffectiveCell(cfg, ub);

    var runCfg = {};
    for (var ck in cfg) { if (cfg.hasOwnProperty(ck)) runCfg[ck] = cfg[ck]; }
    runCfg.cell = effectiveCell;

    // ============================================================
    // 執行
    // ============================================================
    var spacing = Math.max(0.2, runCfg.cell / runCfg.density);
    var done = 0;

    for (var k = 0; k < targets.length; k++) {
        var res = processPath(targets[k], runCfg, spacing, ox, oy, stats);
        if (!res) continue;
        done++;
        if (res !== true) {
            // 完全像素化模式把原路徑換成了複合路徑；若原路徑本身就是
            // 選取的頂層物件，reselect 時要指到新的物件才不會抓空。
            for (var rIdx = 0; rIdx < roots.length; rIdx++) {
                if (roots[rIdx] === targets[k]) { roots[rIdx] = res; break; }
            }
        }
    }

    // 重新選取處理後的物件
    try {
        doc.selection = null;
        for (var s = 0; s < roots.length; s++) {
            try { roots[s].selected = true; } catch (e) {}
        }
    } catch (e) {}

    // ============================================================
    // 結果回報
    // ============================================================
    var msg = "完成：處理了 " + done + " 條路徑。\n格子 " + round2(effectiveCell) +
              " pt，取樣間距 " + round2(spacing) + " pt。";
    var notes = [];
    if (stats.degenerate) notes.push("• " + stats.degenerate + " 條路徑吸附後點數不足，已略過（格子相對物件太大）");
    if (stats.collapsed)  notes.push("• " + stats.collapsed + " 條路徑因格子過大，已整個簡化成單一方塊（極致像素化）");
    if (stats.capped)     notes.push("• " + stats.capped + " 條路徑達到取樣點上限，結果可能不夠細");
    if (stats.text)       notes.push("• " + stats.text + " 個文字物件未處理（需建立外框）");
    if (stats.plugin)     notes.push("• " + stats.plugin + " 個即時效果／封套物件未處理（需擴充外觀）");
    if (stats.locked)     notes.push("• " + stats.locked + " 個鎖定或隱藏的物件已略過");
    if (stats.clip)       notes.push("• " + stats.clip + " 條剪裁路徑已略過");
    if (stats.guide)      notes.push("• " + stats.guide + " 條參考線已略過");
    if (stats.openSkipped) notes.push("• " + stats.openSkipped + " 條開放路徑不支援完全像素化，已略過（可改用格點吸附模式）");
    if (stats.toobig)      notes.push("• " + stats.toobig + " 條路徑的格子數過多（格子相對物件太小），已略過");
    if (notes.length) msg += "\n\n" + notes.join("\n");
    alert(msg);


    // ============================================================
    // ---- 以下為函式定義 ----
    // ============================================================

    // ------------------------------------------------------------
    // 對話框（含即時預覽）
    //
    // 原理：對話框開啟時先把使用者原本選取的物件隱藏起來，
    // 每次滑桿數值變動（debounce 過後）就刪掉上一份預覽複本、
    // 重新複製一份選取物件、套用目前的參數並顯示出來，
    // 讓畫布上看到的就是「這組參數處理完會長怎樣」。
    // 按「執行」會清掉預覽複本、還原原件可見度，
    // 之後交給下面原本那一整套（含保留原件）流程正式處理；
    // 按「取消」則直接清掉預覽、還原原件，不做任何改動。
    // ------------------------------------------------------------
    function runDialogWithLivePreview(items) {
        var previewItems = null;   // 目前顯示中的預覽複本（頂層物件陣列）
        var pendingTaskId = null;
        var hidden = [];           // 記錄哪些原始物件被我們暫時隱藏了

        // 固定 pt 模式的滑桿上限不能寫死：物件本身可能就有好幾百 pt 大，
        // 上限太小會導致怎麼拖都拖不到「整個物件變一個大方塊」。
        // 這裡依選取物件實際的外接框長邊，動態算一個夠大的上限。
        var seedBounds = unionBounds(items);
        var seedLongSide = Math.max(seedBounds[2] - seedBounds[0], seedBounds[1] - seedBounds[3]);
        if (!isFinite(seedLongSide) || seedLongSide <= 0) seedLongSide = 100;
        var cellSliderMax = Math.max(100, Math.ceil(seedLongSide * 1.5));

        function clearPreview() {
            if (previewItems) {
                for (var i = 0; i < previewItems.length; i++) {
                    try { previewItems[i].remove(); } catch (e) {}
                }
                previewItems = null;
            }
        }

        function hideOriginals() {
            for (var i = 0; i < items.length; i++) {
                try {
                    if (!items[i].hidden) {
                        items[i].hidden = true;
                        hidden.push(items[i]);
                    }
                } catch (e) {}
            }
        }

        function restoreOriginals() {
            for (var i = 0; i < hidden.length; i++) {
                try { hidden[i].hidden = false; } catch (e) {}
            }
            hidden = [];
        }

        // ------------------------------------------------------------
        // 視窗
        // ------------------------------------------------------------
        var w = new Window("dialog", "Kaku");
        w.orientation = "column";
        w.alignChildren = ["fill", "top"];
        w.margins = 16;
        w.spacing = 10;

        var pSize = w.add("panel", undefined, "格子大小依據");
        pSize.orientation = "column";
        pSize.alignChildren = ["left", "top"];
        pSize.margins = 12;
        var sizeFixed = pSize.add("radiobutton", undefined, "固定 pt 數值");
        var sizeRatio = pSize.add("radiobutton", undefined, "依選取範圍等比例（跨向格數，可做到極致像素化）");
        sizeFixed.value = true;

        // 格子大小：滑桿 + 數字（固定 pt 模式）
        var g1 = w.add("group");
        g1.alignChildren = ["left", "center"];
        g1.add("statictext", undefined, "格子大小：").preferredSize.width = 90;
        var cellSlider = g1.add("slider", undefined, 8, 0.5, cellSliderMax);
        cellSlider.preferredSize.width = 180;
        var cellIn = g1.add("edittext", undefined, "8");
        cellIn.characters = 5;
        g1.add("statictext", undefined, "pt");

        // 跨向格數（等比例模式）：沿選取範圍長邊切幾格
        var g1b = w.add("group");
        g1b.alignChildren = ["left", "center"];
        g1b.add("statictext", undefined, "跨向格數：").preferredSize.width = 90;
        var ratioSlider = g1b.add("slider", undefined, 8, 1, 200);
        ratioSlider.preferredSize.width = 180;
        var ratioIn = g1b.add("edittext", undefined, "8");
        ratioIn.characters = 5;
        g1b.add("statictext", undefined, "（數字越小、方塊越大越極致）");

        // 每格取樣點數：滑桿 + 數字
        var g2 = w.add("group");
        g2.alignChildren = ["left", "center"];
        g2.add("statictext", undefined, "取樣密度：").preferredSize.width = 90;
        var densSlider = g2.add("slider", undefined, 3, 1, 20);
        densSlider.preferredSize.width = 180;
        var densIn = g2.add("edittext", undefined, "3");
        densIn.characters = 5;
        g2.add("statictext", undefined, "（越大越貼合，越慢）");

        var pm = w.add("panel", undefined, "像素化模式");
        pm.orientation = "column";
        pm.alignChildren = ["left", "top"];
        pm.margins = 12;
        var mOutline = pm.add("radiobutton", undefined, "格點吸附（保留角度，貼合原輪廓）");
        var mBlock = pm.add("radiobutton", undefined, "完全像素化（整格填滿，直角方塊）");
        mOutline.value = true;

        var p1 = w.add("panel", undefined, "格點原點");
        p1.orientation = "column";
        p1.alignChildren = ["left", "top"];
        p1.margins = 12;
        var rDoc = p1.add("radiobutton", undefined, "文件原點 (0, 0)");
        var rSel = p1.add("radiobutton", undefined, "選取範圍左上角");
        rSel.value = true;

        var p2 = w.add("panel", undefined, "選項");
        p2.orientation = "column";
        p2.alignChildren = ["left", "top"];
        p2.margins = 12;
        var cSimp = p2.add("checkbox", undefined, "移除共線的多餘錨點");
        cSimp.value = true;
        var cKeep = p2.add("checkbox", undefined, "保留原件（處理複本）");
        cKeep.value = false;
        var cText = p2.add("checkbox", undefined, "文字自動建立外框");
        cText.value = false;
        var cClip = p2.add("checkbox", undefined, "一併處理剪裁路徑");
        cClip.value = false;

        var p3 = w.add("panel", undefined, "預覽");
        p3.orientation = "row";
        p3.alignChildren = ["left", "center"];
        p3.margins = 12;
        var cLive = p3.add("checkbox", undefined, "即時預覽");
        cLive.value = true;
        var bRefresh = p3.add("button", undefined, "重新整理預覽");

        var statusTxt = w.add("statictext", undefined, "");
        statusTxt.preferredSize.width = 320;

        var gb = w.add("group");
        gb.alignment = "right";
        gb.add("button", undefined, "取消", { name: "cancel" });
        gb.add("button", undefined, "執行", { name: "ok" });

        // ------------------------------------------------------------
        // 讀取目前 UI 數值成 cfg（不做嚴格驗證，供預覽用；
        // 正式送出前 finalizeCfg() 才會做完整檢查）
        // ------------------------------------------------------------
        function readCfg() {
            var cell = parseFloat(cellIn.text);
            var dens = parseFloat(densIn.text);
            var ratio = parseFloat(ratioIn.text);
            if (isNaN(cell) || cell <= 0) cell = 0.5;
            if (isNaN(dens) || dens < 1) dens = 1;
            if (dens > 20) dens = 20;
            if (isNaN(ratio) || ratio < 1) ratio = 1;
            if (ratio > 200) ratio = 200;
            return {
                cell: cell,
                density: dens,
                cellSizeMode: sizeRatio.value ? "ratio" : "fixed",
                cellRatio: ratio,
                mode: mBlock.value ? "block" : "outline",
                originDoc: rDoc.value,
                simplify: cSimp.value,
                keepOriginal: cKeep.value,
                outlineText: cText.value,
                includeClipping: cClip.value
            };
        }

        function syncCellFromSlider() {
            cellIn.text = String(round2(cellSlider.value));
        }
        function syncCellFromText() {
            var v = parseFloat(cellIn.text);
            if (isNaN(v) || v <= 0) return;
            // 文字欄位本身不受滑桿上限限制（可以打比滑桿上限更大的數字），
            // 這裡只是把滑桿的顯示位置盡量對齊，超出範圍就停在滑桿的兩端。
            var clamped = v; if (clamped < 0.5) clamped = 0.5; if (clamped > cellSliderMax) clamped = cellSliderMax;
            cellSlider.value = clamped;
        }
        function syncDensFromSlider() {
            densIn.text = String(Math.round(densSlider.value));
        }
        function syncDensFromText() {
            var v = parseFloat(densIn.text);
            if (isNaN(v)) return;
            var clamped = v; if (clamped < 1) clamped = 1; if (clamped > 20) clamped = 20;
            densSlider.value = clamped;
        }
        function syncRatioFromSlider() {
            ratioIn.text = String(Math.round(ratioSlider.value));
        }
        function syncRatioFromText() {
            var v = parseFloat(ratioIn.text);
            if (isNaN(v)) return;
            var clamped = v; if (clamped < 1) clamped = 1; if (clamped > 200) clamped = 200;
            ratioSlider.value = clamped;
        }
        function updateSizeModeEnabled() {
            var fixed = sizeFixed.value;
            cellSlider.enabled = fixed;
            cellIn.enabled = fixed;
            ratioSlider.enabled = !fixed;
            ratioIn.enabled = !fixed;
        }

        // ------------------------------------------------------------
        // 即時預覽核心：重建一份處理過的複本並顯示
        // ------------------------------------------------------------
        function updatePreviewNow() {
            pendingTaskId = null;
            clearPreview();

            var cfgP = readCfg();

            var dup = [];
            for (var i = 0; i < items.length; i++) {
                try { dup.push(items[i].duplicate()); } catch (e) {}
            }
            for (var i2 = 0; i2 < dup.length; i2++) {
                try { dup[i2].hidden = false; } catch (e) {}
            }

            var pStats = { text: 0, plugin: 0, locked: 0, guide: 0, clip: 0, other: 0, degenerate: 0, capped: 0, openSkipped: 0, toobig: 0, collapsed: 0 };
            var pTargets = [];
            var pTexts = [];
            collect(dup, pTargets, pTexts, pStats, cfgP);

            if (cfgP.outlineText && pTexts.length > 0) {
                var pNewGroups = [];
                for (var t = 0; t < pTexts.length; t++) {
                    try { pNewGroups.push(pTexts[t].createOutline()); } catch (e) {}
                }
                collect(pNewGroups, pTargets, [], pStats, cfgP);
                for (var g = 0; g < pNewGroups.length; g++) dup.push(pNewGroups[g]);
            }

            var doneP = 0;
            if (pTargets.length > 0) {
                var ub = unionBounds(pTargets);
                var ox = 0, oy = 0;
                if (!cfgP.originDoc) { ox = ub[0]; oy = ub[1]; }

                var effectiveCellP = deriveEffectiveCell(cfgP, ub);
                if (cfgP.cellSizeMode === "ratio") {
                    // 等比例模式下，把換算出來的 pt 值同步顯示在固定大小欄位
                    // 給使用者參考（欄位本身被停用，不影響輸入）。
                    cellIn.text = String(round2(effectiveCellP));
                    try {
                        var vv = effectiveCellP; if (vv < 0.5) vv = 0.5; if (vv > cellSliderMax) vv = cellSliderMax;
                        cellSlider.value = vv;
                    } catch (e) {}
                }

                var runCfgP = {};
                for (var ck2 in cfgP) { if (cfgP.hasOwnProperty(ck2)) runCfgP[ck2] = cfgP[ck2]; }
                runCfgP.cell = effectiveCellP;

                var spacing = Math.max(0.2, runCfgP.cell / runCfgP.density);
                for (var k = 0; k < pTargets.length; k++) {
                    var resP = processPath(pTargets[k], runCfgP, spacing, ox, oy, pStats);
                    if (!resP) continue;
                    doneP++;
                    if (resP !== true) {
                        for (var dIdx = 0; dIdx < dup.length; dIdx++) {
                            if (dup[dIdx] === pTargets[k]) { dup[dIdx] = resP; break; }
                        }
                    }
                }
                var skippedNote = (pStats.openSkipped || pStats.toobig)
                    ? "（略過 " + (pStats.openSkipped + pStats.toobig) + " 條）" : "";
                statusTxt.text = "預覽：" + doneP + " 條路徑・格子 " + round2(effectiveCellP) +
                                  " pt・間距 " + round2(spacing) + " pt" + skippedNote;
            } else {
                statusTxt.text = "預覽：沒有可處理的路徑（文字／即時效果需先轉外框或擴充外觀）";
            }

            previewItems = dup;
            app.redraw();
        }

        // 有 app.scheduleTask 就 debounce，避免拖曳滑桿時瘋狂重算；
        // 沒有的話就退回同步即時更新。
        var canSchedule = true;
        try { app.scheduleTask("", 0, false); } catch (e) { canSchedule = false; }

        function scheduleUpdate() {
            if (!cLive.value) return;
            if (!canSchedule) { updatePreviewNow(); return; }
            if (pendingTaskId !== null) {
                try { app.cancelTask(pendingTaskId); } catch (e) {}
                pendingTaskId = null;
            }
            $.global.__gridQuantizePreviewTick = updatePreviewNow;
            try {
                pendingTaskId = app.scheduleTask("$.global.__gridQuantizePreviewTick();", 150, false);
            } catch (e) {
                updatePreviewNow();
            }
        }

        // ------------------------------------------------------------
        // 事件綁定
        // ------------------------------------------------------------
        cellSlider.onChanging = function () { syncCellFromSlider(); scheduleUpdate(); };
        cellIn.onChange = function () { syncCellFromText(); scheduleUpdate(); };
        densSlider.onChanging = function () { syncDensFromSlider(); scheduleUpdate(); };
        densIn.onChange = function () { syncDensFromText(); scheduleUpdate(); };
        ratioSlider.onChanging = function () { syncRatioFromSlider(); scheduleUpdate(); };
        ratioIn.onChange = function () { syncRatioFromText(); scheduleUpdate(); };
        sizeFixed.onClick = function () { updateSizeModeEnabled(); scheduleUpdate(); };
        sizeRatio.onClick = function () { updateSizeModeEnabled(); scheduleUpdate(); };
        mOutline.onClick = scheduleUpdate;
        mBlock.onClick = scheduleUpdate;
        rDoc.onClick = scheduleUpdate;
        rSel.onClick = scheduleUpdate;
        cSimp.onClick = scheduleUpdate;
        cKeep.onClick = scheduleUpdate;
        cText.onClick = scheduleUpdate;
        cClip.onClick = scheduleUpdate;
        bRefresh.onClick = updatePreviewNow;
        cLive.onClick = function () {
            if (cLive.value) updatePreviewNow();
        };

        updateSizeModeEnabled();
        hideOriginals();
        app.redraw();

        w.onShow = function () {
            updatePreviewNow();
        };

        var result = w.show();

        // 不論結果為何，先把懸而未決的預覽工作收尾乾淨
        if (pendingTaskId !== null) {
            try { app.cancelTask(pendingTaskId); } catch (e) {}
            pendingTaskId = null;
        }
        try { delete $.global.__gridQuantizePreviewTick; } catch (e) {}
        clearPreview();
        restoreOriginals();
        app.redraw();

        if (result !== 1) return null;

        var finalCfg = readCfg();
        if (isNaN(finalCfg.cell) || finalCfg.cell <= 0) {
            alert("格子大小必須是大於 0 的數字。");
            return null;
        }
        return finalCfg;
    }

    // ------------------------------------------------------------
    // 遞迴蒐集：群組、複合路徑都往下找
    // ------------------------------------------------------------
    function collect(items, out, textsOut, st, conf) {
        for (var i = 0; i < items.length; i++) {
            var it;
            try { it = items[i]; } catch (e) { continue; }

            var tn;
            try { tn = it.typename; } catch (e) { continue; }   // 已失效的參照

            try {
                if (it.locked || it.hidden) { st.locked++; continue; }
            } catch (e) {}

            switch (tn) {
                case "PathItem":
                    try {
                        if (it.guides) { st.guide++; break; }
                        if (it.clipping && !conf.includeClipping) { st.clip++; break; }
                    } catch (e) {}
                    out.push(it);
                    break;

                case "CompoundPathItem":
                    try { collect(it.pathItems, out, textsOut, st, conf); } catch (e) {}
                    break;

                case "GroupItem":
                    try { collect(it.pageItems, out, textsOut, st, conf); } catch (e) {}
                    break;

                case "TextFrame":
                    if (textsOut) textsOut.push(it);
                    st.text++;
                    break;

                case "PluginItem":
                    st.plugin++;
                    break;

                default:
                    st.other++;
            }
        }
    }

    // ------------------------------------------------------------
    // 單條路徑處理：依模式分派
    // 回傳值：false = 略過／失敗；true = 原地處理完成；
    //        其他（PageItem）= 原路徑被換成新物件（完全像素化拆成
    //        多塊或鏤空時會改成複合路徑），呼叫端要用這個新物件
    //        取代原本的參照。
    // ------------------------------------------------------------
    function processPath(p, conf, spacing, gx, gy, st) {
        if (conf.mode === "block") return processPathBlock(p, conf, spacing, gx, gy, st);
        return processPathOutline(p, conf, spacing, gx, gy, st);
    }

    // ------------------------------------------------------------
    // 模式一：格點吸附（保留角度，鋸齒貼合原輪廓）
    // ------------------------------------------------------------
    function processPathOutline(p, conf, spacing, gx, gy, st) {
        var closed;
        try { closed = p.closed; } catch (e) { return false; }

        var raw = resample(p, spacing, 15000, st);
        if (!raw || raw.length < 2) return false;

        var pts = snapAll(raw, conf.cell, gx, gy);

        // 閉合路徑：頭尾若吸到同一格，去掉尾巴
        if (closed && pts.length > 1 &&
            pts[0][0] === pts[pts.length - 1][0] &&
            pts[0][1] === pts[pts.length - 1][1]) {
            pts.pop();
        }

        if (conf.simplify) pts = simplifyCollinear(pts, closed);

        if (pts.length < 2 || (closed && pts.length < 3)) {
            // 格子相對物件太大，一般吸附會塌陷成點／線。封閉路徑就退而求其次，
            // 直接用涵蓋整個物件的那一顆（或幾顆）格子畫出一個方塊，
            // 而不是什麼都不做──這才是把格子推到極致時該有的樣子。
            if (closed) {
                var rectPts = boundingBoxCells(raw, conf.cell, gx, gy);
                if (rectPts) {
                    try {
                        p.setEntirePath(rectPts);
                        p.closed = true;
                        st.collapsed++;
                        return true;
                    } catch (e) {}
                }
            }
            st.degenerate++;
            return false;
        }

        try {
            p.setEntirePath(pts);
            p.closed = closed;
        } catch (e) {
            st.degenerate++;
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------
    // 模式二：完全像素化（整格填滿、只有直角）
    //
    // 作法：把路徑取樣成細折線多邊形，用掃描線判斷每個格子「中心點」
    // 是否落在多邊形內，得到一張填滿／不填滿的格子網格；再沿著填滿
    // 格子的邊界描邊（只在鄰格「沒填滿」的那一側畫線），得到一或多圈
    // 直角輪廓。只支援封閉路徑。
    // 若結果不只一圈（例如細長物件被拆成好幾塊、或圍出鏤空），
    // 就把原路徑換成一個複合路徑，把每一圈各自放進一條子路徑。
    // ------------------------------------------------------------
    function processPathBlock(p, conf, spacing, gx, gy, st) {
        var closed;
        try { closed = p.closed; } catch (e) { return false; }
        if (!closed) { st.openSkipped++; return false; }

        var raw = resample(p, spacing, 15000, st);
        if (!raw || raw.length < 3) { st.degenerate++; return false; }

        var cell = conf.cell;
        var minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
        for (var i = 0; i < raw.length; i++) {
            if (raw[i][0] < minX) minX = raw[i][0];
            if (raw[i][0] > maxX) maxX = raw[i][0];
            if (raw[i][1] < minY) minY = raw[i][1];
            if (raw[i][1] > maxY) maxY = raw[i][1];
        }

        // 格子索引：col 沿 x 從 gx 起算，row 沿 y 從 gy（頂端原點）往下算
        var colMin = Math.floor((minX - gx) / cell);
        var colMax = Math.ceil((maxX - gx) / cell) - 1;
        var rowMin = Math.floor((gy - maxY) / cell);
        var rowMax = Math.ceil((gy - minY) / cell) - 1;
        if (colMax < colMin) colMax = colMin;
        if (rowMax < rowMin) rowMax = rowMin;

        var nCols = colMax - colMin + 1;
        var nRows = rowMax - rowMin + 1;
        if (nCols < 1 || nRows < 1 || nCols * nRows > 2000000) {
            st.toobig++;
            return false;
        }

        // 掃描線：用「格子是否被多邊形碰到」而不是「格子中心點是否在內」，
        // 而且每一列再切 3 條子掃描線。純中心點取樣在物件是細長／彎曲筆畫、
        // 格子跟筆畫寬度差不多大時很容易完全找不到任何命中的格子中心
        // （結果「處理了 0 條路徑」，看起來像壞掉了），改成只要格子被
        // 多邊形碰到一點就算填滿，才不會漏掉細筆畫。
        var n = raw.length;
        var grid = [];
        var anyFilled = false;
        var SUBSCAN = 3;
        for (var rr = 0; rr < nRows; rr++) {
            var row = new Array(nCols);
            for (var z = 0; z < nCols; z++) row[z] = false;
            grid[rr] = row;

            for (var sub = 0; sub < SUBSCAN; sub++) {
                var frac = (sub + 0.5) / SUBSCAN;
                var yTest = gy - (rowMin + rr + frac) * cell;
                var xs = [];
                for (var e = 0; e < n; e++) {
                    var A = raw[e], B = raw[(e + 1) % n];
                    var ay = A[1], by = B[1];
                    if (ay === by) continue;
                    if ((yTest >= ay && yTest < by) || (yTest >= by && yTest < ay)) {
                        var t = (yTest - ay) / (by - ay);
                        xs.push(A[0] + t * (B[0] - A[0]));
                    }
                }
                if (xs.length < 2) continue;
                xs.sort(function (a, b) { return a - b; });

                for (var pi = 0; pi + 1 < xs.length; pi += 2) {
                    var cFrom = Math.floor((xs[pi] - gx) / cell) - colMin;
                    var cTo = Math.ceil((xs[pi + 1] - gx) / cell) - 1 - colMin;
                    if (cFrom < 0) cFrom = 0;
                    if (cTo > nCols - 1) cTo = nCols - 1;
                    for (var cc = cFrom; cc <= cTo; cc++) { row[cc] = true; anyFilled = true; }
                }
            }
        }

        if (!anyFilled) {
            // 整個物件都塞在一兩顆格子裡、剛好沒有格子中心點落在形狀內：
            // 格數很少的話乾脆整顆格子都算填滿，做到極致像素化而不是放棄。
            if (nCols * nRows <= 4) {
                for (var rr4 = 0; rr4 < nRows; rr4++) {
                    for (var cc4 = 0; cc4 < nCols; cc4++) grid[rr4][cc4] = true;
                }
                anyFilled = true;
                st.collapsed++;
            } else {
                st.degenerate++;
                return false;
            }
        }

        // 邊界描邊：填滿格子只在「鄰格沒填滿」的那一側畫邊，
        // 順著同一個方向繞，外圈與鏤空會自動呈現相反的走向。
        function filledAt(rr, cc) {
            if (rr < 0 || rr >= nRows || cc < 0 || cc >= nCols) return false;
            return grid[rr][cc];
        }
        var nextMap = {};
        for (var rr2 = 0; rr2 < nRows; rr2++) {
            for (var cc2 = 0; cc2 < nCols; cc2++) {
                if (!grid[rr2][cc2]) continue;
                var TL = cc2 + "_" + rr2, TR = (cc2 + 1) + "_" + rr2,
                    BR = (cc2 + 1) + "_" + (rr2 + 1), BL = cc2 + "_" + (rr2 + 1);
                if (!filledAt(rr2 - 1, cc2)) nextMap[TL] = TR;
                if (!filledAt(rr2, cc2 + 1)) nextMap[TR] = BR;
                if (!filledAt(rr2 + 1, cc2)) nextMap[BR] = BL;
                if (!filledAt(rr2, cc2 - 1)) nextMap[BL] = TL;
            }
        }

        function keyToPt(k) {
            var us = k.indexOf("_");
            var x = parseInt(k.substring(0, us), 10);
            var y = parseInt(k.substring(us + 1), 10);
            return [gx + (colMin + x) * cell, gy - (rowMin + y) * cell];
        }

        var contours = [];
        for (var startKey in nextMap) {
            if (!nextMap.hasOwnProperty(startKey)) continue;
            if (nextMap[startKey] === null) continue;
            var loop = [];
            var curKey = startKey;
            var guardCount = 0;
            var guardMax = (nCols + 1) * (nRows + 1) * 4 + 8;
            while (nextMap.hasOwnProperty(curKey) && nextMap[curKey] !== null && guardCount < guardMax) {
                loop.push(keyToPt(curKey));
                var nk = nextMap[curKey];
                nextMap[curKey] = null;
                curKey = nk;
                guardCount++;
                if (curKey === startKey) break;
            }
            if (loop.length >= 3) contours.push(loop);
        }

        if (contours.length === 0) { st.degenerate++; return false; }

        if (conf.simplify) {
            for (var ci = 0; ci < contours.length; ci++) {
                contours[ci] = simplifyCollinear(contours[ci], true);
            }
        }
        var kept = [];
        for (var ci2 = 0; ci2 < contours.length; ci2++) {
            if (contours[ci2].length >= 3) kept.push(contours[ci2]);
        }
        if (kept.length === 0) { st.degenerate++; return false; }

        if (kept.length === 1) {
            try {
                p.setEntirePath(kept[0]);
                p.closed = true;
            } catch (e) { st.degenerate++; return false; }
            return true;
        }

        // 多於一圈（拆成好幾塊或出現鏤空）：改用複合路徑承載每一圈
        var parent;
        try { parent = p.parent; } catch (e) { parent = doc; }
        var cp;
        try { cp = parent.compoundPathItems.add(); } catch (e) { st.degenerate++; return false; }
        try {
            cp.filled = p.filled;
            cp.fillColor = p.fillColor;
            cp.stroked = p.stroked;
            cp.strokeColor = p.strokeColor;
            cp.strokeWidth = p.strokeWidth;
            cp.opacity = p.opacity;
        } catch (e) {}
        try { cp.move(p, ElementPlacement.PLACEBEFORE); } catch (e) {}

        for (var kk = 0; kk < kept.length; kk++) {
            try {
                var sub = cp.pathItems.add();
                sub.setEntirePath(kept[kk]);
                sub.closed = true;
            } catch (e) {}
        }
        try { p.remove(); } catch (e) {}
        return cp;
    }

    // ------------------------------------------------------------
    // 沿路徑均勻補點：把每段三次貝茲曲線取樣成折線
    // 這一步是關鍵 —— 直接吸附原本的稀疏錨點只會讓曲線歪掉，
    // 不會變成階梯。
    // ------------------------------------------------------------
    function resample(p, spacing, maxPts, st) {
        var pp;
        try { pp = p.pathPoints; } catch (e) { return null; }
        var n = pp.length;
        if (n < 2) return null;

        var closed = p.closed;
        var last = closed ? n : n - 1;
        var out = [];
        var hitCap = false;

        for (var i = 0; i < last; i++) {
            var a = pp[i];
            var b = pp[(i + 1) % n];
            var p0 = a.anchor, p1 = a.rightDirection;
            var p2 = b.leftDirection, p3 = b.anchor;

            var len = segLength(p0, p1, p2, p3);
            var steps = Math.ceil(len / spacing);
            if (steps < 1) steps = 1;
            if (steps > 600) { steps = 600; hitCap = true; }

            for (var s = 0; s < steps; s++) {
                out.push(bezierAt(p0, p1, p2, p3, s / steps));
            }

            if (out.length > maxPts) { hitCap = true; break; }
        }

        if (!closed) out.push([pp[n - 1].anchor[0], pp[n - 1].anchor[1]]);
        if (hitCap) st.capped++;
        return out;
    }

    function bezierAt(p0, p1, p2, p3, t) {
        var mt = 1 - t;
        var a = mt * mt * mt;
        var b = 3 * mt * mt * t;
        var c = 3 * mt * t * t;
        var d = t * t * t;
        return [
            a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
            a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]
        ];
    }

    function segLength(p0, p1, p2, p3) {
        var n = 12, len = 0, prev = p0, cur;
        for (var i = 1; i <= n; i++) {
            cur = bezierAt(p0, p1, p2, p3, i / n);
            len += Math.sqrt((cur[0] - prev[0]) * (cur[0] - prev[0]) +
                             (cur[1] - prev[1]) * (cur[1] - prev[1]));
            prev = cur;
        }
        return len;
    }

    // ------------------------------------------------------------
    // 吸附到格點，順手去掉連續重複點
    // ------------------------------------------------------------
    function snapAll(pts, cell, gx, gy) {
        var out = [];
        for (var i = 0; i < pts.length; i++) {
            var x = Math.round((pts[i][0] - gx) / cell) * cell + gx;
            var y = Math.round((pts[i][1] - gy) / cell) * cell + gy;
            if (out.length === 0 ||
                out[out.length - 1][0] !== x ||
                out[out.length - 1][1] !== y) {
                out.push([x, y]);
            }
        }
        return out;
    }

    // ------------------------------------------------------------
    // 移除共線中間點（吸附後會產生大量落在同一直線上的錨點）
    // ------------------------------------------------------------
    function simplifyCollinear(pts, closed) {
        var n = pts.length;
        if (n < 3) return pts;
        var out = [];
        for (var i = 0; i < n; i++) {
            if (!closed && (i === 0 || i === n - 1)) { out.push(pts[i]); continue; }
            var pv = pts[(i - 1 + n) % n];
            var cu = pts[i];
            var nx = pts[(i + 1) % n];
            var cross = (cu[0] - pv[0]) * (nx[1] - pv[1]) - (cu[1] - pv[1]) * (nx[0] - pv[0]);
            if (Math.abs(cross) > 1e-6) out.push(cu);
        }
        return (out.length >= (closed ? 3 : 2)) ? out : pts;
    }

    // ------------------------------------------------------------
    // 把一堆點的外接框，展開到最近的整數格邊界，回傳一個矩形
    // （4 個點，順時針），至少涵蓋一顆格子。用在格點吸附模式的
    // 極端塌陷退場機制：格子太大時，整個物件就變成一塊方塊。
    // ------------------------------------------------------------
    function boundingBoxCells(pts, cell, gx, gy) {
        var minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
        for (var i = 0; i < pts.length; i++) {
            if (pts[i][0] < minX) minX = pts[i][0];
            if (pts[i][0] > maxX) maxX = pts[i][0];
            if (pts[i][1] < minY) minY = pts[i][1];
            if (pts[i][1] > maxY) maxY = pts[i][1];
        }
        if (!isFinite(minX) || !isFinite(minY)) return null;

        var c0 = Math.floor((minX - gx) / cell);
        var c1 = Math.ceil((maxX - gx) / cell);
        var r0 = Math.floor((gy - maxY) / cell);
        var r1 = Math.ceil((gy - minY) / cell);
        if (c1 <= c0) c1 = c0 + 1;
        if (r1 <= r0) r1 = r0 + 1;

        var x0 = gx + c0 * cell, x1 = gx + c1 * cell;
        var y0 = gy - r0 * cell, y1 = gy - r1 * cell;
        return [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
    }

    // ------------------------------------------------------------
    // 依「格子大小依據」把 cfg 換算成實際要用的格子 pt 值。
    // 固定模式直接用 cfg.cell；等比例模式則用整體選取範圍
    // （ub = unionBounds 結果）的長邊除以「跨向格數」。
    // ------------------------------------------------------------
    function deriveEffectiveCell(cfg, ub) {
        var cell = cfg.cell;
        if (cfg.cellSizeMode === "ratio") {
            var w = ub[2] - ub[0];
            var h = ub[1] - ub[3];
            var longSide = Math.max(w, h);
            if (isFinite(longSide) && longSide > 0 && cfg.cellRatio > 0) {
                cell = longSide / cfg.cellRatio;
            }
        }
        if (!isFinite(cell) || cell <= 0) cell = 1;
        return cell;
    }

    // ------------------------------------------------------------
    function unionBounds(items) {
        var L = Infinity, T = -Infinity, R = -Infinity, B = Infinity;
        for (var i = 0; i < items.length; i++) {
            var gb;
            try { gb = items[i].geometricBounds; } catch (e) { continue; }
            if (gb[0] < L) L = gb[0];
            if (gb[1] > T) T = gb[1];
            if (gb[2] > R) R = gb[2];
            if (gb[3] < B) B = gb[3];
        }
        if (!isFinite(L)) { L = 0; T = 0; R = 0; B = 0; }
        return [L, T, R, B];
    }

    function round2(v) { return Math.round(v * 100) / 100; }

})();

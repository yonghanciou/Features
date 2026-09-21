# Kaku

Illustrator 的像素化特效外掛，把路徑對齊網格量化成方塊像素風格：可以整格填滿成直角色塊，也可以只是貼齊原輪廓的邊緣吸附。

[![下載 Kaku.aip](https://img.shields.io/badge/下載-Kaku.aip-1473E6?style=for-the-badge)](https://github.com/yonghanciou/Features/releases/tag/v1.0.0-kaku)

![Kaku-cover](assets/kaku-v1.0.png)

## 支援版本

- 作業系統：macOS（核心開發平台，未測試 Windows）
- 軟體：Adobe Illustrator（以 Illustrator 2026 SDK 開發、實測）

## 像素化邏輯

兩種模式：

- **像素化**（預設）：把路徑取樣後整格填滿，只有直角邊，複合路徑的鏤空也會正確保留。
- **邊緣吸附**：把取樣點吸附到最近的格點，邊緣會帶斜角，貼合原輪廓的走向，不像像素化只有直角。

## 安裝方式

![Kaku](assets/dialog.png)

到 [Releases](https://github.com/yonghanciou/Features/releases/tag/v1.0.0-kaku) 下載 `Kaku-vX.X.X-macOS.zip`：

1. 解壓縮得到 `Kaku.aip`（**不要雙擊它**——它不是應用程式，雙擊只會跳出「無法打開」，雙擊之後按「強制打開」也不會有反應，是正常的，直接跳下一步），移到 `/Applications/Adobe Illustrator [版本]/Plug-ins.localized/`。
2. 目前是 ad-hoc 簽章（不是正式 Apple Developer ID），重新啟動 Illustrator 時 macOS 可能會擋下這個外掛。如果「效果」選單裡沒有出現「SFF Features」，擇一處理：
   - **系統設定**：打開「系統設定 > 隱私權與安全性」，捲到最下面應該會看到「已阻擋『Kaku.aip』以保護你的 Mac」，按「強制打開」，跳出的確認視窗再按一次「打開」。
   - **終端機**（一次到位，不會再跳警告）：
     ```bash
     xattr -d com.apple.quarantine "/Applications/Adobe Illustrator [版本]/Plug-ins.localized/Kaku.aip"
     ```
3. 完全結束 Illustrator（⌘Q）再重新打開，「效果 > SFF Features > Kaku...」應該就會出現。

**自己編譯**：這個資料夾放的是外掛原始碼（`src/`），需要搭配 Adobe Illustrator C++ Plug-in SDK 自行編譯成 `.aip`。

**不想裝外掛**：`src/Kaku.jsx` 是免安裝的獨立版本：Illustrator 的「檔案 > 指令碼 > Kaku.jsx」就能直接對選取路徑套用像素化（是破壞性的，不會出現在外觀面板裡回頭調整，也還沒有原生版後來補上的複合路徑鏤空/斜角修正，只是輕量替代方案）。

## 使用方法

1. 選取一個路徑物件（單一路徑、群組、複合路徑都可以）。
2. 執行「效果 > SFF Features > Kaku...」。
3. 在跳出的對話框裡調整：
   - **模式**：像素化 / 邊緣吸附。
   - **格數**：滑桿 + 數字欄位，沿選取範圍長邊要切成幾格。
   - **取樣密度**：1–20，愈大取樣愈細、愈貼合原本曲線，但也愈慢。
   - **預覽**：勾選後拖曳滑桿畫布會即時更新。
4. 按「確定」套用。效果會留在「外觀」面板裡，之後可以隨時雙擊再打開這個對話框重新調整參數。

![套用前後比較](assets/before-after.png)

![模式切換效果示範](assets/mode-demo.gif)

## 已知問題

- 只在 macOS 上開發與測試，尚未驗證 Windows。
- 像素化模式只支援封閉路徑，開放路徑（單純線條）會被略過不處理。
- 格子數換算後如果超過約 200 萬格（格子相對物件極小），該條路徑會被跳過以避免卡死。
- 目前只在 Illustrator 2026 上實際測試過，其他版本理論相容但未逐一驗證。
- 獨立 jsx 版本（`src/Kaku.jsx`）是較早的版本，沒有原生版後來補上的複合路徑鏤空正確處理、鋸齒角落斜切修正、直線邊緣浮點誤差修正，像素化模式在特定情況下可能出現原生版已修好的瑕疵。

## 更新紀錄

- v1.0 — 初版：像素化（整格填滿，正確保留複合路徑鏤空）與邊緣吸附兩種模式、格數等比例縮放、原生 Cocoa 對話框（即時預覽）、浮點誤差修正（避免直線邊緣冒出雜點）、對角相接修正（避免鋸齒角落出現斜切）。

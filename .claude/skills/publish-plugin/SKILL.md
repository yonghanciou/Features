---
name: publish-plugin
description: 在 Features repo 新增或修改外掛並要推上 GitHub 前使用。一律走「分支 + PR + Squash and Merge」流程,絕不直接 push 或 force push 到 main,避免覆蓋掉別人(或自己在別處)已經上傳的內容。
---

# 發布外掛到 Features Repo

對 `Features` repo 做任何「新增外掛」或「修改既有外掛」並準備推上 GitHub 之前,一律照以下步驟做。**絕對不要直接對 main 做 commit、push 或 force push。**

## 步驟

1. **同步遠端狀態**,避免用過時內容覆蓋掉別人(或自己在別的地方,例如 GitHub 網頁)已經推上去的東西:
   ```bash
   git fetch origin
   git status
   ```
   如果本機 main 落後 origin/main,先處理同步(`git pull` 或告知使用者有衝突),不要略過這步。

2. **建立獨立分支**,不要在 main 上直接改:
   ```bash
   git checkout -b plugin/<外掛名稱>      # 新增外掛
   git checkout -b update/<外掛名稱>      # 修改既有外掛
   ```

3. **在這個分支上完成所有變更**:新增/修改檔案、寫 README(可參考 `PLUGIN_TEMPLATE.md`)、更新根目錄 `README.md` 的外掛列表。可以多次 commit 修正錯誤,這些過程不會留在 main 的歷史裡。

4. **推上這個分支**(不是 main):
   ```bash
   git push -u origin plugin/<外掛名稱>
   ```

5. **開一個 Pull Request** 到 main:
   ```bash
   gh pr create --title "新增外掛:<外掛名稱>" --body "簡述這個外掛做什麼、支援版本等"
   ```

6. **交給使用者確認**:PR 開好之後告訴使用者連結,請他自己到 GitHub 上檢查內容。確認沒問題後,由使用者(或經他同意)選 **Squash and Merge** 合併——這樣 main 上只會留一筆乾淨的紀錄。

7. 合併後清理分支:
   ```bash
   git branch -d plugin/<外掛名稱>
   git push origin --delete plugin/<外掛名稱>
   ```

## 禁止事項

- 不要對 `main` 執行 `git push -f` / `--force`。
- 不要在沒有先 `git fetch` 確認同步的情況下做任何覆蓋性操作(squash、rebase、`reset --hard` 後 push)。
- 不要跳過分支,直接把新外掛或修改內容 commit 到 main。
- 合併方式只用 GitHub 的 Squash and Merge,不要在本機手動 squash 歷史後 force push。

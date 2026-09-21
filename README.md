# Features

我開發的設計工具外掛合集,依軟體分類存放。每個外掛都是獨立的資料夾,裡面附上自己的說明文件。

> **開發環境:** 所有外掛皆以 **macOS** 為核心平台開發與測試,尚未驗證 Windows 相容性。
>
> **Powered by** Claude。

> **下載:** 每個外掛編譯好、可以直接安裝的版本都放在 **[Releases](https://github.com/yonghanciou/Features/releases)**,這個 repo 裡的資料夾放的是原始碼。

## 外掛列表

### Illustrator

| 外掛 | 簡述 | 下載 |
|---|---|---|
| [smoothie](illustrator/smoothie/) | 帶有曲率邏輯的圓角特效外掛 | [Releases](https://github.com/yonghanciou/Features/releases/tag/v1.0.0-smoothie) |
| [Zurich Seq](illustrator/zurich-seq/) | Swiss / 等分網格輔助線產生器,常用設定可存成預設 | [Releases](https://github.com/yonghanciou/Features/releases/tag/v1.0.0-zurich-seq) |
| [kaku](illustrator/kaku/) | 把路徑量化成方塊像素網格的外掛,可整格填色或只吸附邊緣 | [Releases](https://github.com/yonghanciou/Features/releases/tag/v1.0.0-kaku) |

### Glyphs

| 外掛 | 簡述 | 下載 |
|---|---|---|
| _(尚未上架,敬請期待)_ | | |

## 資料夾結構

每個外掛資料夾放的是**原始碼**,完整編譯好、可以直接安裝使用的版本請到 **[Releases](https://github.com/yonghanciou/Features/releases)** 下載。

```
Features/
├── illustrator/
│   └── <plugin-name>/
│       ├── README.md
│       └── src/...(原始碼)
└── glyphs/
    └── <plugin-name>/
        ├── README.md
        └── src/...(原始碼)
```

## 新增外掛的步驟

1. 在對應軟體資料夾(`illustrator/` 或 `glyphs/`)下新增一個資料夾,命名用小寫加連字號,例如 `auto-align`。
2. 把外掛的**原始碼**放進去,並複製 [PLUGIN_TEMPLATE.md](PLUGIN_TEMPLATE.md) 改名成 `README.md`,填寫說明。
3. 到 [Releases](https://github.com/yonghanciou/Features/releases) 上傳編譯好的版本,建立一個新 Release。
4. 回到這份根目錄 README,在上面對應的表格加一列連結(含下載連結)。

## 授權

本專案採用 [MIT License](LICENSE)。

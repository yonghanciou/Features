# Features

我開發的設計工具外掛合集,依軟體分類存放。每個外掛都是獨立的資料夾,裡面附上自己的說明文件。

> **開發環境:** 所有外掛皆以 **macOS** 為核心平台開發與測試,尚未驗證 Windows 相容性。
> > **Powered by** Claude。

## 外掛列表

### Illustrator

| 外掛 | 簡述 |
|---|---|
| _(尚未上架,敬請期待)_ | |

### Glyphs

| 外掛 | 簡述 |
|---|---|
| _(尚未上架,敬請期待)_ | |

## 資料夾結構

```
Features/
├── illustrator/
│   └── <plugin-name>/
│       ├── README.md
│       └── ...(外掛檔案)
└── glyphs/
    └── <plugin-name>/
        ├── README.md
        └── ...(外掛檔案)
```

## 新增外掛的步驟

1. 在對應軟體資料夾(`illustrator/` 或 `glyphs/`)下新增一個資料夾,命名用小寫加連字號,例如 `auto-align`。
2. 把外掛檔案放進去,並複製 [PLUGIN_TEMPLATE.md](PLUGIN_TEMPLATE.md) 改名成 `README.md`,填寫說明。
3. 回到這份根目錄 README,在上面對應的表格加一列連結。

## 授權

本專案採用 [MIT License](LICENSE)。

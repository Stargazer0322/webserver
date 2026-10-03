# Windows C++ Web Server

這是一個使用 C++ 與 Windows Winsock API 實作的簡易 HTTP/1.1 Web Server。伺服器監聽本機 `127.0.0.1:80`，並從 `www` 目錄提供網頁與檔案服務。

## 功能

- 使用 TCP socket 接收 HTTP/1.1 請求
- 支援 `GET`、`POST`、`HEAD`、`PUT`、`DELETE`
- 提供 `www` 目錄下的 HTML、CSS、圖片、PDF、影音與其他檔案
- 支援登入、Cookie 驗證與登出
- 支援檔案上傳、檔案清單、下載、HEAD 檔案資訊與刪除
- 支援 `Range`、`If-Range`、`If-None-Match`、`If-Modified-Since` 等快取或部分內容請求
- 支援大於 1 MB 請求的暫存檔處理
- 支援 HTTP keep-alive

## 專案結構

```text
.
├── src/
│   ├── web_server.cpp       # Web Server 主程式
│   ├── web_server.exe       # 已編譯的執行檔（若存在）
│   └── ...
├── www/
│   ├── index.html           # 登入頁面
│   ├── member.html          # 登入後的檔案管理頁面
│   ├── style.css
│   └── upload/              # 上傳檔案儲存目錄
├── test.http                # HTTP Client 測試請求
└── .vscode/                 # VS Code C/C++ 設定
```

## 環境需求

- Windows
- MinGW-w64 GCC，或其他支援 Winsock 的 C++ 編譯器
- C++17 或更新版本
- 編譯時需要連結 Windows Winsock library：`ws2_32`

## 編譯

在專案根目錄執行：

```powershell
g++ -std=c++17 -Wall -Wextra -g src\web_server.cpp -o src\web_server.exe -lws2_32
```

若使用 VS Code，可使用 `.vscode/tasks.json` 中的 C/C++ 建置工作，或直接在終端機執行上述命令。

## 啟動

由 `src` 目錄啟動程式，因為程式使用相對路徑 `../www` 尋找網頁檔案：

```powershell
cd src
.\web_server.exe
```

啟動成功後開啟：

<http://127.0.0.1/>

程式會佔用 TCP port `80`。如果 bind 失敗，請確認：

- port 80 沒有被 IIS、Apache 或其他服務使用
- 終端機具有監聽 port 80 所需的權限
- Windows 防火牆允許本機程式建立 socket

停止伺服器可在終端機按 `Ctrl+C`。

## 登入

首頁 `/` 會對應到 `www/index.html`。目前測試帳號直接寫在 `src/web_server.cpp`：

| 欄位 | 值 |
| --- | --- |
| 使用者名稱 | `123` |
| 密碼 | `12` |

登入成功後，伺服器會設定 `auth_token` Cookie，並導向 `/member.html`。`/member.html`、`/upload/` 與上傳目錄中的檔案都需要有效的 Cookie。

## HTTP API

### 取得公開資源

```http
GET /
GET /index.html
GET /style.css
```

### 登入與登出

登入表單會以 `POST` 傳送至 `/index.html`：

```powershell
curl.exe -i -c cookies.txt -X POST http://127.0.0.1/index.html `
  -H "Content-Type: application/x-www-form-urlencoded" `
  --data "username=123&password=12"
```

登出則對 `/member.html` 傳送：

```powershell
curl.exe -i -b cookies.txt -X POST http://127.0.0.1/member.html `
  -H "Content-Type: application/x-www-form-urlencoded" `
  --data "logout=1"
```

### 列出上傳檔案

```powershell
curl.exe -i -b cookies.txt http://127.0.0.1/upload/
```

### 上傳檔案

```powershell
curl.exe -i -b cookies.txt -X PUT http://127.0.0.1/upload/ `
  -F "file=@C:\path\to\example.txt"
```

### 取得檔案資訊

```powershell
curl.exe -I -b cookies.txt http://127.0.0.1/upload/example.txt
```

### 下載檔案

```powershell
curl.exe -i -b cookies.txt `
  http://127.0.0.1/upload/example.txt `
  -o example.txt
```

### 刪除檔案

```powershell
curl.exe -i -b cookies.txt -X DELETE `
  http://127.0.0.1/upload/example.txt
```

## 測試

`test.http` 包含基本的 HTTP 請求，可使用支援 REST Client 格式的 VS Code 擴充套件執行。

啟動伺服器後，也可以使用瀏覽器測試登入與檔案管理流程，或使用上方的 `curl.exe` 指令測試 API。

## 注意事項與限制

- 伺服器只綁定 `127.0.0.1`，預設只能從本機存取。
- 目前使用固定帳號密碼，僅適合課程、練習與本機測試，不適合直接部署到正式環境。
- Token 儲存在記憶體中，程式重新啟動後所有登入狀態會失效。
- 靜態檔案根目錄、監聽 IP 與 port 都直接寫在 `src/web_server.cpp`，目前沒有設定檔。
- 程式使用相對路徑存取 `www`；請務必從 `src` 目錄啟動執行檔。
- 目前沒有 TLS/HTTPS、正式的使用者資料庫、權限分級或完整的請求大小限制。
- `www/upload/` 內的檔案會被實際寫入、刪除，測試前請先備份重要資料。

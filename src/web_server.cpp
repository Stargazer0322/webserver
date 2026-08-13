#include <winsock2.h>
#include <iostream>
#include <string>
#include <string.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <filesystem>
#include <sys/stat.h>

using namespace std;

class HTTP {
    private:
        bool connect; //是否繼續連線
        string method; // GET POST等等的
        string file_path; // 要求的檔案位址
        string protocol; // HTTP1.1
        string file_content; // 檔案內容
        string file_type; // 檔案類型
        string status_code; // HTTP狀態碼
        string cookies_to_set; // 要設置的 cookies
        map<string, string> header_map; // 要設置的 headers
        map<string, string> request_cookies; // 從請求中讀取的 cookies
        map<string, string> user_tokens; // username -> token
        string content_range; // 回傳的部分內容範圍
        string request_body; // http 全部
        string headers; // http head
        string body; // http body
        string temp_file_path; // 大檔案暫存路徑
        bool rangerequestallowed; // 是否允許處理 Range 請求
        bool download; // 是否為下載檔案

        // URL 解碼
        string URLDecode (const string& text) {
            string decoded;
            decoded.reserve(text.length());

            for (size_t i = 0; i < text.length(); ++i) {
                if (text[i] == '%') {
                    if (i + 2 < text.length()) {
                        int value = 0;
                        istringstream is(text.substr(i + 1, 2));
                        if (is >> hex >> value) {
                            decoded += static_cast<char>(value);
                            i += 2;
                        } else {
                            decoded += '%';
                        }
                    } else {
                        decoded += '%';
                    }
                } else if (text[i] == '+') {
                    decoded += ' ';
                } else {
                    decoded += text[i];
                }
            }
            return decoded;
        }

        // UTF-8 轉 Big5
        string UTF8ToBig5 (const string& utf8Str) {
            string decoded = URLDecode(utf8Str);
            // UTF-8 轉 Unicode
            int wideSize = MultiByteToWideChar(CP_UTF8, 0,decoded.c_str(), -1, NULL, 0);
            if (wideSize == 0) return decoded;
            
            wchar_t* wideStr = new wchar_t[wideSize];
            MultiByteToWideChar(CP_UTF8, 0, decoded.c_str(), -1, wideStr, wideSize);
            
            // Unicode 轉 Big5
            int big5Size = WideCharToMultiByte(950, 0, wideStr, -1, NULL, 0, NULL, NULL);
            if (big5Size == 0) {
                delete[] wideStr;
                return decoded;
            }
            
            char* big5Str = new char[big5Size];
            WideCharToMultiByte(950, 0, wideStr, -1, big5Str, big5Size, NULL, NULL);
            
            string result(big5Str);
            delete[] wideStr;
            delete[] big5Str;
            return result;
        }

        //確認檔案是否存在
        bool File_exist (const string& filepath) {
            ifstream file(filepath);
            if (file.is_open()) {
                file.close();
                return true;
            }
            file.close();
            return false;
        }

        //讀取檔案
        string ReadFile () {
            string filepath = "../www" + file_path;
            ifstream file(filepath, ios::binary);
            if (!file.is_open()) {
                cout << "notfound" <<endl;
                Status_code(404);
                if (Get_file_type() == "text/html") {
                    return "<html><body><h1>Error 404: File not found!</h1></body></html>";
                } else {
                    return "";
                }
            }
            
            stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        //切割request
        void Cut_Request () {
            string requestLine;
            istringstream requestStream(request_body);
            getline(requestStream, requestLine);
            istringstream lineStream(requestLine);
            lineStream >> method >> file_path >> protocol;
            size_t header_end = request_body.find("\r\n\r\n");
            if (header_end == string::npos) { 
                Status_code(400);
                return;
            }
            headers = request_body.substr(0, header_end + 2);
            if (temp_file_path.empty()) {
                body = request_body.substr(header_end + 4);
            }
        }

        // 檢查並切割header
        void Check_Cut_Header () {
            if (method.empty() || file_path.empty() || protocol.empty()) {
                cout << "Invalid HTTP request format." << endl;
                Status_code(400);
                return;
            } else if (method != "GET" && method != "POST" && method != "HEAD" && method != "PUT" && method != "DELETE") {
                cout << "Unsupported HTTP method: " << method << endl;
                Status_code(400);
                return;
            } else if (protocol != "HTTP/1.1") {
                cout << "Unsupported HTTP protocol: " << protocol << endl;
                Status_code(505);
                return;
            }
            string header_line, key, value;
            istringstream headerStream(headers);
            size_t check_key, check_value;
            getline(headerStream, header_line);
            while (getline(headerStream, header_line)) {
                check_key = header_line.find(": ");
                if (check_key == string::npos) {
                    Status_code(400);
                    cout << "Header style error: " << header_line << endl;
                    return;
                }
                key = header_line.substr(0, check_key);

                check_value = header_line.find("\r");
                if (check_value == string::npos) {
                    Status_code(400);
                    cout << "Header style error: " << header_line << endl;
                    return;
                }
                value = header_line.substr(check_key + 2, check_value - (check_key + 2));
                header_map[key] = value;
            }
        }

        // 生成簡單的隨機令牌
        string GenerateToken() {
            int length = 32;
            const string charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
            string token;
            srand(time(NULL));
            for (int i = 0; i < length; ++i) {
                token += charset[rand() % charset.length()];
            }
            return token;
        }

        // 驗證令牌
        bool ValidateToken(const string& token) {
            for (const auto& pair : user_tokens) {
                if (pair.second == token) {
                    return true;
                }
            }
            return false;
        }

        // 獲取令牌對應的用戶名
        string GetUsernameFromToken(const string& token) {
            for (const auto& pair : user_tokens) {
                if (pair.second == token) {
                    return pair.first;
                }
            }
            return "";
        }

        //解析cookie
        void PraseCookie () {
            request_cookies.clear();
            string cookie_line = header_map["Cookie"];
            size_t pos = 0;
            while (pos < cookie_line.length()) {
                size_t eq_pos = cookie_line.find("=", pos);
                if (eq_pos == string::npos) {
                    break;
                }

                // 提取 cookie 名稱
                string name = cookie_line.substr(pos, eq_pos - pos);
                while (!name.empty() && name[0] == ' ') {
                    name = name.substr(1);
                }

                size_t semicolon_pos = cookie_line.find(";", eq_pos);
                string value;
                if (semicolon_pos == string::npos) {
                    // 這是最後一個 cookie
                    value = cookie_line.substr(eq_pos + 1);
                    pos = cookie_line.length();
                } else {
                    // 還有更多 cookie
                    value = cookie_line.substr(eq_pos + 1, semicolon_pos - (eq_pos + 1));
                    pos = semicolon_pos + 1;
                }

                request_cookies[name] = value;
                cout << "Found cookie: " << name << " = " << value << endl;
            }
        }

        //生成etag
        string GenerateETag(const string& filepath) {
            struct stat fileInfo;
            if (stat(filepath.c_str(), &fileInfo) != 0) {
                return "";  // 檔案不存在或無法訪問
            }
            
            // 使用檔案大小和最後修改時間來生成 ETag
            size_t filesize = fileInfo.st_size;
            time_t modTime = fileInfo.st_mtime;
            
            // 格式化為 "filesize-modtime" 的形式
            stringstream ss;
            ss << filesize << "-" << hex << modTime;
            return "\"" + ss.str() + "\"";
        }

        // 獲取檔案的最後修改時間並格式化
        time_t GetLastModifiedTime(const string& filepath) {
            struct stat fileInfo;
            if (stat(filepath.c_str(), &fileInfo) != 0) {
                cout << "無法獲取檔案資訊: " << filepath << endl;
                return -1;
            }
            
            time_t cftime = fileInfo.st_mtime;
            
            char buffer[80];
            struct tm timeinfo;
            
            gmtime_s(&timeinfo, &cftime);
            strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &timeinfo);
            cout << "lastmod: " << buffer << endl;
            
            return cftime;
        }

        // 解析 HTTP 日期格式
        time_t ParseHTTPTime(const string& timeStr) {
            struct tm tm = {};
            istringstream ss(timeStr);
            ss >> get_time(&tm, "%a, %d %b %Y %H:%M:%S");
            if (ss.fail()) {
                Status_code(400);
                cout << "Failed to parse time: " << timeStr << endl;
                return -1; // 解析失敗
            }
            return _mkgmtime(&tm);
        }

        void HandleIFMod() {
            if (header_map.find("If-Modified-Since") != header_map.end()) {
                string ifmodifiedSince = header_map["If-Modified-Since"];
                cout << "ifmod:" << ifmodifiedSince << endl;

                // 假設檔案的最後修改時間是以下函式取得的
                string filepath = "../www" + file_path;
                if (!File_exist(filepath)) {
                    Status_code(404);
                    return;
                }
                time_t lastmod = GetLastModifiedTime(filepath); 
                time_t ifmod = ParseHTTPTime(ifmodifiedSince);
                if (ifmod > lastmod) {
                    Status_code(304);
                    file_content = ""; // 不需要返回內容
                    return;
                }
            }
        }

        void HandleIFNoneMatch() {
            if (header_map.find("If-None-Match") != header_map.end()) {
                string ifnonematch = header_map["If-None-Match"];
                cout << "If-None-Match: " << ifnonematch << endl;
                
                string filepath = "../www" + file_path;
                if (!File_exist(filepath)) {
                    Status_code(404);
                    return;
                }
                
                // 生成當前檔案的 ETag
                string currentetag = GenerateETag(filepath);
                cout << "Current ETag: " << currentetag << endl;
                
                // 檢查 If-None-Match 是否包含 "*" 或匹配當前 ETag
                if (ifnonematch == "*" || ifnonematch == currentetag) {
                    // 資源未變更，返回 304 Not Modified
                    Status_code(304);
                    file_content = ""; // 不需要返回內容
                    return;
                }
                
                // 如果有多個 ETag 值（以逗號分隔）
                if (ifnonematch.find(",") != string::npos) {
                    istringstream iss(ifnonematch);
                    string token;
                    while (getline(iss, token, ',')) {
                        // 去除空白
                        token.erase(0, token.find_first_not_of(" \t"));
                        token.erase(token.find_last_not_of(" \t") + 1);
                        
                        if (token == currentetag) {
                            Status_code(304);
                            file_content = "";
                            return;
                        }
                    }
                }
            }
        }

        void HandleIFMatch() {
            if (header_map.find("If-Match") != header_map.end()) {
                string ifmatch = header_map["If-Match"];
                cout << "If-Match: " << ifmatch << endl;
                
                string filepath = "../www" + file_path;
                if (!File_exist(filepath)) {
                    Status_code(404);
                    return;
                }
                
                // 生成當前檔案的 ETag
                string currentetag = GenerateETag(filepath);
                cout << "Current ETag: " << currentetag << endl;
                
                // 如果 If-Match 是 "*"，表示任何 ETag 都匹配
                if (ifmatch == "*") {
                    // 資源存在，繼續處理
                    return;
                }
                
                // 檢查 If-Match 是否匹配當前 ETag
                bool matched = false;
                
                // 如果有多個 ETag 值（以逗號分隔）
                if (ifmatch.find(",") != string::npos) {
                    istringstream iss(ifmatch);
                    string token;
                    while (getline(iss, token, ',')) {
                        // 去除空白
                        token.erase(0, token.find_first_not_of(" \t"));
                        token.erase(token.find_last_not_of(" \t") + 1);
                        
                        if (token == currentetag) {
                            matched = true;
                            break;
                        }
                    }
                } else {
                    if (ifmatch == currentetag) {
                        matched = true;
                    }
                }
                
                if (!matched) {
                    // 如果 ETag 不匹配，返回 412 Precondition Failed
                    Status_code(412);
                    file_content = "";
                    return;
                }
            }
        }

        void HandleIFUnmod() {
            if (header_map.find("If-Unmodified-Since") != header_map.end()) {
                string ifunmodifiedsince = header_map["If-Unmodified-Since"];
                cout << "ifmod:" << ifunmodifiedsince << endl;

                string filepath = "../www" + file_path;
                if (!File_exist(filepath)) {
                    Status_code(404);
                    return;
                }
                time_t lastmod = GetLastModifiedTime(filepath); 
                time_t ifmod = ParseHTTPTime(ifunmodifiedsince);
                if (ifmod < lastmod) {
                    Status_code(412);
                    file_content = "";
                    return;
                }
            }
        }

        void HandleIFRange() {
            rangerequestallowed = false;
            if (header_map.find("Range") == header_map.end() || header_map.find("If-Range") == header_map.end()) {
                return;
            }
            
            string ifrange = header_map["If-Range"];
            cout << "If-Range: " << ifrange << endl;
            
            string filepath = "../www" + file_path;
            if (!File_exist(filepath)) {
                Status_code(404);
                return;
            }
            
            // 檢查 If-Range 是否為 ETag 值
            if (ifrange.front() == '"' && ifrange.back() == '"') {
                string currentetag = GenerateETag(filepath);
                if (ifrange == currentetag) {
                    rangerequestallowed = true;
                }
            } else {
                time_t ifrangetime = ParseHTTPTime(ifrange);
                time_t lastmodtime = GetLastModifiedTime(filepath);
                
                if (ifrangetime != -1 && lastmodtime != -1) {
                    if (lastmodtime <= ifrangetime) {
                        rangerequestallowed = true;
                    }
                }
            }
        }

        void HandleRangeRequest() {
            if (header_map.find("Range") == header_map.end()) {
                return;
            }
            if (!rangerequestallowed) {
                return;
            }
            string rangeheader = header_map["Range"];
            cout << "Range header: " << rangeheader << endl;
            
            if (rangeheader.substr(0, 6) != "bytes=") {
                Status_code(400);
                file_content = "<html><body><h1>400 Range Not Satisfiable</h1></body></html>";
                return;
            }
            
            string rangevalue = rangeheader.substr(6);
            size_t dashpos = rangevalue.find("-");
            if (dashpos == string::npos) {
                Status_code(400);
                file_content = "<html><body><h1>400 Range Not Satisfiable</h1></body></html>";
                return;
            }
            
            string startstr = rangevalue.substr(0, dashpos);
            string endstr = rangevalue.substr(dashpos + 1);
            
            long long filesize = file_content.size();
            long long rangestart = 0;
            long long rangeend = filesize - 1;
            
            // 解析起始位置
            if (!startstr.empty()) {
                rangestart = stoll(startstr);
                if (rangestart >= filesize) {
                    Status_code(400);
                    file_content = "<html><body><h1>400 Range Not Satisfiable</h1></body></html>";
                    return;
                }
            }
            
            // 解析結束位置
            if (startstr.empty() && !endstr.empty()) {
                // 如果格式是 "bytes=-N"，表示最後 N 個字節
                rangestart = filesize - stoll(endstr);
                if (rangestart < 0) {
                    rangestart = 0;
                }
                rangeend = filesize - 1;
            } else {
                // 解析起始位置
                if (!startstr.empty()) {
                    rangestart = stoll(startstr);
                    if (rangestart >= filesize) {
                        Status_code(416);
                        return;
                    }
                }
                
                // 解析結束位置
                if (!endstr.empty()) {
                    rangeend = stoll(endstr);
                    if (rangeend >= filesize) {
                        rangeend = filesize - 1;
                    }
                }
            }
            
            // 確保範圍有效
            if (rangestart > rangeend || rangestart < 0 || rangeend >= filesize) {
                Status_code(400);
                file_content = "<html><body><h1>400 Range Not Satisfiable</h1></body></html>";
                return;
            }
            
            Status_code(206);
            
            // 提取請求的部分內容
            long long contentlength = rangeend - rangestart + 1;
            string partialcontent = file_content.substr(rangestart, contentlength);
            file_content = partialcontent;
            
            // 添加 Content-Range 標頭
            content_range = "bytes " + to_string(rangestart) + "-" + to_string(rangeend) + "/" + to_string(filesize);
        }
        
        //處理條件請求
        void HandleCondition() {
            if (header_map.find("If-Match") != header_map.end()) {
                if (method == "GET" || method == "HEAD" || method == "PUT" || method == "DELETE") {
                    HandleIFMatch();
                } else {
                    Status_code(400);
                    cout << "If-Match used with invalid method: " << method << endl;
                }
            }
            if (header_map.find("If-None-Match") != header_map.end()) {
                if (method == "GET" || method == "HEAD" || method == "POST") {
                    HandleIFNoneMatch();
                } else {
                    Status_code(400);
                    cout << "If-None-Match used with invalid method: " << method << endl;
                }
            }
            if (header_map.find("If-Modified-Since") != header_map.end()) {
                if (method == "GET" || method == "HEAD") {
                    HandleIFMod();
                } else {
                    Status_code(400);
                    cout << "If-Modified-Since used with invalid method: " << method << endl;
                }
            }
            if (header_map.find("If-Unmodified-Since") != header_map.end()) {
                if (method == "GET" || method == "HEAD" || method == "PUT" || method == "DELETE") {
                    HandleIFUnmod();
                } else {
                    Status_code(400);
                    cout << "If-Unmodified-Since used with invalid method: " << method << endl;
                }
            }
            if (header_map.find("If-Range") != header_map.end()) {
                if (method == "GET" || method == "HEAD") {
                    HandleIFRange();
                } else {
                    Status_code(400);
                    cout << "If-Range used with invalid method: " << method << endl;
                }
            }
        }

        //連接解析
        void Connection () {
            size_t connect_pos = headers.find("Connection:");
            if (connect_pos != string::npos) {
                size_t connect_end = headers.find("\r\n", connect_pos);
                string connect_part = headers.substr(connect_pos + 11, connect_end - (connect_pos + 11));
                if (connect_part.find("keep-alive") != string::npos) {
                    connect = true;
                } else {
                    connect = false;
                }
            } else {
                connect = false;
            }
        }

        //解析帳戶密碼
        bool Find_user_pass (string &username, string &password) {
            size_t user_point = body.find("username=");
            if (user_point != string::npos) {
                // 從"username="後開始
                string username_part = body.substr(user_point + 9);
                // 尋找可能的分隔符，如&
                size_t delimiter = username_part.find("&");
                if (delimiter != string::npos) {
                    username = username_part.substr(0, delimiter);
                } else {
                    username = username_part;
                }
                cout << "Username: " << username << endl;
            } else {
                cout << "No username found in the request body." << endl;
                return false;
            }
            
            size_t password_point = body.find("password=");
            if (password_point != string::npos) {
                // 從"password="後開始
                string password_part = body.substr(password_point + 9);
                // 尋找可能的分隔符，如&
                size_t delimiter = password_part.find("&");
                if (delimiter != string::npos) {
                    password = password_part.substr(0, delimiter);
                } else {
                    password = password_part;
                }
                cout << "Password: " << password << endl;
            } else {
                cout << "No password found in the request body." << endl;
                return false;
            }
            return true;
        }

        //列出上傳目錄的檔案
        string Filelist () {
            filesystem::path upload_path = "../www/upload";
            if (!filesystem::exists(upload_path)) {
                return "[]"; // 如果目錄不存在，返回空的 JSON 陣列
            }
            string filelist = "[\n";
            for (const auto& entry : filesystem::directory_iterator(upload_path)) {
                if (entry.is_regular_file()) {
                    filelist += "   {\n";
                    filelist += "       \"name\": \"" + UTF8ToBig5(entry.path().filename().string()) + "\",\n";
                    filelist += "       \"size\": " + to_string(entry.file_size()) + "\n";
                    filelist += "   },\n";
                }
            }
            if (filelist.length() > 2) {
                filelist.pop_back();
                filelist.pop_back();
            }
            filelist += "\n]";
            return filelist;
        }

        //檢查是否登入
        bool Auth_user ()  {
            // 檢查是否有auth_token cookie
            auto it = request_cookies.find("auth_token");
            if (it != request_cookies.end()) {
                string token = it->second;
                // 驗證令牌
                if (ValidateToken(token)) {
                    // 令牌有效，設置auth為true
                    return true;
                }
            }
            
            Status_code(401);
            file_content = "{\"error\": \"Unauthorized. Please login or provide a valid token.\"}";
            file_type = "application/json;";
            return false;
        }

        void HandleGET () {
            if (file_path == "/index.html") {
                file_content = ReadFile();
                file_type = "text/html;";
            } else if (file_path == "/member.html") {
                if (!Auth_user()) {
                    return;
                }
                file_content = ReadFile();
                file_type = "text/html;";
            } else if (file_path == "/upload/" || file_path == "/upload") {
                if (!Auth_user()) {
                    return;
                }
                file_type = "application/json;";
                file_content = Filelist();
            } else if (file_path.find("/upload/") != string::npos) {
                if (!Auth_user()) {
                    return;
                }
                file_content = ReadFile();
                file_type = Get_file_type();
                download = true;
            } else if (file_path == "/test301.html") {
                if (!Auth_user()) {
                    return;
                }
                file_path = "https://www.google.com";
                file_type = "text/html;";
                Status_code(301);
            } else if (file_path == "/test400.html") {
                if (!Auth_user()) {
                    return;
                }
                file_content = "<html><body><h1>Error 400: Bad Request!</h1></body></html>";
                file_type = "text/html;";
                Status_code(400);
            } else if (file_path == "/style.css") {
                file_content = ReadFile();
                file_type = "text/css;";
            } else {
                if (!Auth_user()) {
                    return;
                }
                file_content = ReadFile();
                file_type = Get_file_type();
            }
            HandleRangeRequest();
        }

        void HandlePOST () {
            string username = "";
            string password = "";
            
            // 解析請求中的用戶名和密碼
            if (file_path == "/index.html") {
                if (!Find_user_pass(username, password)) {
                    Status_code(400);
                    return;
                }

                if (username == "123" && password == "12") {
                    file_path = "/member.html";
                    file_type = "text/html;";
                    Status_code(301);
                    cout << "user&pass ok" << endl;
                    
                    // 生成並存儲令牌
                    string token = GenerateToken();
                    user_tokens[username] = token;
                    
                    // 設置cookie，過期時間為30天
                    cookies_to_set += "Set-Cookie: auth_token=" + token + "; Max-Age=2592000; Path=/; HttpOnly; SameSite=Strict\r\n";

                } else {
                    file_content = ReadFile();
                    file_type = "text/html;";
                    Status_code(401);
                    size_t error_mes = file_content.find("<h1>登入會員</h1>");
                    string new_error = "<h1>登入會員</h1>\n    <div style=\"color: red;\">帳號或密碼錯誤，請重新輸入</div>";
                    file_content.replace(error_mes, 22, new_error);
                    cout << "user&pass false" << endl;
                }
            
            } else if (file_path == "/member.html") {
                if (!Auth_user()) {
                    return;
                } 
                if (body.find("logout=1") != string::npos) {
                    // 查找並刪除用戶的令牌
                    auto it = request_cookies.find("auth_token");
                    if (it != request_cookies.end()) {
                        string token = it->second;
                        string username = GetUsernameFromToken(token);
                        if (!username.empty()) {
                            user_tokens.erase(username);
                        }
                    }
                    
                    file_path = "/index.html";
                    Status_code(301);
                    
                    // 清除cookie
                    cookies_to_set += "Set-Cookie: auth_token=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict\r\n";
                }
            } else if (file_path == "/upload/" || file_path == "/upload") {
                if (!Auth_user()) {
                    return;
                }
                
                file_type = "application/json;";
                size_t content_type_pos = headers.find("Content-Type:");
                string content_type;
                if (content_type_pos != string::npos) {
                    size_t content_type_end = headers.find("\r\n", content_type_pos);
                    content_type = headers.substr(content_type_pos + 13, content_type_end - (content_type_pos + 13));
                }
                
                // 處理 multipart/form-data 格式
                if (content_type.find("multipart/form-data") != string::npos) {
                    // 原有邏輯處理 multipart/form-data
                    if (!temp_file_path.empty()) {
                        ifstream tempFile(temp_file_path, ios::binary);
                        if (tempFile.is_open()) {
                            stringstream buffer;
                            buffer << tempFile.rdbuf();
                            body = buffer.str();
                            tempFile.close();
                            // 刪除暫存檔案
                            if (remove(temp_file_path.c_str()) != 0) {
                                cout << "無法刪除暫存檔案: " << temp_file_path << endl;
                            }
                        } else {
                            cout << "無法打開暫存檔案: " << temp_file_path << endl;
                            Status_code(500);
                            file_content = "{\"success\": false, \"error\": \"無法處理上傳的檔案\"}";
                            return;
                        }
                    }
                    
                    size_t boundary_pos = content_type.find("boundary=");
                    if (boundary_pos != string::npos) {
                        // 找到請求中的 boundary
                        size_t boundary_end = content_type.find("\r\n", boundary_pos);
                        string boundary = content_type.substr(boundary_pos + 9, boundary_end - (boundary_pos + 9));
                        
                        // 查找文件內容的開始位置
                        string boundary_marker = "--" + boundary;
                        size_t content_start = body.find(boundary_marker);
                        if (content_start != string::npos) {
                            // 查找文件名
                            string upload_filename;
                            size_t filename_pos = body.find("filename=\"", content_start);
                            if (filename_pos != string::npos) {
                                filename_pos += 10;
                                size_t filename_end = body.find("\"", filename_pos);
                                if (filename_end != string::npos) {
                                    upload_filename = body.substr(filename_pos, filename_end - filename_pos);

                                    upload_filename = UTF8ToBig5(upload_filename);
                                }
                            }
                            
                            // 查找文件內容的開始
                            size_t file_start = body.find("\r\n\r\n", content_start);
                            if (file_start != string::npos) {
                                file_start += 4; 
                                
                                // 查找文件內容的結束
                                size_t file_end = body.find(boundary_marker, file_start);
                                if (file_end != string::npos) {
                                    file_end -= 2; // 回退 \r\n
                                    
                                    // 提取文件內容
                                    string file_data = body.substr(file_start, file_end - file_start);
                                    
                                    // 保存文件
                                    string filepath = "../www/upload/" + upload_filename;
                                    if (File_exist(filepath)) {
                                        // 如果檔案已存在，添加數字後綴
                                        size_t dot_pos = upload_filename.find_last_of(".");
                                        string name_part = (dot_pos != string::npos) ? upload_filename.substr(0, dot_pos) : upload_filename;
                                        string ext_part = (dot_pos != string::npos) ? upload_filename.substr(dot_pos) : "";
                                        int copy_index = 1;
                                        upload_filename = name_part + "_" + to_string(copy_index) + ext_part;
                                        filepath = "../www/upload/" + upload_filename;
                                        while (File_exist(filepath)) {
                                            copy_index++;
                                            upload_filename = name_part + "_" + to_string(copy_index) + ext_part;
                                            filepath = "../www/upload/" + upload_filename;
                                        }
                                    }
                                    ofstream outfile(filepath, ios::binary);
                                    if (outfile.is_open()) {
                                        outfile.write(file_data.data(), file_data.length());
                                        outfile.close();
                                        
                                        // 設置成功響應
                                        file_content = "{\"success\": true, \"filename\": \"" + upload_filename + "\"}";
                                        cout << "file: " << upload_filename << " upload succses" << endl;
                                    } else {
                                        Status_code(500);
                                        file_content = "{\"success\": false, \"error\": \"無法保存文件\"}";
                                        cout << " file upload failed " << upload_filename << endl;
                                    }
                                } else {
                                    Status_code(400);
                                    file_content = "{\"success\": false, \"error\": \"無法解析文件內容\"}";
                                }
                            } else {
                                Status_code(400);
                                file_content = "{\"success\": false, \"error\": \"無法找到文件內容\"}";
                            }
                        } else {
                            Status_code(400);
                            file_content = "{\"success\": false, \"error\": \"無法解析請求\"}";
                        }
                    } else {
                        Status_code(400);
                        file_content = "{\"success\": false, \"error\": \"未找到boundary參數\"}";
                    }
                } else {
                    // 從請求路徑或 URL 參數中獲取檔案名稱
                    string upload_filename;
                    
                    size_t slash = content_type.find("/");
                    if (slash != string::npos && slash < content_type.length() - 1) {
                        cout << "找到Content-Type: " << content_type << endl;
                        upload_filename = content_type.substr(slash + 1);
                    }
                    
                    time_t now = time(0);
                    struct tm *timeinfo = localtime(&now);
                    char time_buffer[80];
                    strftime(time_buffer, 80, "%Y%m%d_%H%M%S", timeinfo);
                    string time_str(time_buffer);
                    
                    if (upload_filename.empty()) {
                        upload_filename = "file_" + time_str + ".txt";
                    } else {
                        upload_filename = "file_" + time_str + "." + upload_filename;
                    }
                    
                    string filepath = "../www/upload/" + upload_filename;
                    if (File_exist(filepath)) {
                        // 如果檔案已存在，添加數字後綴
                        size_t dot_pos = upload_filename.find_last_of(".");
                        string name_part = (dot_pos != string::npos) ? upload_filename.substr(0, dot_pos) : upload_filename;
                        string ext_part = (dot_pos != string::npos) ? upload_filename.substr(dot_pos) : "";
                        int copy_index = 1;
                        upload_filename = name_part + "_" + to_string(copy_index) + ext_part;
                        filepath = "../www/upload/" + upload_filename;
                        while (File_exist(filepath)) {
                            copy_index++;
                            upload_filename = name_part + "_" + to_string(copy_index) + ext_part;
                            filepath = "../www/upload/" + upload_filename;
                        }
                    }
                    if (!temp_file_path.empty()) {
                        try {
                            filesystem::create_directories("../www/upload/");
                            filesystem::copy_file(temp_file_path, filepath, filesystem::copy_options::overwrite_existing);
                            
                            // 刪除暫存檔案
                            filesystem::remove(temp_file_path);
                            
                            // 設置成功響應
                            file_content = "{\"success\": true, \"filename\": \"" + upload_filename + "\"}";
                            cout << "file: " << upload_filename << " upload success " << endl;
                        } catch (const std::exception& e) {
                            Status_code(500);
                            file_content = "{\"success\": false, \"error\": \"檔案系統錯誤: " + string(e.what()) + "\"}";
                            cout << "檔案系統錯誤: " << e.what() << endl;
                        }
                    } else {
                        ofstream outfile(filepath, ios::binary);
                        if (outfile.is_open()) {
                            outfile.write(body.data(), body.length());
                            outfile.close();
                            
                            // 設置成功響應
                            file_content = "{\"success\": true, \"filename\": \"" + upload_filename + "\"}";
                            cout << "file: " << upload_filename << " upload success (direct binary)" << endl;
                        } else {
                            Status_code(500);
                            file_content = "{\"success\": false, \"error\": \"無法保存文件\"}";
                            cout << "file upload failed " << upload_filename << endl;
                        }
                    }
                }

            } else if (file_path.find("/upload/") != string::npos && file_path != "/upload/" && file_path != "/upload") {
                if (!Auth_user()) {
                    return;
                }
                string filepath = "../www" + file_path;
                if (!filesystem::exists(filepath)) {
                    Status_code(404);
                    file_content = "{\"success\": false, \"error\": \"指定的txt檔案不存在\"}";
                    file_type = "application/json;";
                    return;
                }
                ofstream file(filepath, ios::binary|ios::app);
                if (file.is_open()) {
                    file.write(body.data(), body.length());
                    file_content = "{\"success\": true, \"message\": \"檔案已更新\"}";
                    file_type = "application/json;";
                    file.close();
                    return;
                } else {
                    Status_code(500);
                    file_content = "{\"success\": false, \"error\": \"無法打開指定的txt檔案進行寫入\"}";
                    file_type = "application/json;";
                    return;
                }

            } else {
                Status_code(400);
                file_content = "{\"success\": false, \"error\": \"無法處理該路徑的POST請求\"}";
                file_type = "application/json;";
                return;
            }
        }
        
        void HandleHEAD () {
            if (file_path == "/index.html") {
                file_type = "text/html;";
            } else if (file_path == "/member.html") {
                if (!Auth_user()) {
                    return;
                }
                file_type = "text/html;";
            } else if (file_path == "/upload/") {
                if (!Auth_user()) {
                    return;
                }
                file_type = "application/json;";
            } else if (file_path.find("/upload/") != string::npos) {
                if (!Auth_user()) {
                    return;
                }
                file_type = Get_file_type();
            } else {
                if (!Auth_user()) {
                    return;
                }
                file_type = Get_file_type();
            }
            HandleRangeRequest();
        }
        
        void HandlePUT () {
            if (file_path == "/upload/" || file_path == "/upload") {
                if (!Auth_user()) {
                    return;
                }
                
                file_type = "application/json;";
                size_t content_type_pos = headers.find("Content-Type:");
                string content_type;
                if (content_type_pos != string::npos) {
                    size_t content_type_end = headers.find("\r\n", content_type_pos);
                    content_type = headers.substr(content_type_pos + 13, content_type_end - (content_type_pos + 13));
                }
                
                // 處理 multipart/form-data 格式
                if (content_type.find("multipart/form-data") != string::npos) {
                    // 原有邏輯處理 multipart/form-data
                    if (!temp_file_path.empty()) {
                        ifstream tempFile(temp_file_path, ios::binary);
                        if (tempFile.is_open()) {
                            stringstream buffer;
                            buffer << tempFile.rdbuf();
                            body = buffer.str();
                            tempFile.close();
                            // 刪除暫存檔案
                            if (remove(temp_file_path.c_str()) != 0) {
                                cout << "無法刪除暫存檔案: " << temp_file_path << endl;
                            }
                        } else {
                            cout << "無法打開暫存檔案: " << temp_file_path << endl;
                            Status_code(500);
                            file_content = "{\"success\": false, \"error\": \"無法處理上傳的檔案\"}";
                            return;
                        }
                    }
                    
                    size_t boundary_pos = content_type.find("boundary=");
                    if (boundary_pos != string::npos) {
                        // 找到請求中的 boundary
                        size_t boundary_end = content_type.find("\r\n", boundary_pos);
                        string boundary = content_type.substr(boundary_pos + 9, boundary_end - (boundary_pos + 9));
                        
                        // 查找文件內容的開始位置
                        string boundary_marker = "--" + boundary;
                        size_t content_start = body.find(boundary_marker);
                        if (content_start != string::npos) {
                            // 查找文件名
                            string upload_filename;
                            size_t filename_pos = body.find("filename=\"", content_start);
                            if (filename_pos != string::npos) {
                                filename_pos += 10;
                                size_t filename_end = body.find("\"", filename_pos);
                                if (filename_end != string::npos) {
                                    upload_filename = body.substr(filename_pos, filename_end - filename_pos);

                                    upload_filename = UTF8ToBig5(upload_filename);
                                }
                            }
                            
                            // 查找文件內容的開始
                            size_t file_start = body.find("\r\n\r\n", content_start);
                            if (file_start != string::npos) {
                                file_start += 4; 
                                
                                // 查找文件內容的結束
                                size_t file_end = body.find(boundary_marker, file_start);
                                if (file_end != string::npos) {
                                    file_end -= 2; // 回退 \r\n
                                    
                                    // 提取文件內容
                                    string file_data = body.substr(file_start, file_end - file_start);
                                    
                                    // 保存文件
                                    string filepath = "../www/upload/" + upload_filename;
                                    ofstream outfile(filepath, ios::binary);
                                    if (outfile.is_open()) {
                                        outfile.write(file_data.data(), file_data.length());
                                        outfile.close();
                                        
                                        // 設置成功響應
                                        file_content = "{\"success\": true, \"filename\": \"" + upload_filename + "\"}";
                                        cout << "file: " << upload_filename << " upload succses" << endl;
                                    } else {
                                        Status_code(500);
                                        file_content = "{\"success\": false, \"error\": \"無法保存文件\"}";
                                        cout << " file upload failed " << upload_filename << endl;
                                    }
                                } else {
                                    Status_code(400);
                                    file_content = "{\"success\": false, \"error\": \"無法解析文件內容\"}";
                                }
                            } else {
                                Status_code(400);
                                file_content = "{\"success\": false, \"error\": \"無法找到文件內容\"}";
                            }
                        } else {
                            Status_code(400);
                            file_content = "{\"success\": false, \"error\": \"無法解析請求\"}";
                        }
                    } else {
                        Status_code(400);
                        file_content = "{\"success\": false, \"error\": \"未找到boundary參數\"}";
                    }
                } else {
                    // 從請求路徑或 URL 參數中獲取檔案名稱
                    string upload_filename;
                    
                    size_t slash = content_type.find("/");
                    if (slash != string::npos && slash < content_type.length() - 1) {
                        cout << "找到Content-Type: " << content_type << endl;
                        upload_filename = content_type.substr(slash + 1);
                    }
                    
                    time_t now = time(0);
                    struct tm *timeinfo = localtime(&now);
                    char time_buffer[80];
                    strftime(time_buffer, 80, "%Y%m%d", timeinfo);
                    string time_str(time_buffer);
                    
                    if (upload_filename.empty()) {
                        upload_filename = "file_" + time_str + ".txt";
                    } else {
                        upload_filename = "file_" + time_str + "." + upload_filename;
                    }
                    
                    string filepath = "../www/upload/" + upload_filename;
                    
                    if (!temp_file_path.empty()) {
                        try {
                            filesystem::create_directories("../www/upload/");
                            filesystem::copy_file(temp_file_path, filepath, filesystem::copy_options::overwrite_existing);
                            
                            // 刪除暫存檔案
                            filesystem::remove(temp_file_path);
                            
                            // 設置成功響應
                            file_content = "{\"success\": true, \"filename\": \"" + upload_filename + "\"}";
                            cout << "file: " << upload_filename << " upload success " << endl;
                        } catch (const std::exception& e) {
                            Status_code(500);
                            file_content = "{\"success\": false, \"error\": \"檔案系統錯誤: " + string(e.what()) + "\"}";
                            cout << "檔案系統錯誤: " << e.what() << endl;
                        }
                    } else {
                        ofstream outfile(filepath, ios::binary);
                        if (outfile.is_open()) {
                            outfile.write(body.data(), body.length());
                            outfile.close();
                            
                            // 設置成功響應
                            file_content = "{\"success\": true, \"filename\": \"" + upload_filename + "\"}";
                            cout << "file: " << upload_filename << " upload success (direct binary)" << endl;
                        } else {
                            Status_code(500);
                            file_content = "{\"success\": false, \"error\": \"無法保存文件\"}";
                            cout << "file upload failed " << upload_filename << endl;
                        }
                    }
                }
            } else if (file_path.find("/upload/") != string::npos) {
                if (!Auth_user()) {
                    return;
                }

                string filepath = "../www" + file_path;
                if (!filesystem::exists(filepath)) {
                    Status_code(404);
                    file_content = "{\"success\": false, \"error\": \"指定的txt檔案不存在\"}";
                    file_type = "application/json;";
                    return;
                }
                ofstream file(filepath, ios::binary|ios::trunc);
                if (file.is_open()) {
                    file.write(body.data(), body.length());
                    file_content = "{\"success\": true, \"message\": \"檔案已更新\"}";
                    file_type = "application/json;";
                    file.close();
                    return;
                } else {
                    Status_code(500);
                    file_content = "{\"success\": false, \"error\": \"無法打開指定的txt檔案進行寫入\"}";
                    file_type = "application/json;";
                    return;
                }
            } else {
                Status_code(400);
                file_content = "{\"success\": false, \"error\": \"無法處理該路徑的PUT請求\"}";
                file_type = "application/json;";
            }
        }
        
        void HandleDELETE () {
            file_type = "application/json;";
            if (file_path.find("/upload/") != string::npos) {
                if (!Auth_user()) {
                    return;
                }
                try {
                    string delete_path = "../www" + file_path;
                    ifstream checkFile(delete_path);
                    if (checkFile.good()) {
                        checkFile.close();
                        if (remove(delete_path.c_str()) == 0) {
                            file_content = "{\"success\": true, \"message\": \"檔案已刪除\"}";
                        } else {
                            cout << "檔案系統錯誤" << endl;
                            Status_code(500);
                            file_content = "{\"success\": false, \"error\": \"檔案系統錯誤\"}";
                        }
                    } else {
                        cout << "not found:" << delete_path <<endl;
                        Status_code(404);
                        file_content = "{\"success\": false, \"error\": \"上傳目錄不存在\"}";
                    }
                } catch (const exception& e) {
                    cout << "error: " << e.what() << endl;
                    Status_code(500);
                    file_content = "{\"success\": false, \"error\": \"伺服器內部錯誤\"}";
                }
            } else {
                Status_code(404);
                file_content = "{\"success\": false, \"error\": \"無法刪除該路徑\"}";
            }
        }

        //選擇方法
        void Method_choose () {
            if (method == "GET") {
                HandleGET();
            } else if (method == "POST") {
                HandlePOST();
            } else if (method == "HEAD") {
                HandleHEAD(); 
            } else if (method == "PUT") {
                HandlePUT();  
            } else if (method == "DELETE") {
                HandleDELETE(); 
            }
        }

        // 解析file的種類
        string Get_file_type () {
            size_t dotPos = file_path.find_last_of(".");
            if (dotPos == string::npos) {
                return "text/plain;";
            }
            string file = file_path.substr(dotPos + 1);
            
            // 常見的文本類型
            if (file == "html" || file == "htm") {
                return "text/html;";
            } else if (file == "css") {
                return "text/css;";
            } else if (file == "js") {
                return "application/javascript;";
            } else if (file == "json") {
                return "application/json;";
            } else if (file == "xml") {
                return "application/xml;";
            } else if (file == "txt") {
                return "text/plain;";
            }
            // 圖片類型
            else if (file == "jpg" || file == "jpeg") {
                return "image/jpeg;";
            } else if (file == "png") {
                return "image/png;";
            } else if (file == "gif") {
                return "image/gif;";
            } else if (file == "svg") {
                return "image/svg+xml;";
            } else if (file == "ico") {
                return "image/x-icon;";
            }
            // 音頻和視頻類型
            else if (file == "mp3") {
                return "audio/mpeg;";
            } else if (file == "wav") {
                return "audio/wav;";
            } else if (file == "mp4") {
                return "video/mp4;";
            } else if (file == "webm") {
                return "video/webm;";
            }
            // 文檔類型
            else if (file == "pdf") {
                return "application/pdf;";
            } else if (file == "doc" || file == "docx") {
                return "application/msword;";
            } else if (file == "xls" || file == "xlsx") {
                return "application/vnd.ms-excel;";
            }
            // 壓縮文件
            else if (file == "zip") {
                return "application/zip;";
            } else if (file == "rar") {
                return "application/x-rar-compressed;";
            } else if (file == "7z") {
                return "application/x-7z-compressed;";
            }
            // 默認返回純文本
            else {
                return "text/plain;";
            }
        }

        //設定狀態碼
        void Status_code (int code) {
            if (status_code == "200") {
                if (code == 206) {
                    status_code = "206";
                } else if (code == 301) {
                    status_code = "301";
                    file_content = ReadFile();
                } else if (code == 304) {
                    status_code = "304";
                } else if (code == 400) {
                    status_code = "400";
                } else if (code == 401) {
                    status_code = "401";
                } else if (code == 404) {
                    status_code = "404";
                } else if (code == 412) {
                    status_code = "412";
                    file_content = "<html><body><h1>前提條件失敗</h1><p>請求中的前提條件未被滿足</p></body></html>";
                } else if (code == 500) {
                    status_code = "500";
                } else if (code == 505) {
                    status_code = "505";
                    file_content = "<html><body><h1>HTTP版本不支援</h1><p>伺服器不支援您使用的HTTP版本</p>"
                    "<p>請使用 HTTP/1.1 或更早版本</p></body></html>";
                }
            }
        }

        //處理狀態碼的response
        void HTTP_code_respone (string &response) {
            if (status_code == "200") {
                response += status_code;
                response += " OK\r\n";
            } else if (status_code == "206") {
                response += status_code;
                response += " Partial Content\r\n";
            } else if (status_code == "301") {
                response += status_code;
                response += " Moved Permanently\r\n";
            } else if (status_code == "304") {
                response += status_code;
                response += " Not Modified\r\n";
            } else if (status_code == "400") {
                response += status_code;
                response += " Bad Request\r\n";
            } else if (status_code == "401") {
                response += status_code;
                response += " Unauthorized\r\n";
            } else if (status_code == "404") {
                response += status_code;
                response += " Not Found\r\n";
            } else if (status_code == "412") {
                response += status_code;
                response += " Precondition Failed\r\n";
            } else if (status_code == "505") {
                response += status_code;
                response += " HTTP Version Not Supported\r\n";
            } else {
                response += "500 Internal Server Error\r\n";
            }
        }

    public:
        HTTP () {
            connect = false;
            method = "";
            file_path = "";
            protocol = "";
            file_content = "";
            file_type = "";
            status_code = "200";
            cookies_to_set = "";
            request_body = "";
            headers = "";
            body = "";
            content_range = "";
            temp_file_path = "";
            rangerequestallowed = true;
            download = false;
        }

        //重置 HTTP 狀態
        void ResetHTTP () {
            method = "";
            file_path = "";
            protocol = "";
            file_content = "";
            file_type = "";
            status_code = "200";
            cookies_to_set = "";
            request_body = "";
            headers = "";
            body = "";
            content_range = "";
            temp_file_path = "";
            header_map.clear();
            rangerequestallowed = false;
            download = false;
        }

        //設定連線狀態
        bool GET_Connect () {
            return connect;
        }

        //建立 HTTP 回應
        string HTTP_response (const string &request, const string &temp_file_path) {
            request_body = request;
            this->temp_file_path = temp_file_path;

            Cut_Request();
            
            cout << "headers:" << endl << UTF8ToBig5(headers) << endl;
            cout << "body:" << endl << body.size() << " bytes" << endl;
            cout << endl;
            Check_Cut_Header();
            PraseCookie();
            Connection();
            
            file_path = UTF8ToBig5(file_path); 
            
            // 預設路徑
            if (file_path == "/") {
                file_path = "/index.html";
            }

            if (status_code == "200") {
                HandleCondition();
                if (status_code == "200") {
                    Method_choose();
                }
            }

            // if (method == "GET" && file_path.find("/upload/") != string::npos) {
            //     system("pause");
            // }

            // 建立 HTTP 回應
            string response = "HTTP/1.1 ";
            HTTP_code_respone(response);
            if (status_code == "301") {
                response += "Location: " + file_path + "\r\n";
            }
            if (status_code == "206" && !content_range.empty()) {
                response += "Content-Range: " + content_range + "\r\n";
            }
            response += "Content-Type: ";
            response += file_type;
            response += " charset=big5\r\n";

            if (!cookies_to_set.empty()) {
                response += cookies_to_set;
            }

            if (download && (status_code == "200" || status_code == "206")) {
                size_t lastSlash = file_path.find_last_of("/");
                if (lastSlash != string::npos) {
                    lastSlash++; // 移動到斜杠後面一位
                } else {
                    lastSlash = 0; // 如果沒有斜杠，從頭開始
                }
                string filename = file_path.substr(lastSlash);
                response += "Content-Disposition: attachment; filename=\"" + filename + "\r\n";
                cout << filename << endl;
            }

            if (connect) {
                response += "Connection: keep-alive\r\n";
                response += "Keep-Alive: timeout=5, max=100\r\n";
            } else {
                response += "Connection: close\r\n";
            }

            if (status_code == "200" || status_code == "206") {
                string filepath = "../www" + file_path;
                if (File_exist(filepath) && filesystem::is_regular_file(filepath)) {
                    string etag = GenerateETag(filepath);
                    response += "ETag: " + etag + "\r\n";
                }
            }

            response += "Content-Length: " + to_string(file_content.length()) + "\r\n";

            cout << endl << response <<endl;

            response += "\r\n"; // 標頭與內容之間的空行

            response += file_content;
            
            return response;
        }
};

// 接收完整的 HTTP 請求
string ReceiveFullRequest(SOCKET socket, string& temp_file_path) {
    const int BUFFER_SIZE = 8192;
    const size_t LARGE_FILE_THRESHOLD = 1024 * 1024; // 超過 1MB 的檔案視為大檔案
    char buffer[BUFFER_SIZE];
    string temp_req;
    int bytesReceived;
    ofstream tempFile;
    bool isLargeFile = false;

    // 設置接收超時
    struct timeval timeout;
    timeout.tv_sec = 120;  // 超時時間2分鐘
    timeout.tv_usec = 0;

    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        cout << "無法設置接收超時" << endl;
    }

    // 接收第一部分請求，以獲取請求頭
    bytesReceived = recv(socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytesReceived <= 0) {
        return "";
    }

    buffer[bytesReceived] = '\0';
    temp_req.append(buffer, bytesReceived);
    
    // 尋找請求頭結束位置
    size_t headerEnd = temp_req.find("\r\n\r\n");
    if (headerEnd == string::npos) {
        // 如果還沒收到完整的頭部，繼續接收
        do {
            bytesReceived = recv(socket, buffer, BUFFER_SIZE - 1, 0);
            if (bytesReceived <= 0) {
                break;
            }

            buffer[bytesReceived] = '\0';
            temp_req.append(buffer, bytesReceived);
            headerEnd = temp_req.find("\r\n\r\n");
        } while (headerEnd == string::npos);

        if (headerEnd == string::npos) {
            return temp_req;
        }
    }
    string header = temp_req.substr(0, headerEnd + 4);
    string temp_body = temp_req.substr(headerEnd + 4);
    // 查找 Content-Length
    size_t contentLengthPos = header.find("Content-Length: ");
    if (contentLengthPos != string::npos) {
        size_t endOfLine = header.find("\r\n", contentLengthPos);
        if (endOfLine != string::npos) {
            string contentLengthStr = header.substr(contentLengthPos + 16, endOfLine - (contentLengthPos + 16));
            long long contentLength = strtoll(contentLengthStr.c_str(), NULL, 10);

            // 如果檔案大小超過閾值，視為大檔案
            if (contentLength > LARGE_FILE_THRESHOLD) {
                isLargeFile = true;
                tempFile.open(temp_file_path, ios::binary);
                tempFile.write(temp_body.c_str(), temp_body.size());
                if (!tempFile.is_open()) {
                    cout << "無法打開暫存檔案: " << temp_file_path << endl;
                    return "";
                }
            } else {
                temp_file_path.clear();
            }

            // 計算已接收的內容部分
            size_t receivedContentLength = temp_req.length() - (headerEnd + 4);

            // 如果內容部分尚未完全接收
            while (receivedContentLength < static_cast<size_t>(contentLength)) {
                long long remainingBytes = contentLength - receivedContentLength;
                int bytesToReceive = (remainingBytes < BUFFER_SIZE) ? static_cast<int>(remainingBytes) : BUFFER_SIZE;

                bytesReceived = recv(socket, buffer, bytesToReceive, 0);
                if (bytesReceived <= 0) {
                    if (WSAGetLastError() == WSAETIMEDOUT) {
                        cout << "接收超時，已接收: " << receivedContentLength << " 字節，總計: " << contentLength << " 字節" << endl;
                    } else {
                        cout << "接收錯誤或連接關閉，錯誤碼: " << WSAGetLastError() << endl;
                    }
                    break;  // 連接關閉或出錯
                }

                if (isLargeFile) {
                    // 將接收到的內容部分直接寫入檔案
                    tempFile.write(buffer, bytesReceived);
                } else {
                    // 將接收到的內容追加到內存中
                    temp_req.append(buffer, bytesReceived);
                }
                receivedContentLength += bytesReceived;

                // 更新接收進度
                cout << "\r接收進度: " << (receivedContentLength * 100 / contentLength) << "%";
                cout.flush();
            }

            // 完成接收後換行
            cout << endl;

            // 檢查是否接收完全
            if (receivedContentLength < static_cast<size_t>(contentLength)) {
                cout << "警告: 未能接收完整請求，接收了 " << receivedContentLength
                     << " 字節，應接收 " << contentLength << " 字節" << endl;
            }
        }
    }

    if (isLargeFile) {
        tempFile.close();
        return header.substr(0, headerEnd + 4);  // 返回請求頭部
    } else {
        return temp_req;  // 返回完整請求（小檔案）
    }
}

int main() {
    WSAData wsaData;
    WORD version = MAKEWORD(2, 2); 
    int iResult = WSAStartup(version, &wsaData); 
    if (iResult != 0) {
        cout << "Winsock failed: " << iResult << endl;
        system("pause");
        return 1;
    }
    cout << "Winsock correct!" << endl;

    SOCKET sListen = INVALID_SOCKET;
    sListen= socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sListen== INVALID_SOCKET) {

        cout << "sListen failed " << endl;
        WSACleanup();
        system("pause");
        return 1;
    }
    cout << "sListen correct!" << endl;

    SOCKADDR_IN addr;
    memset (&addr, 0, sizeof (addr)) ; 
    addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80); 


    // addr 為 sockaddr_in 結構，強制轉型為 sockaddr 結構

    int socket_bind = bind(sListen, (SOCKADDR*)&addr, sizeof(addr));

    if (socket_bind == SOCKET_ERROR) {
        cout << "bind failed! Error: " << WSAGetLastError() << endl;
        WSACleanup();
        system("pause");
        return 1;
    }
    cout << "bind correct!" << endl;

    int socket_listen = listen(sListen, SOMAXCONN);

    if (socket_listen == SOCKET_ERROR) {
        cout << "socket_listen failed! Error: " << WSAGetLastError() << endl;
        WSACleanup();
        system("pause");
        return 1;
    }
    cout << "socket_listen correct!" << endl;
    cout << "Web server running on http://127.0.0.1:80" << endl;
    cout << "Press Ctrl+C to stop the server" << endl;

    SOCKET sConnect = INVALID_SOCKET;
    HTTP http_server = HTTP();

    struct sockaddr_in clientAddr; // client 端位址資訊
    int clientAddrLen = sizeof(clientAddr);
    while (1) {
        if (sConnect == INVALID_SOCKET) {
            sConnect = accept(sListen, (SOCKADDR*)&clientAddr, &clientAddrLen);
            printf("\nserver: got connection from %s\n\n", inet_ntoa(clientAddr.sin_addr));
        }
        if (sConnect != INVALID_SOCKET) {
            
            struct timeval timeout;      
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;
            
            if (setsockopt(sConnect, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
                cout << "無法設置接收超時" << endl;
            }
            string temp_file_path = "temp_request.txt";
            string request = ReceiveFullRequest(sConnect, temp_file_path);
            if (request.empty()) {
                // 如果收到空請求，應該關閉連線
                closesocket(sConnect);
                sConnect = INVALID_SOCKET;
                continue;
            }
            
            if (!request.empty()) {
                string http_respone = http_server.HTTP_response(request, temp_file_path);

                int bytesSent = send(sConnect, http_respone.c_str(), http_respone.size(), 0);

                if (bytesSent == SOCKET_ERROR) {
                    cout << "Send failed! Error: " << WSAGetLastError() << endl << endl;
                } else {
                    cout << "Successfully sent " << bytesSent << " bytes." << endl << endl;
                }
            }
            http_server.ResetHTTP();
            if (!http_server.GET_Connect()) {
                closesocket(sConnect);
                sConnect = INVALID_SOCKET;
            }
        } else {
            cout << "socket_accept failed! Error: " << WSAGetLastError() << endl;
            break;
        }
    }
    closesocket(sConnect);
    closesocket(sListen);
    WSACleanup();
    system("pause");
    return 0;
}
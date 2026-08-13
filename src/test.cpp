#include <winsock2.h>
#include <iostream>
#include <string>
#include <string.h>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>
#include <algorithm>

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
        map<string, string> request_cookies; // 從請求中讀取的 cookies
        map<string, string> user_tokens; // username -> token
        string request_body; // http 全部
        string headers; // http head
        string body; // http body
        string temp_file_path; // 大檔案暫存路徑

        // URL 解碼
        string URLDecode(const string& text) {
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
        string UTF8ToBig5   (const string& utf8Str) {
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

        void Check_Request () {
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
            string header_line;
            istringstream headerStream(headers);
            size_t check;
            getline(headerStream, header_line);
            while (getline(headerStream, header_line)) {
                check = header_line.find(": ");
                if (check == string::npos) {
                    Status_code(400);
                    cout << "Header style error: " << header_line << endl;
                    return;
                }
                check = header_line.find("\r");
                if (check == string::npos) {
                    Status_code(400);
                    cout << "Header style error: " << header_line << endl;
                    return;
                }
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
            size_t cookie_start = headers.find("Cookie:");
            if (cookie_start == string::npos) {
                return;
            }
            size_t line_end = headers.find("\r\n", cookie_start);
            if (line_end == string::npos) {
                return;
            }
            string cookie_line = headers.substr(cookie_start+8, line_end-(cookie_start+8));
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
        void Find_user_pass (string &username, string &password) {
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
            }
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
            } else if (file_path == "test.html") {
                file_type = "text/html;";
                file_content = "<html><body><h1>Test Page</h1><p>This is a test page.</p></body></html>";
            } else {
                file_content = ReadFile();
                file_type = Get_file_type();
            }
        }

        void HandlePOST () {
            string username = "";
            string password = "";
            
            // 解析請求中的用戶名和密碼
            if (file_path == "/index.html") {
                Find_user_pass(username, password);

                if (username == "123" && password == "12") {
                    file_path = "/member.html";
                    file_type = "text/html;";
                    Status_code(301);
                    cout << "user&pass ok" << endl;
                    
                    // 生成並存儲令牌
                    string token = GenerateToken();
                    user_tokens[username] = token;
                    
                    // 設置cookie，過期時間為30天
                    cookies_to_set = "Set-Cookie: username=" + username + "; Max-Age=2592000; Path=/\r\n";
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
                    cookies_to_set = "Set-Cookie: username=; Max-Age=0; Path=/\r\n";
                    cookies_to_set += "Set-Cookie: auth_token=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict\r\n";
                }
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
            }
        }
        
        void HandlePUT () {
            if (file_path == "/upload/") {
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
                if (temp_file_path.empty()) {
                        // 處理 multipart/form-data 格式
                    if (content_type.find("multipart/form-data") != string::npos) {
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
                            cout << "test" <<endl;
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
                        
                        // 保存文件
                        string filepath = "../www/upload/" + upload_filename;
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
                } else {
                    ifstream tempFile(temp_file_path, ios::binary);
                    if (tempFile.is_open()) {
                        stringstream buffer;
                        buffer << tempFile.rdbuf();
                        body = buffer.str();
                        tempFile.close();
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
                
            }
        }
        
        void HandleDELETE () {
            file_type = "application/json;";
            if (file_path.find("upload/") != string::npos) {
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
                if (code == 301) {
                    status_code = "301";
                    file_content = ReadFile();
                } else if (code == 400) {
                    status_code = "400";
                } else if (code == 401) {
                    status_code = "401";
                } else if (code == 404) {
                    status_code = "404";
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
            } else if (status_code == "301") {
                response += status_code;
                response += " Moved Permanently\r\n";
            } else if (status_code == "400") {
                response += status_code;
                response += " Bad Request\r\n";
            } else if (status_code == "401") {
                response += status_code;
                response += " Unauthorized\r\n";
            } else if (status_code == "404") {
                response += status_code;
                response += " Not Found\r\n";
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
            
            cout << "headers:" << endl << headers;
            cout << "body:" << endl << body.size() << " bytes" << endl;
            cout << endl;
            Check_Request();

            PraseCookie();
            Connection();
            
            file_path = UTF8ToBig5(file_path); 

            // 預設路徑
            if (file_path == "/") {
                file_path = "/index.html";
            }

            if (status_code == "200") {
                Method_choose();
            }

            // 建立 HTTP 回應
            string response = "HTTP/1.1 ";
            HTTP_code_respone(response);
            if (status_code == "301") {
                response += "Location: " + file_path + "\r\n";
            }
            response += "Content-Type: ";
            response += file_type;
            response += " charset=big5\r\n";

            if (!cookies_to_set.empty()) {
                response += cookies_to_set;
            }

            if (connect) {
                response += "Connection: keep-alive\r\n";
                response += "Keep-Alive: timeout=5, max=100\r\n";
            } else {
                response += "Connection: close\r\n";
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
            timeout.tv_sec = 120;
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
#include <winsock2.h>
#include <iostream>
#include <string>
#include <string.h>

using namespace std;

int main() {
    // 初始化 Winsock
    WSAData wsaData;
    WORD version = MAKEWORD(2, 2);
    int iResult = WSAStartup(version, &wsaData);
    if (iResult != 0) {
        cout << "Winsock failed: " << iResult << endl;
        string a;
        cout << "輸入已結束:";
        cin >> a;
        return 1;
    }
    cout << "Winsock initialized!" << endl;

    // 創建 socket
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        cout << "Socket creation failed! Error: " << WSAGetLastError() << endl;
        WSACleanup();
        string a;
        cout << "輸入已結束:";
        cin >> a;
        return 1;
    }
    cout << "Socket created!" << endl;

    // 設置伺服器地址
    SOCKADDR_IN serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddr.sin_port = htons(1234);

    // 連接到伺服器
    int connectResult = connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
    if (connectResult == SOCKET_ERROR) {
        cout << "Connection failed! Error: " << WSAGetLastError() << endl;
        closesocket(clientSocket);
        WSACleanup();
        string a;
        cout << "輸入已結束:";
        cin >> a;
        return 1;
    }
    cout << "Connected to server!" << endl;

    // 接收數據
    char message[200];
    ZeroMemory(message, 200);

    int bytesReceived = recv(clientSocket, message, sizeof(message) - 1, 0);
    if (bytesReceived > 0) {
        message[bytesReceived] = '\0'; // 確保字串正確結尾
        cout << "Received message: " << message << endl;
        cout << "Bytes received: " << bytesReceived << endl;
    } else if (bytesReceived == 0) {
        cout << "Connection closed by server." << endl;
    } else {
        cout << "Receive failed! Error: " << WSAGetLastError() << endl;
    }
    // 關閉連接
    closesocket(clientSocket);
    WSACleanup();

    string a;
    cout << "輸入已結束:";
    cin >> a;
    
    return 0;
}
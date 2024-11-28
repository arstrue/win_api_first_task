#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#define BUFF_SIZE 1000
#define PORT "51984"
#define IPv4ADDRESS "127.0.0.1"

DWORD WINAPI WriteToPipe(LPDWORD);
DWORD WINAPI ReadFromPipe(LPDWORD);
SOCKET serverSocket = NULL;

void CreateSocket(const CHAR* argv);
void ShutdownConnection(void);

void CreateSocket() {
    // Initialize Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        wprintf(L"WSAStartup function failed with error: %d\n", iResult);
        return 1;
    }
    struct addrinfo* result = NULL,
        * ptr = NULL,
        hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    iResult = getaddrinfo(IPv4ADDRESS, PORT, &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    ptr = result;

    // Create a SOCKET for connecting to server
    serverSocket = socket(ptr->ai_family, ptr->ai_socktype,
        ptr->ai_protocol);

    // Connect to server.
    iResult = connect(serverSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }
    freeaddrinfo(result);

    if (serverSocket == INVALID_SOCKET) {
        printf("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }

}

void ShutdownConnection(void) {
    DWORD iResult = shutdown(serverSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        exit(-1);
    }
    closesocket(serverSocket);
    WSACleanup();
}

DWORD WINAPI WriteToPipe(LPDWORD dummy) {
    UNREFERENCED_PARAMETER(dummy);
    DWORD iResult, dwRead, bSuccess;

    while(1) {
        CHAR chBuf[BUFF_SIZE] = "";

        bSuccess =
            ReadFile(GetStdHandle(STD_INPUT_HANDLE), chBuf, BUFF_SIZE, &dwRead, NULL);
        if (!bSuccess) {
            return 0;
        }

        iResult = send(serverSocket, chBuf, dwRead, 0);
        if (iResult == SOCKET_ERROR) {
            return -1;
        }
    }
}

DWORD WINAPI ReadFromPipe(LPDWORD dummy) {
    UNREFERENCED_PARAMETER(dummy);
    DWORD iResult, dwRead, bSuccess;

    while (1) {
        CHAR chBuf[BUFF_SIZE + 1] = "";

        iResult = recv(serverSocket, chBuf, BUFF_SIZE, 0);
        if (iResult > 0) {
            chBuf[strlen(chBuf)] = 0;
            bSuccess = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), chBuf,
                strlen(chBuf), &dwRead, NULL);
            if (!bSuccess) {
                return 0;
            }
        }
        else {
            return -1;
        }
    }
}

DWORD main() {
    CreateSocket();

    HANDLE threads[2];
    threads[0] = CreateThread(NULL, 0, WriteToPipe, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, ReadFromPipe, NULL, 0, NULL);

    WaitForMultipleObjects(2, threads, TRUE, INFINITE);

    for (int i = 0; i < 2; ++i)
        CloseHandle(threads[i]);

    ShutdownConnection();
    printf("Disconnected from server\n");

    return 0;
}
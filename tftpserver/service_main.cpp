#include "stdafx.h"
#include "headers.h"
#include <process.h>
#include <stdio.h>
#include "tftpserver.h"
#include "CTftpServer.h"
#include <vector>

char g_szWorkingDirectory[MAX_PATH];
char g_szIniPath[MAX_PATH];

HANDLE g_hListenEvent = NULL;
HANDLE g_hListenThread = NULL;
SOCKET g_socketListen;

std::vector<CTftpServer*> g_vecServs;

void DeleteClosedServer();

void ProcessNewConnect(SOCKET s)
{
	int ret = 0;
	SOCKADDR clientAddr;
	int nlen = sizeof(clientAddr);
	char szBuf[256] = { 0 };

	ret = recvfrom(g_socketListen, szBuf, sizeof(szBuf), 0,
		&clientAddr, &nlen);

	HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	CTftpServer* pServer = new CTftpServer(hEvent);
	g_vecServs.push_back(pServer);

	if (WAIT_OBJECT_0 == WaitForSingleObject(hEvent, 2000))
	{
		char szBuffer[128] = { 0 };
		sprintf(szBuffer, "%d", pServer->GetPort());
		LOG("bind port: %s", szBuffer);
		sendto(g_socketListen, szBuffer, strlen(szBuffer) + 1, 0, &clientAddr, nlen);
		CloseHandle(hEvent);
	}
}

DWORD __stdcall ListenThread(LPVOID lpParam)
{
	LOG("Listen thread start!");

	int iResult;
	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR) {
		LOG("WSAStartup failed!\n");
		return 0;
	}

	char szIp[40] = { 0 };
	char szPort[40] = { 0 };
	GetPrivateProfileStringA("TftpServer", "Address", "0.0.0.0", szIp, 40, g_szIniPath);
	GetPrivateProfileStringA("TftpServer", "FilePort", "69", szPort, 40, g_szIniPath);

	WritePrivateProfileStringA("TftpServer", "Address", szIp, g_szIniPath);
	WritePrivateProfileStringA("TftpServer", "FilePort", szPort, g_szIniPath);

	LOG("Local addr: %s:%s", szIp, szPort);

	SOCKADDR_IN listenAddr;
	listenAddr.sin_addr.S_un.S_addr = inet_addr(szIp);
	listenAddr.sin_family = AF_INET;
	listenAddr.sin_port = htons(atoi(szPort));


	g_socketListen = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	bind(g_socketListen, (SOCKADDR *)&listenAddr, sizeof(listenAddr));

	HANDLE hSocketEvent = WSACreateEvent();
	WSAEventSelect(g_socketListen, hSocketEvent, FD_READ);

	HANDLE hObjects[2];
	hObjects[0] = hSocketEvent;
	hObjects[1] = g_hListenEvent;

	while (true)
	{
		int Rc = WaitForMultipleObjects(2, hObjects, FALSE, 100);
		
		switch (Rc)
		{
		case WAIT_OBJECT_0:
			LOG("Received connect!");
			WSAEventSelect(g_socketListen, 0, 0);
			ProcessNewConnect(g_socketListen);
			ResetEvent(hSocketEvent);
			WSAEventSelect(g_socketListen, hSocketEvent, FD_READ);
			break;
		case (WAIT_OBJECT_0 + 1):
			LOG("Listen thread exit!");
			return 0;
			break;
		default:
			DeleteClosedServer();
			break;
		}
	}
	return 0;
}

void DeleteClosedServer()
{
	for (int i = g_vecServs.size() - 1; i >= 0; i--)
	{
		if (g_vecServs[i]->IsThreadExit())
		{
			delete g_vecServs[i];
			g_vecServs.erase(g_vecServs.begin() + i);
		}
	}
}

void StartTftpServices(const char* szIniPath)
{
	strcpy(g_szIniPath, szIniPath);
	
	char szPath[MAX_PATH] = { 0 };
	GetPrivateProfileStringA("TftpServer", "WorkingDirectory", "D:\\", szPath, MAX_PATH, g_szIniPath);
	SetWorkDirectory(szPath);

	g_hListenEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	g_hListenThread = CreateThread(NULL, 0, ListenThread, NULL, 0, NULL);

} // StartTftpd32Services

void StopTftpServices(void)
{
	if (g_hListenEvent && g_hListenThread)
	{
		SetEvent(g_hListenEvent);
		WaitForSingleObject(g_hListenThread, INFINITE);
		CloseHandle(g_hListenThread);
		CloseHandle(g_hListenEvent);
		g_hListenEvent = NULL;
		g_hListenThread = NULL;
		for (int i = g_vecServs.size() - 1; i >= 0; i--)
		{
			delete g_vecServs[i];
			g_vecServs.erase(g_vecServs.begin() + i);
		}
	}
}

void SetWorkDirectory(const char* szPath)
{
	WritePrivateProfileStringA("TftpServer", "WorkingDirectory", szPath, g_szIniPath);
	strncpy(g_szWorkingDirectory, szPath, MAX_PATH);
}

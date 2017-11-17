//////////////////////////////////////////////////////
//
// Projet TFTPD32.   Feb 99 By  Ph.jounin
// File start_threads.c:  Thread management
//
// The main function of the service
//
// source released under European Union Public License
//
//////////////////////////////////////////////////////

#include "stdafx.h"
#include "headers.h"
#include <process.h>
#include "threading.h"
#include <stdio.h>
#include "tftpserver.h"
#include "CTftpServer.h"

CTftpServer* pServer;

void StartTftpd32Services(const char* szIniPath)
{
	// starts worker threads
	int iResult;
	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR) {
		OutputDebugStringA("WSAStartup failed!\n");
		return;
	}
	char szPath[MAX_PATH] = { 0 };
	GetPrivateProfileStringA("TftpServer", "WorkingDirectory", sSettings.szWorkingDirectory, szPath, MAX_PATH, szIniPath);
	SetWorkDirectory(szPath);

	SOCKADDR_IN addr;
	addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	addr.sin_family = AF_INET;
	addr.sin_port = htons(69);

	pServer = new CTftpServer(addr);
	WritePrivateProfileStringA("TftpServer", "WorkingDirectory", szPath, szIniPath);
} // StartTftpd32Services

void StopTftpd32Services(void)
{
	//TerminateWorkerThreads(FALSE);
}

void SetWorkDirectory(const char* szPath)
{
	//WritePrivateProfileStringA("TftpServer", "WorkingDirectory", szPath, INI_FILE);
	//strncpy(sSettings.szWorkingDirectory, szPath, MAX_PATH);
}

  // send data using Udp
int UdpSend(int nFromPort, struct sockaddr *sa_to, int sa_len, const char *data, int len)
{
	SOCKET              s;
	SOCKADDR_STORAGE    sa_from;
	int                 Rc;
	int                 True = 1;
	char                szServ[NI_MAXSERV];
	ADDRINFO            Hints, *res;

	// populate sa_from
	memset(&sa_from, 0, sizeof sa_from);
	Hints.ai_family = sa_to->sa_family;
	Hints.ai_flags = NI_NUMERICSERV;
	Hints.ai_socktype = SOCK_DGRAM;
	Hints.ai_protocol = IPPROTO_UDP;
	sprintf(szServ, "%d", nFromPort);
	Rc = getaddrinfo(NULL, szServ, &Hints, &res);

	s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s == INVALID_SOCKET)  return -1;
	// REUSEADDR option in order to allow thread to open 69 port
	Rc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)& True, sizeof True);
	LOG(Rc == 0 ? "UdpSend: Port %d may be reused" : "setsockopt error", nFromPort);

	Rc = bind(s, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);

	LOG("UdpSend bind returns %d (error %d)", Rc, GetLastError());
	if (Rc < 0) { closesocket(s); return -1; }

	Rc = sendto(s, data, len, 0, sa_to, sa_len);
	LOG("sendto returns %d", Rc);
	closesocket(s);
	return Rc;
} // UdpSend


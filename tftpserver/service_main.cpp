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


void StartTftpd32Services(void)
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
	GetPrivateProfileStringA("TftpServer", "WorkingDirectory", sSettings.szWorkingDirectory, szPath, MAX_PATH, INI_FILE);
	SetWorkDirectory(szPath);
	StartMultiWorkerThreads(FALSE);
	WritePrivateProfileStringA("TftpServer", "WorkingDirectory", szPath, INI_FILE);
	LogToMonitor("Worker threads started\n");
} // StartTftpd32Services

void StopTftpd32Services(void)
{
	TerminateWorkerThreads(FALSE);
}

void SetWorkDirectory(const char* szPath)
{
	WritePrivateProfileStringA("TftpServer", "WorkingDirectory", szPath, INI_FILE);
	strncpy(sSettings.szWorkingDirectory, szPath, MAX_PATH);
}

// Function LastErrorText
// A wrapper for FormatMessage : retrieve the message text for a system-defined error 
char *LastErrorText(void)
{
	static char szLastErrorText[512];
	sprintf(szLastErrorText, "Last error: %d\n", GetLastError());
	//LPVOID      lpMsgBuf = NULL;
	//LPSTR       p;

	//FormatMessageA(
	//	FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
	//	NULL,
	//	GetLastError(),
	//	MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
	//	(char*)lpMsgBuf,
	//	0,
	//	NULL);
	//memset(szLastErrorText, 0, sizeof szLastErrorText);
	//strncpy(szLastErrorText, (char*)lpMsgBuf, sizeof szLastErrorText);
	//// Free the buffer.
	//LocalFree(lpMsgBuf);
	//// remove ending \r\n
	//p = strchr(szLastErrorText, '\r');
	//if (p != NULL)  *p = 0;
	return szLastErrorText;
} // LastErrorText


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
	LogToMonitor(Rc == 0 ? "UdpSend: Port %d may be reused" : "setsockopt error", nFromPort);

	Rc = bind(s, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);

	LogToMonitor("UdpSend bind returns %d (error %d)", Rc, GetLastError());
	if (Rc < 0) { closesocket(s); return -1; }

	Rc = sendto(s, data, len, 0, sa_to, sa_len);
	LogToMonitor("sendto returns %d", Rc);
	closesocket(s);
	return Rc;
} // UdpSend


void LogToMonitor(char *fmt, ...)
{
	va_list args;
	char sz[LOGSIZE];
	int n;

	sz[sizeof sz - 1] = 0;
	va_start(args, fmt);
	n = sprintf(sz, "Th%5d :", GetCurrentThreadId());
	vsprintf(&sz[n], fmt, args);
	OutputDebugStringA(sz);
}

void LOG(int DebugLevel, const char *szFmt, ...)
{
	va_list args;
	char sz[LOGSIZE];
	int n;

	sz[sizeof sz - 1] = 0;
	va_start(args, szFmt);
	n = sprintf(sz, "Th%5d :", GetCurrentThreadId());
	vsprintf(&sz[n], szFmt, args);
	OutputDebugStringA(sz);
}

void SVC_ERROR(const char *szFmt, ...)
{
	va_list args;
	char sz[LOGSIZE];
	int n;

	sz[sizeof sz - 1] = 0;
	va_start(args, szFmt);
	n = sprintf(sz, "Th%5d :", GetCurrentThreadId());
	vsprintf(&sz[n], szFmt, args);
	OutputDebugStringA(sz);
}
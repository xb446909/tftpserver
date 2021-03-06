#pragma once

#include <WinSock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include "Tftp.h"
#include "tftp_struct.h"
#include "tftpd_thread.h"

class CTftpServer
{
public:
	typedef struct tagThreadParam
	{
		SOCKET socket;
		HANDLE hEvent;
		HANDLE hEstablish;
		TftpInfo tftp;
	}ThreadParam;
	static void DebugString(char *fmt, ...);
	CTftpServer(HANDLE hEvent);
	BOOL IsThreadExit();
	~CTftpServer();
	int GetPort();
private:
	HANDLE m_hThread;
	ThreadParam m_threadParam;
};


#define LOG(...)	CTftpServer::DebugString(##__VA_ARGS__)

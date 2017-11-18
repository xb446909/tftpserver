#include "StdAfx.h"
#include "CTftpServer.h"
#include <stdio.h>
#include <stdlib.h>

DWORD __stdcall TftpProc(LPVOID lpParam);
int RecvNewSocket(LPVOID lpParam);
int StartTftpTransfer(LPVOID lpParam);

CTftpServer::CTftpServer(HANDLE hEvent)
	: m_hThread(INVALID_HANDLE_VALUE)
{
	m_threadParam.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	SOCKADDR_IN bindAddr;
	bindAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(0);
	if (bind(m_threadParam.socket, (SOCKADDR*)&bindAddr, sizeof(bindAddr)) == SOCKET_ERROR)
	{
		LOG("bind error: %d", WSAGetLastError());
		return;
	}
	m_threadParam.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_threadParam.hEstablish = hEvent;
	m_hThread = CreateThread(NULL, 0, TftpProc, &m_threadParam, 0, NULL);
}

BOOL CTftpServer::IsThreadExit()
{
	return (WaitForSingleObject(m_hThread, 0) == WAIT_OBJECT_0);
}

CTftpServer::~CTftpServer()
{
	SetEvent(m_threadParam.hEvent);
	WaitForSingleObject(m_hThread, INFINITE);
	CloseHandle(m_hThread);
	CloseHandle(m_threadParam.hEvent);

	LOG("Server deleted!");
}

int CTftpServer::GetPort()
{
	struct sockaddr_storage s_in;
	int s_len = sizeof(s_in);
	char szServ[NI_MAXSERV];
	getsockname(m_threadParam.socket, (struct sockaddr *) & s_in, &s_len);
	getnameinfo((struct sockaddr *) & s_in, sizeof s_in, NULL, 0, szServ, sizeof szServ, NI_NUMERICSERV);
	return atoi(szServ);
}

void CTftpServer::DebugString(char * fmt, ...)
{
	va_list args;
	char sz[512];
	int n;

	sz[sizeof sz - 1] = 0;
	va_start(args, fmt);
	n = sprintf(sz, "Th%5d :", GetCurrentThreadId());
	vsprintf(&sz[n], fmt, args);
	va_end(args);
	strcat(sz, "\n");
	printf(sz);
	OutputDebugStringA(sz);
}


DWORD __stdcall TftpProc(LPVOID lpParam)
{
	LOG("Start tftp main thread!");
	CTftpServer::ThreadParam* pParam = (CTftpServer::ThreadParam*)lpParam;
	HANDLE hSocketEvent = WSACreateEvent();
	WSAEventSelect(pParam->socket, hSocketEvent, FD_READ);
	
	HANDLE hObjects[2];
	hObjects[0] = hSocketEvent;
	hObjects[1] = pParam->hEvent;

	SetEvent(pParam->hEstablish);

	LOG("Receive tftp is ready!");

	int Rc = WaitForMultipleObjects(2, hObjects, FALSE, 5000);
	switch (Rc)
	{
	case WAIT_OBJECT_0:
		LOG("Received tftp!");
		WSAEventSelect(pParam->socket, 0, 0);
		RecvNewSocket(lpParam);
		ResetEvent(hSocketEvent);
		break;
	case (WAIT_OBJECT_0 + 1):
		LOG("Received thread exit event!");
		break;
	default:
		break;
	}

	LOG("End tftp main thread!");
	return 0;
}


int RecvNewSocket(LPVOID lpParam)
{
	CTftpServer::ThreadParam* pParam = (CTftpServer::ThreadParam*)lpParam;
	TftpInfo* pTftp = &pParam->tftp;

	memset(&pTftp->st, 0, sizeof pTftp->st);
	memset(&pTftp->b, 0, sizeof pTftp->b);
	time(&pTftp->st.StartTime);

	int fromlen = sizeof pTftp->b.cnx_frame;
	int Rc = recvfrom(pParam->socket, pTftp->b.cnx_frame, sizeof pTftp->b.cnx_frame, 0,
		(struct sockaddr *)&pTftp->b.from, &fromlen);

	if (Rc < 0)
	{
		LOG("Receive error: %d", WSAGetLastError());
	}
	else if (Rc > PKTSIZE)
	{
		LOG("Error: Received too much bytes!");
	}
	else
	{
		char szAddr[40], szServ[NI_MAXSERV];
		getnameinfo((LPSOCKADDR)& pTftp->b.from, sizeof pTftp->b.from,
			szAddr, sizeof szAddr,
			szServ, sizeof szServ,
			NI_NUMERICHOST | AI_NUMERICSERV);

		StartTftpTransfer(lpParam);
	}
	return 0;
}


int StartTftpTransfer(LPVOID lpParam)
{
	CTftpServer::ThreadParam* pParam = (CTftpServer::ThreadParam*)lpParam;
	TftpInfo* pTftp = &pParam->tftp;

	int Rc;
	if ((Rc = connect(pParam->socket, (struct sockaddr *) & pTftp->b.from, sizeof pTftp->b.from)) != 0)
	{
		Rc = GetLastError();
		LOG("Error : connect returns %d", Rc);
		Sleep(1000);
	}
	else if ((Rc = DecodConnectData(pTftp)) != CNX_FAILED)
	{
		int bSuccess;
		struct sockaddr_storage s_in;
		int s_len = sizeof(s_in);
		char szServ[NI_MAXSERV];
		getsockname(pParam->socket, (struct sockaddr *) & s_in, &s_len);
		getnameinfo((struct sockaddr *) & s_in, sizeof s_in, NULL, 0, szServ, sizeof szServ, NI_NUMERICSERV);
		LOG("Using local port %s, Remote port: %d, Rc: %d", szServ, ntohs(((struct sockaddr_in *) (&pTftp->b.from))->sin_port), Rc);

		pTftp->st.ret_code = TFTP_TRF_RUNNING;
		pTftp->r.skt = pParam->socket;

		// everything OK so far
		switch (Rc)
		{
			// download RRQ
		case CNX_OACKTOSENT_RRQ:
			if (TftpSendOack(pTftp))  bSuccess = TftpSendFile(pTftp);
			break;
		case CNX_SENDFILE:
			bSuccess = TftpSendFile(pTftp);
			break;
			// upload WRQ
		case CNX_OACKTOSENT_WRQ:
			if (TftpSendOack(pTftp))  bSuccess = TftpRecvFile(pTftp, TRUE);
			break;
		case CNX_ACKTOSEND:
			bSuccess = TftpRecvFile(pTftp, FALSE);
			break;
		default:
			break;
		}

		pTftp->st.ret_code = bSuccess ? TFTP_TRF_SUCCESS : TFTP_TRF_ERROR;

	}
	return 0;
}
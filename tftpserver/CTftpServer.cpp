#include "StdAfx.h"
#include "CTftpServer.h"
#include <stdio.h>

DWORD __stdcall TftpProc(LPVOID lpParam);
int RecvNewSocket(LPVOID lpParam);
int StartTftpTransfer(LPVOID lpParam);

CTftpServer::CTftpServer(SOCKADDR_IN addr)
	: m_hThread(INVALID_HANDLE_VALUE)
{
	m_threadParam.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (bind(m_threadParam.socket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		LOG("bind error: %d\n", WSAGetLastError());
		return;
	}
	m_threadParam.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hThread = CreateThread(NULL, 0, TftpProc, &m_threadParam, 0, NULL);
}


CTftpServer::~CTftpServer()
{
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
	OutputDebugStringA(sz);
}


DWORD __stdcall TftpProc(LPVOID lpParam)
{
	LOG("Start tftp main thread!\n");
	CTftpServer::ThreadParam* pParam = (CTftpServer::ThreadParam*)lpParam;
	HANDLE hSocketEvent = WSACreateEvent();
	WSAEventSelect(pParam->socket, hSocketEvent, FD_READ);
	
	HANDLE hObjects[2];
	hObjects[0] = hSocketEvent;
	hObjects[1] = pParam->hEvent;

	while (true)
	{
		int Rc = WaitForMultipleObjects(2, hObjects, FALSE, 2000);
		switch (Rc)
		{
		case WAIT_OBJECT_0:
			LOG("Received socket!\n");
			RecvNewSocket(lpParam);
			break;
		case (WAIT_OBJECT_0 + 1):
			LOG("Received thread exit event!\n");
			break;
		default:
			break;
		}
	}
}


int RecvNewSocket(LPVOID lpParam)
{
	CTftpServer::ThreadParam* pParam = (CTftpServer::ThreadParam*)lpParam;
	TftpInfo* pTftp = &pParam->tftp;

	int fromlen = sizeof pTftp->b.cnx_frame;
	int Rc = recvfrom(pParam->socket, pTftp->b.cnx_frame, sizeof pTftp->b.cnx_frame, 0,
		(struct sockaddr *)&pTftp->b.from, &fromlen);

	if (Rc < 0)
	{
		LOG("Receive error: %d\n", WSAGetLastError());
	}
	else if (Rc > PKTSIZE)
	{
		LOG("Error: Received too much bytes!\n");
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
		LOG("Error : connect returns %d\n", Rc);
		Sleep(1000);
	}
	else if ((Rc = DecodConnectData(pTftp)) != CNX_FAILED)
	{
		int bSuccess;
		struct sockaddr_storage s_in;
		int s_len = sizeof(s_in);
		char szServ[NI_MAXSERV];
		getsockname(pTftp->r.skt, (struct sockaddr *) & s_in, &s_len);
		getnameinfo((struct sockaddr *) & s_in, sizeof s_in, NULL, 0, szServ, sizeof szServ, NI_NUMERICSERV);
		LOG("Using local port %s, Remote port: %d, Rc: %d", szServ, ntohs(((struct sockaddr_in *) (&pTftp->b.from))->sin_port), Rc);

		pTftp->st.ret_code = TFTP_TRF_RUNNING;

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
//////////////////////////////////////////////////////
//
// Projet TFTPD32.  Mai 98 Ph.jounin - June 2006
// File tftp.c:   worker threads management
//
// source released under European Union Public License
//
//////////////////////////////////////////////////////
#include "stdafx.h"


#define TFTP_MAXTHREADS     100

#undef min
#define min(a,b)  (((a) < (b)) ? (a) : (b))


// #define DEB_TEST
#include "headers.h"
#include <process.h>
#include <stdio.h>

#include "threading.h"
#include "tftpd_functions.h"
#include "winsock2.h"


// First item -> structure belongs to the module and is shared by all threads
struct LL_TftpInfo *pTftpFirst;
static int gSendFullStat = FALSE;		// full report should be sent




// statistics requested by console 
// do not answer immediately since we are in console thread
// and pTftp data may change
void ConsoleTftpGetStatistics(void)
{
	gSendFullStat = TRUE;
} // 



////////////////////////////////////////////////////////////
// TFTP daemon --> Runs at main level
////////////////////////////////////////////////////////////



static SOCKET TftpBindLocalInterface(const char* szIniPath)
{
	char strPort[64] = { 0 };
	char strDefPort[64] = { 0 };
	char strIp[64] = { 0 };
	char strApp[128] = { 0 };
	sprintf(strDefPort, "%d", TFTP_DEFPORT);
	GetPrivateProfileStringA("TftpServer", "Address", TFTP_DEFADDR, strIp, 63, szIniPath);
	WritePrivateProfileStringA("TftpServer", "Address", strIp, szIniPath);
	GetPrivateProfileStringA("TftpServer", "Port", strDefPort, strPort, 63, szIniPath);
	WritePrivateProfileStringA("TftpServer", "Port", strPort, szIniPath);

	sSettings.Port = atoi(strPort);

	SOCKADDR_IN service;
	service.sin_family = AF_INET;
	service.sin_addr.s_addr = inet_addr(strIp);
	service.sin_port = htons(atoi(strPort));
	SOCKET sListenSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int Rc = bind(sListenSocket, (SOCKADDR*)&service, sizeof(service));

	return   Rc == INVALID_SOCKET ? Rc : sListenSocket;

} // TftpBindLocalInterface


static void PopulateTftpdStruct(struct LL_TftpInfo *pTftp)
{
	struct LL_TftpInfo *pTmp;
	static DWORD TransferId = 467;    // unique identifiant

		// init or reinit struct
	pTftp->s.dwTimeout = sSettings.Timeout;
	pTftp->s.dwPacketSize = TFTP_SEGSIZE;  // default
	pTftp->r.skt = INVALID_SOCKET;
	pTftp->r.hFile = INVALID_HANDLE_VALUE;
	pTftp->c.bMCast = FALSE;    // may be updated later
	pTftp->c.nOAckPort = 0;		// use classical port for OAck
	pTftp->tm.dwTransferId = TransferId++;

	// init statistics
	memset(&pTftp->st, 0, sizeof pTftp->st);
	time(&pTftp->st.StartTime);
	pTftp->st.dLastUpdate = pTftp->st.StartTime;
	pTftp->st.ret_code = TFTP_TRF_RUNNING;
	// count the transfers (base 0)
	for (pTftp->st.dwTransfert = 0, pTmp = pTftpFirst->next;
		pTmp != NULL;
		pTmp = pTmp->next, pTftp->st.dwTransfert++);

	// clear buffers
	memset(&pTftp->b, 0, sizeof pTftp->b);
} // PopulateTftpdStruct

// Suppress structure item
static struct LL_TftpInfo *TftpdDestroyThreadItem(struct LL_TftpInfo *pTftp)
{
	struct LL_TftpInfo *pTmp = pTftp;

	//LOG (9, "thread %d has exited", pTftp->tm.dwThreadHandle);

	LogToMonitor("removing thread %d (%p/%p/%p)\n", pTftp->tm.dwThreadHandleId, pTftp, pTftpFirst, pTftpFirst->next);
	// do not cancel permanent Thread
	if (!pTftp->tm.bPermanentThread)
	{
		if (pTftp != pTftpFirst)
		{
			// search for the previous struct
			for (pTmp = pTftpFirst; pTmp->next != NULL && pTmp->next != pTftp; pTmp = pTmp->next);
			pTmp->next = pTftp->next;   // detach the struct from list
		}
		else pTftpFirst = pTmp = pTftpFirst->next;

		memset(pTftp, 0xAA, sizeof *pTftp); // fill with something is a good debugging tip
		free(pTftp);
	}

	return pTmp;	// pointer on previous item
} // TftpdDestroyThreadItem


// --------------------------------------------------------
// Filter incoming request
// add-on created on 24 April 2008
// return TRUE if message should be filtered
// --------------------------------------------------------
static int TftpMainFilter(SOCKADDR_STORAGE *from, int from_len, char *data, int len)
{
	static char LastMsg[PKTSIZE];
	static int  LastMsgSize;
	static time_t LastDate;
	static SOCKADDR_STORAGE LastFrom;

	if (len > PKTSIZE) return TRUE;	// packet should really be dropped
	// test only duplicated packets
	if (len == LastMsgSize
		&&  memcmp(data, LastMsg, len) == 0
		&& memcmp(from, &LastFrom, from_len) == 0
		&& time(NULL) == LastDate)
	{
		char szAddr[MAXLEN_IPv6] = "", szServ[NI_MAXSERV] = "";
		int Rc;
		Rc = getnameinfo((LPSOCKADDR)from, sizeof from,
			szAddr, sizeof szAddr,
			szServ, sizeof szServ,
			NI_NUMERICHOST | NI_NUMERICSERV);

		//LOG (1, "Warning : received duplicated request from %s:%s", szAddr, szServ);
		Sleep(250);	// time for the first TFTP thread to start
		return FALSE;	// accept message nevertheless
	}
	// save last frame

	LastMsgSize = len;
	memcpy(LastMsg, data, len);
	LastFrom = *from;
	time(&LastDate);
	return FALSE; // packet is OK
} // TftpMainFilter


// activate a new thread and pass control to it 
static int TftpdChooseNewThread(SOCKET sListenerSocket)
{
	struct LL_TftpInfo *pTftp, *pTmp;
	int             fromlen;
	int             bNewThread;
	int             Rc;
	int             nThread = 0;

	for (pTmp = pTftpFirst; pTmp != NULL; pTmp = pTmp->next)
		nThread++;
	// if max thread reach read datagram and quit
	if (nThread >= sSettings.dwMaxTftpTransfers)
	{
		char dummy_buf[PKTSIZE];
		SOCKADDR_STORAGE    from;
		fromlen = sizeof from;
		// Read the connect datagram to empty queue
		Rc = recvfrom(sListenerSocket, dummy_buf, sizeof dummy_buf, 0,
			(struct sockaddr *) & from, &fromlen);
		if (Rc > 0)
		{
			char szAddr[MAXLEN_IPv6];
			getnameinfo((LPSOCKADDR)& from, sizeof from,
				szAddr, sizeof szAddr,
				NULL, 0,
				NI_NUMERICHOST);
			//LOG (1, "max number of threads reached, connection from %s dropped", szAddr );
		}
		return -1;
	}

	// search a permanent thread in waiting state
	for (pTftp = pTftpFirst; pTftp != NULL; pTftp = pTftp->next)
		if (pTftp->tm.bPermanentThread && !pTftp->tm.bActive)  break;
	bNewThread = (pTftp == NULL);

	if (bNewThread)
	{
		// search for the last thread struct
		for (pTftp = pTftpFirst; pTftp != NULL && pTftp->next != NULL; pTftp = pTftp->next);
		if (pTftp == NULL)   pTftp = pTftpFirst = (struct LL_TftpInfo *)calloc(1, sizeof *pTftpFirst);
		else               pTftp = pTftp->next = (struct LL_TftpInfo *)calloc(1, sizeof *pTftpFirst);
		// note due the calloc if thread has just been created
		//   pTftp->tm.dwThreadHandle == NULL ;
		pTftp->next = NULL;
	}

	PopulateTftpdStruct(pTftp);

	// Read the connect datagram (since this use a "global socket" port 69 its done here)
	fromlen = sizeof pTftp->b.cnx_frame;
	Rc = recvfrom(sListenerSocket, pTftp->b.cnx_frame, sizeof pTftp->b.cnx_frame, 0,
		(struct sockaddr *)&pTftp->b.from, &fromlen);
	if (Rc < 0)
	{
		// the Tftp structure has been created --> suppress it
		//LOG (0, "Error : RecvFrom returns %d: <%s>", WSAGetLastError(), LastErrorText());
		if (!pTftp->tm.bPermanentThread)
		{
			// search for the last thread struct
			for (pTmp = pTftpFirst; pTmp->next != pTftp; pTmp = pTmp->next);
			pTmp->next = pTftp->next; // remove pTftp from linked list
			free(pTftp);
		}
	}
	// should the message be silently dropped
	else if (TftpMainFilter(&pTftp->b.from, sizeof pTftp->b.from, pTftp->b.cnx_frame, Rc))
	{
		char szAddr[MAXLEN_IPv6];
		getnameinfo((LPSOCKADDR)& pTftp->b.from, sizeof pTftp->b.from,
			szAddr, sizeof szAddr,
			NULL, 0,
			NI_NUMERICHOST | AI_NUMERICSERV);
		// If this is an IPv4-mapped IPv6 address; drop the leading part of the address string so we're left with the familiar IPv4 format.
		// Hack copied from the Apache source code
		if (pTftp->b.from.ss_family == AF_INET6
			&& IN6_IS_ADDR_V4MAPPED(&(*(struct sockaddr_in6 *) & pTftp->b.from).sin6_addr))
		{
			memmove(szAddr, szAddr + sizeof("::ffff:") - 1, strlen(szAddr + sizeof("::ffff:") - 1) + 1);
		}
		LOG (1, "Warning : Unaccepted request received from %s", szAddr);
		// the Tftp structure has been created --> suppress it
		if (!pTftp->tm.bPermanentThread)
		{
			// search for the last thread struct
			for (pTmp = pTftpFirst; pTmp->next != pTftp; pTmp = pTmp->next);
			pTmp->next = pTftp->next; // remove pTftp from linked list
			free(pTftp);
		}
	}
	else	// message is accepted
	{
		char szAddr[MAXLEN_IPv6], szServ[NI_MAXSERV];
		getnameinfo((LPSOCKADDR)& pTftp->b.from, sizeof pTftp->b.from,
			szAddr, sizeof szAddr,
			szServ, sizeof szServ,
			NI_NUMERICHOST | AI_NUMERICSERV);
		// If this is an IPv4-mapped IPv6 address; drop the leading part of the address string so we're left with the familiar IPv4 format.
		if (pTftp->b.from.ss_family == AF_INET6
			&& IN6_IS_ADDR_V4MAPPED(&(*(struct sockaddr_in6 *) & pTftp->b.from).sin6_addr))
		{
			memmove(szAddr, szAddr + sizeof("::ffff:") - 1, strlen(szAddr + sizeof("::ffff:") - 1) + 1);
		}
		//LOG (1, "Connection received from %s on port %s", szAddr, szServ);
#if (defined DEBUG || defined DEB_TEST)
		BinDump(pTftp->b.cnx_frame, Rc, "Connect:");
#endif		

		// mark thread as started (will not be reused)
		pTftp->tm.bActive = TRUE;

		// start new thread or wake up permanent one
		if (bNewThread)
		{
			pTftp->r.skt = sListenerSocket;
			// create the worker thread
			// pTftp->tm.dwThreadHandle = (HANDLE) _beginthread (StartTftpTransfer, 8192, (void *) pTftp);
			StartTftpTransfer(pTftp);
			//pTftp->tm.dwThreadHandle = CreateThread(NULL,
			//	8192,
			//	StartTftpTransfer,
			//	pTftp,
			//	0,
			//	&pTftp->tm.dwThreadHandleId);
			//LogToMonitor("Thread %d transfer %d started (records %p/%p)\n", pTftp->tm.dwThreadHandleId, pTftp->tm.dwTransferId, pTftpFirst, pTftp);
			//LOG (9, "thread %d started", pTftp->tm.dwThreadHandle);

		}
		else                 // Start the thread
		{
			LogToMonitor("waking up thread %d for transfer %d\n",
				pTftp->tm.dwThreadHandleId,
				pTftp->tm.dwTransferId);
			if (pTftp->tm.hEvent != NULL)       SetEvent(pTftp->tm.hEvent);
		}
		// Put the multicast hook which adds the new client if the same mcast transfer
		// is already in progress

	} // recv ok --> thread has been started

	return TRUE;
} // TftpdStartNewThread


static int TftpdCleanup(SOCKET sListenerSocket)
{
	struct LL_TftpInfo *pTftp, *pTmp;
	// suspend all threads
	for (pTftp = pTftpFirst; pTftp != NULL; pTftp = pTftp->next)
		if (pTftp->tm.dwThreadHandle != NULL) SuspendThread(pTftp->tm.dwThreadHandle);

	if (WSAIsBlocking())  WSACancelBlockingCall();   // the thread is probably blocked into a select

	// then frees resources
	for (pTftp = pTftpFirst; pTftp != NULL; pTftp = pTmp)
	{
		pTmp = pTftp->next;

		if (pTftp->r.skt != INVALID_SOCKET)          closesocket(pTftp->r.skt), pTftp->r.skt = INVALID_SOCKET;
		if (pTftp->r.hFile != INVALID_HANDLE_VALUE)  CloseHandle(pTftp->r.hFile), pTftp->r.hFile != INVALID_HANDLE_VALUE;
		if (pTftp->tm.hEvent != NULL)                CloseHandle(pTftp->tm.hEvent), pTftp->tm.hEvent != NULL;
		free(pTftp);
	}
	Sleep(100);
	// kill the threads
	for (pTftp = pTftpFirst; pTftp != NULL; pTftp = pTftp->next)
		if (pTftp->tm.dwThreadHandle != NULL) TerminateThread(pTftp->tm.dwThreadHandle, 0);

	// close main socket
	closesocket(sListenerSocket);
	return TRUE;
} // TftpdCleanup


// a watch dog which reset the socket event if data are available
static int ResetSockEvent(SOCKET s, HANDLE hEv)
{
	u_long dwData;
	int Rc;
	Rc = ioctlsocket(s, FIONREAD, &dwData);
	if (dwData == 0) ResetEvent(hEv);
	return Rc;
}


// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------
void TftpdMain(void *param)
{
	LogToMonitor("Start tftp main thread!\n");
	int Rc;
	int parse;
	HANDLE hSocketEvent = INVALID_HANDLE_VALUE;
	struct LL_TftpInfo *pTftp;
	// events : either socket event or wake up by another thread
	enum { E_TFTP_SOCK = 0, E_TFTP_WAKE, E_TFTP_EV_NB };
	HANDLE tObjects[E_TFTP_EV_NB];

	// creates socket and starts permanent threads
		//if (pTftpFirst==NULL)  CreatePermanentThreads ();


	tThreads[TH_TFTP].bInit = TRUE;  // inits OK

   // Socket was not opened at the start since we have to use interface 
   // once an address as been assigned
	while (tThreads[TH_TFTP].gRunning)
	{
		// if socket as not been created before
		if (tThreads[TH_TFTP].skt == INVALID_SOCKET)
		{
			tThreads[TH_TFTP].skt = TftpBindLocalInterface();
		} // open the socket
		if (tThreads[TH_TFTP].skt == INVALID_SOCKET)
		{
			break;
		}

		// create event for the incoming Socket
		hSocketEvent = WSACreateEvent();
		Rc = WSAEventSelect(tThreads[TH_TFTP].skt, hSocketEvent, FD_READ);
		Rc = GetLastError();

		tObjects[E_TFTP_SOCK] = hSocketEvent;
		tObjects[E_TFTP_WAKE] = tThreads[TH_TFTP].hEv;

		// stop only when TFTP is stopped and all threads have returned

		// waits for either incoming connection or thread event
		Rc = WaitForMultipleObjects(E_TFTP_EV_NB,
			tObjects,
			FALSE,
			sSettings.dwRefreshInterval);
#ifdef RT                                      
		if (Rc != WAIT_TIMEOUT) LogToMonitor("exit wait, object %d\n", Rc);
#endif
		if (!tThreads[TH_TFTP].gRunning) break;

		switch (Rc)
		{
		case E_TFTP_WAKE:   // a thread has exited
								// update table
			do
			{
				parse = FALSE;
				for (pTftp = pTftpFirst; pTftp != NULL; pTftp = pTftp->next)
				{
					if (!pTftp->tm.bPermanentThread && !pTftp->tm.bActive)
					{
						if (pTftp == NULL) { LogToMonitor("NULL POINTER pTftpFirst:%p\n", pTftpFirst); Sleep(5000); break; }
						CloseHandle(pTftp->tm.dwThreadHandle);
						pTftp = TftpdDestroyThreadItem(pTftp);
						parse = TRUE;
						break;
					} // thread no more active
				}
			} // parse all threads (due to race conditions, we can have only one event)
			while (parse);
			break;

			// we have received a message on the port 69
		case E_TFTP_SOCK:    // Socket Msg
			WSAEventSelect(tThreads[TH_TFTP].skt, 0, 0);
			TftpdChooseNewThread(tThreads[TH_TFTP].skt);
			ResetEvent(hSocketEvent);
			WSAEventSelect(tThreads[TH_TFTP].skt, hSocketEvent, FD_READ);
			// ResetSockEvent (sListenerSocket, hSocketEvent);
			break;

		case  WAIT_TIMEOUT:
			gSendFullStat = FALSE;         // reset full stat flag
			// ResetSockEvent (sListenerSocket, hSocketEvent);
			break;
		case -1:
			LogToMonitor("WaitForMultipleObjects error %d\n", GetLastError());
			//LOG (1, "WaitForMultipleObjects error %d", GetLastError());
			break;
		}   // switch


		CloseHandle(hSocketEvent);

	} // endless loop



	// TftpdCleanup (sListenerSocket, hSemaphore);

	// Should the main thread kill other threads ?
	if (tThreads[TH_TFTP].bSoftReset)
		LogToMonitor("do NOT signal worker threads\n");
	else
	{
		LogToMonitor("signalling worker threads\n");
		/////////////////////////////////
		// wait for end of worker threads
		for (pTftp = pTftpFirst; pTftp != NULL; pTftp = pTftp->next)
		{
			if (pTftp->tm.bActive)                nak(pTftp, ECANCELLED);
			else if (pTftp->tm.bPermanentThread)  SetEvent(pTftp->tm.hEvent);
		}
		LogToMonitor("waiting for worker threads\n");

		while (pTftpFirst != NULL)
		{
			WaitForSingleObject(pTftpFirst->tm.dwThreadHandle, 10000);
			LogToMonitor("End of thread %d\n", pTftpFirst->tm.dwThreadHandleId);
			pTftpFirst->tm.bPermanentThread = FALSE;
			TftpdDestroyThreadItem(pTftpFirst);
		}
	} // Terminate sub threads

	Rc = closesocket(tThreads[TH_TFTP].skt);
	tThreads[TH_TFTP].skt = INVALID_SOCKET;
	WSACloseEvent(hSocketEvent);

	LogToMonitor("main TFTP thread ends here\n");
	_endthread();
} // Tftpd main thread


void ScanDir(HANDLE hFile, const char *szDirectory)
{
	WIN32_FIND_DATAA  FindData;
	FILETIME    FtLocal;
	SYSTEMTIME  SysTime;
	char        szLine[256], szFileSpec[MAX_PATH + 5];
	char        szDate[sizeof "jj/mm/aaaa"];
	HANDLE      hFind;

	szFileSpec[MAX_PATH - 1] = 0;
	strncpy(szFileSpec, szDirectory, MAX_PATH);
	strcat(szFileSpec, "\\*.*");
	hFind = FindFirstFileA(szFileSpec, &FindData);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			// display only files, skip directories
			if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)  continue;
			FileTimeToLocalFileTime(&FindData.ftCreationTime, &FtLocal);
			FileTimeToSystemTime(&FtLocal, &SysTime);
			GetDateFormatA(LOCALE_SYSTEM_DEFAULT,
				DATE_SHORTDATE,
				&SysTime,
				NULL,
				szDate, sizeof szDate);
			szDate[sizeof "jj/mm/aaaa" - 1] = 0;    // truncate date
			FindData.cFileName[62] = 0;      // truncate file name if needed
											 // dialog structure allow up to 64 char
			sprintf(szLine, "%s\t%s\t%d\r\n",
				FindData.cFileName, szDate, FindData.nFileSizeLow);
			DWORD Dummy;
			WriteFile(hFile, szLine, strlen(szLine), &Dummy, NULL);
		} while (FindNextFileA(hFind, &FindData));
	}
	FindClose(hFind);

}  // ScanDir


int CreateIndexFile()
{
	HANDLE           hDirFile;
	static int       Semaph = 0;
	char szDirFile[MAX_PATH];

	if (Semaph++ != 0)  return 0;

	sprintf(szDirFile, "%s\\%s", sSettings.szWorkingDirectory, DIR_TEXT_FILE);
	hDirFile = CreateFileA(szDirFile,
		GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_ARCHIVE | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	if (hDirFile == INVALID_HANDLE_VALUE) return 0;
	// Walk through directory
	ScanDir(hDirFile, sSettings.szWorkingDirectory);
	CloseHandle(hDirFile);
	Semaph = 0;
	return 0;
}
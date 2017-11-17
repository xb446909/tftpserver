//////////////////////////////////////////////////////
//
// Projet TFTPD32.   Feb 99 By  Ph.jounin
// File start_threads.c:  Thread management
//
// Started by the main thread
// The procedure is itself a thread
//
// source released under European Union Public License
//
//////////////////////////////////////////////////////

// Quick documentation for the 
// The first thread started fork the thread StartTftpd32Services then call OpenDialogBox
// and act as the GUI
// StartTftpd32Services start one thread by service (DHCP, DNS, TFTP server, Syslog, ...)
// and 3 utility threads : 
//		- The console which controls the messages between GUI and threads
//		- The registry threads which is in charge to read and write the settings
//		- The Scheduler which logs the status of the worker threads
// Tftpd32UpdateServices may be launched by the console thread in order to kill or start 
// some threads.
#include "stdafx.h"


#include "headers.h"
#include <process.h>
#include <stdio.h>
#include "threading.h"



#define BOOTPD_PORT   67
#define BOOTPC_PORT   68
#define TFTP_PORT     69
#define SNTP_PORT    123
#define DNS_PORT      53
#define SYSLOG_PORT  514

const int BootPdPort = BOOTPD_PORT;
const int SntpdPort = SNTP_PORT;
const int DnsPort = DNS_PORT;
const int SyslogPort = SYSLOG_PORT;

struct S_ThreadMonitoring  tThreads[TH_NUMBER];

static const struct S_MultiThreadingConfig
{
	char       *name;                       // name of the service
	int         serv_mask;                  // identify service into sSettings.uServices
	void(*thread_proc)(void *);    // the service main thread
	BOOL        manual_event;               // automatic/manual reset of its event
	int         stack_size;                 // 
	int         family;                     // socket family
	int         type;                       // socket type
	char       *service;                    // the port to be bound ascii
	int			*def_port;                   // the port to be bound numerical
	int	    	rfc_port;					// default port taken from RFC
	char       *sz_interface;               // the interface to be opened
	int         wake_up_by_ev;              // would a SetEvent wake up the thread, FALSE if thread blocked on recvfrom
	int         restart;                    // should the thread by restarted  
}
tThreadsConfig[] =
{
	// Order is the same than enum in threading.h
	"TFTP",      TFTPD32_TFTP_SERVER,  TftpdMain, FALSE,  4096, AF_INET, SOCK_DGRAM, "tftp", &sSettings.Port, TFTP_PORT,  sSettings.szTftpLocalIP,  TRUE,   TRUE,
};



/////////////////////////////////////////////////////////////////
// return TRUE IPv6 is enabled on the local system
/////////////////////////////////////////////////////////////////
BOOL IsIPv6Enabled(void)
{
	SOCKET s = INVALID_SOCKET;
	int Rc = 0;
	// just try to open an IPv6 socket
	s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	Rc = GetLastError();  // should be WSAEAFNOSUUPORT 10047
	closesocket(s);
	return s != INVALID_SOCKET;
} // IsIPv6Enabled


static void FreeThreadResources(int Idx)
{
	if (tThreads[Idx].skt != INVALID_SOCKET)       closesocket(tThreads[Idx].skt);
	if (tThreads[Idx].hEv != INVALID_HANDLE_VALUE) CloseHandle(tThreads[Idx].hEv);
	tThreads[Idx].skt = INVALID_SOCKET;
	tThreads[Idx].hEv = INVALID_HANDLE_VALUE;
	tThreads[Idx].bSoftReset = FALSE;
} //  FreeThreadResources (Ark);


/////////////////////////////////////////////////
// Wake up a thread :
// two methods : either use SetEvent or 
//               send a "fake" message (thread blocked on recvfrom)
/////////////////////////////////////////////////

static int FakeServiceMessage(const char *name, int family, int type, const char *service, int def_port, const char *sz_if)
{
	SOCKET  s;
	int Rc;
	ADDRINFO           Hints, *res;
	char               szServ[NI_MAXSERV];

	memset(&Hints, 0, sizeof Hints);
	if (sSettings.bIPv4 && !sSettings.bIPv6 && (family == AF_INET6 || family == AF_UNSPEC))
		Hints.ai_family = AF_INET;    // force IPv4 
	else if (sSettings.bIPv6 && !sSettings.bIPv4 && (family == AF_INET || family == AF_UNSPEC))
		Hints.ai_family = AF_INET6;  // force IPv6
	else     Hints.ai_family = family;    // use IPv4 or IPv6, whichever

	Hints.ai_socktype = type;
	Hints.ai_flags = AI_NUMERICHOST;
	sprintf(szServ, "%d", def_port);
	Rc = getaddrinfo((sz_if == NULL || sz_if[0] == 0) ? "127.0.0.1" : sz_if,
		service == NULL ? service : szServ,
		&Hints,
		&res);
	if (Rc == 0)
	{
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		Rc = sendto(s, "wake up", sizeof("wake up"), 0, res->ai_addr, res->ai_addrlen);
		closesocket(s);
		freeaddrinfo(res);
	}
	return Rc > 0;
} // FakeServiceMessage

int WakeUpThread(int Idx)
{
	int Rc;
	if (tThreadsConfig[Idx].wake_up_by_ev)
	{
		Rc = SetEvent(tThreads[Idx].hEv);
		assert(!tThreads[Idx].gRunning || Rc != 0);
	}
	else     FakeServiceMessage(tThreadsConfig[Idx].name,
		tThreadsConfig[Idx].family,
		tThreadsConfig[Idx].type,
		tThreadsConfig[Idx].service,
		*tThreadsConfig[Idx].def_port,
		tThreadsConfig[Idx].sz_interface);
	return TRUE;
} // WakeUpThread


// return a OR between the running threads
int GetRunningThreads(void)
{
	int Ark;
	int uServices = TFTPD32_NONE;
	for (Ark = 0; Ark < TH_NUMBER; Ark++)
		if (tThreads[Ark].gRunning)
			uServices |= tThreadsConfig[Ark].serv_mask;
	return uServices;
} // GetRunningThreads


/////////////////////////////////////////////////
// of threads life and death
/////////////////////////////////////////////////
static int StartSingleWorkerThread(int Ark)
{
	if (tThreads[Ark].gRunning)  return 0;

	tThreads[Ark].skt = INVALID_SOCKET;

	// Create the wake up event
	if (tThreadsConfig[Ark].wake_up_by_ev)
	{
		tThreads[Ark].hEv = CreateEvent(NULL, tThreadsConfig[Ark].manual_event, FALSE, NULL);
		if (tThreads[Ark].hEv == INVALID_HANDLE_VALUE)
		{
			FreeThreadResources(Ark);
			return FALSE;
		}
	}
	else tThreads[Ark].hEv = INVALID_HANDLE_VALUE;


	tThreads[Ark].bSoftReset = FALSE;

	// now start the thread
	tThreads[Ark].tTh = (HANDLE)_beginthread(tThreadsConfig[Ark].thread_proc,
		tThreadsConfig[Ark].stack_size,
		NULL);
	if (tThreads[Ark].tTh == INVALID_HANDLE_VALUE)
	{
		FreeThreadResources(Ark);
		return FALSE;
	}
	else
	{
		// all resources have been allocated --> status OK
		tThreads[Ark].gRunning = TRUE;
	} // service correctly started
	return TRUE;
} // StartSingleWorkerThread


// Start all threads
int StartMultiWorkerThreads(BOOL bSoft)
{
#define INIT_MAX_ATTEMPS 30
	int Ark, nToBeStarted = 0, nThreadInitialized, nAttempts = 0;
#ifdef _DEBUG
	int Rc;
#endif

	for (Ark = 0; Ark < TH_NUMBER; Ark++)
	{
		// process mangement threads and 
		if ((!bSoft   &&   TFTPD32_MNGT_THREADS & tThreadsConfig[Ark].serv_mask)
			|| sSettings.uServices  & tThreadsConfig[Ark].serv_mask)
		{
			StartSingleWorkerThread(Ark);
#ifdef SERVICE_EDITION
			// for service, do not wait for console to be connected to GUI 
			// bInit set by console only when TCP connection between service and gui done
			if (tThreadsConfig[Ark].serv_mask != TFTPD32_CONSOLE)
#endif
				nToBeStarted++;
			// Pause to synchronise GUI and console
		} // process all threads
	}

	// GUI should run faster than the other threads.
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);


#ifdef _DEBUG
	// stress test synchronisation
	Rc = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	//  Rc = SetThreadPriority (tThreads [TH_CONSOLE].tTh, THREAD_PRIORITY_BELOW_NORMAL);
	if (Rc == 0) Rc = GetLastError();
#endif

	// waits until the services are started
	do
	{
		nAttempts++;
		Sleep(100);
		// count number of services initialized
		for (Ark = nThreadInitialized = 0; Ark < TH_NUMBER; Ark++)
			if (tThreads[Ark].bInit) nThreadInitialized++;
	} while (nThreadInitialized < nToBeStarted  &&  nAttempts < INIT_MAX_ATTEMPS);
	if (nAttempts >= INIT_MAX_ATTEMPS)
	{
		// at least on service has not been started -> find which one
		for (Ark = nThreadInitialized = 0; Ark < TH_NUMBER; Ark++)
			if ((
				(!bSoft   &&   TFTPD32_MNGT_THREADS & tThreadsConfig[Ark].serv_mask)
				|| sSettings.uServices  & tThreadsConfig[Ark].serv_mask
				)
				&& !tThreads[Ark].bInit
				)
				LogToMonitor("service %s not started", tThreadsConfig[Ark].name);
	} // log errors
	else LogToMonitor("--- all services started, init done");


	//// wake up GUI
	//SendMsgRequest(C_SERVICES_STARTED, NULL, 0, FALSE, FALSE);
	//// let time for the GUI to pool the services
	////Sleep (500);

	if (IsIPv6Enabled())
		LogToMonitor("IPv6 enabled");
	return TRUE;
} // StartMultiWorkerThreads


void TerminateWorkerThreads(BOOL bSoft)
{
	int Ark;
	HANDLE tHdle[TH_NUMBER];
	int nCount;
	for (Ark = 0, nCount = 0; Ark < TH_NUMBER; Ark++)
	{
		// if bSoft do not kill management threads
		if (bSoft  &&  TFTPD32_MNGT_THREADS & tThreadsConfig[Ark].serv_mask)
			continue;

		if (tThreads[Ark].gRunning)
		{
			tThreads[Ark].gRunning = FALSE;
			WakeUpThread(Ark);
			tHdle[nCount++] = tThreads[Ark].tTh;
		} // if service is running
	}
	// wait for end of threads
	WaitForMultipleObjects(nCount, tHdle, TRUE, 5000);

	for (Ark = 0; Ark < TH_NUMBER; Ark++)
	{
		if (!(bSoft  &&  TFTPD32_MNGT_THREADS & tThreadsConfig[Ark].serv_mask))
			FreeThreadResources(Ark);
	}
	LogToMonitor("all level 1 threads have returned\n");
} // TerminateWorkerThreads


// ---------------------------------------------------------------
// Settings has been changed : kill old threads and start new threads
// ---------------------------------------------------------------
void Tftpd32UpdateServices(void *lparam)
{
	int Ark;
	struct S_RestartTable *pRestart = (struct S_RestartTable *) lparam;

	// scan all worker threads
	for (Ark = TH_TFTP; Ark < TH_NUMBER; Ark++)
	{
		BOOL bOld = pRestart->oldservices & tThreadsConfig[Ark].serv_mask;
		BOOL bNew = pRestart->newservices & tThreadsConfig[Ark].serv_mask;
		BOOL bFlap = tThreads[Ark].gRunning && (pRestart->flapservices & tThreadsConfig[Ark].serv_mask);
		// do not restart a service which is not running
		// signal thread
		if (bFlap || (bOld && !bNew))
		{
			LogToMonitor("terminating %s service\n", tThreadsConfig[Ark].name);
			tThreads[Ark].gRunning = FALSE;
			tThreads[Ark].bSoftReset = TRUE;
			WakeUpThread(Ark);
			// Now wait long enough since Scheduler has to record the end of each thread
			Sleep(200);
		}
		// not running but should be started
		if (bFlap || (!bOld && bNew))
		{
			LogToMonitor("starting %s service\n", tThreadsConfig[Ark].name);
			StartSingleWorkerThread(Ark);
		}
	} // scan all events
} // Tftpd32UpdateServices



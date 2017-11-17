//////////////////////////////////////////////////////
//
// Projet TFTPD32.  January 2006 Ph.jounin
// Projet DHCPD32.  January 2006 Ph.jounin
// File threading.h:    Manage threads
//
// source released under artistic license (see license.txt)
//
//////////////////////////////////////////////////////


// Starts one thread per service (Tftpd, Sntpd, dhcpd)
// Order is the same than tThreadsConfig array 
// pseudo service should be first
enum e_Threads { TH_TFTP, 
				 TH_NUMBER };

// Events created for main threads
struct S_ThreadMonitoring
{
    int     gRunning;    // thread status
    HANDLE  tTh;         // thread handle
    HANDLE  hEv;         // wake up event
    SOCKET  skt;         // Listening SOCKET
	int     bSoftReset;  // Thread will be reset
	BOOL    bInit;		 // inits are terminated
};
extern struct S_ThreadMonitoring tThreads [TH_NUMBER];

struct S_RestartTable
{
	int oldservices;
	int newservices;
	int flapservices;
};



// Threads management : birth, life and death
int  StartMultiWorkerThreads (const char* szIniPath);
int  WakeUpThread (int Idx);
void TerminateWorkerThreads (BOOL bSoft);
int GetRunningThreads (void);


BOOL Tftpd32ReadSettings (void);
BOOL Tftpd32SaveSettings (void);
BOOL Tftpd32DestroySettings (void);
void Tftpd32UpdateServices (void *lparam);


// interaction between tftp and console (statistics)
DWORD WINAPI StartTftpTransfer (LPVOID pThreadArgs);
void ConsoleTftpGetStatistics (void);


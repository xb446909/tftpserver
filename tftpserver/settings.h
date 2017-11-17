
//////////////////////////////////////////////////////
// Projet TFTPD32.  Mars 2000 Ph.jounin
//
// File settings.h 
// settings structure declaration 
//
// released under artistic license (see license.txt)
// 
//////////////////////////////////////////////////////

#define DFLT_CONSOLE_PWD "tftpd32"

enum e_SecurityLevels { SECURITY_NONE, SECURITY_STD, SECURITY_HIGH, SECURITY_READONLY };

#define MAXLEN_IPv6 40

struct S_Tftpd32Settings
{
    char                  szBaseDirectory [_MAX_PATH];
    int                   LogLvl;
    unsigned              Timeout;
    unsigned              Retransmit;
    unsigned              WinSize;
    enum e_SecurityLevels SecurityLvl;
    int					  Port;
    BOOL                  bHide;
    BOOL                  bNegociate;
    BOOL                  bPXECompatibility;
    BOOL                  bProgressBar;
    BOOL                  bDirText;
    BOOL                  bMD5;
    BOOL                  bResumeOption;
    BOOL                  bUnixStrings;
    BOOL                  bBeep;
    BOOL                  bVirtualRoot;
	// changed in release 4 : szTftpLocalIP is either an IP address or an interface descriptor
    char                  szTftpLocalIP [max (MAXLEN_IPv6, MAX_ADAPTER_DESCRIPTION_LENGTH+4)];
    unsigned              uServices;
    unsigned              nTftpLowPort;
    unsigned              nTftpHighPort;
    DWORD                 bPersLeases;
	DWORD                 bPing;
	DWORD                 bDoubleAnswer;
    char                  szDHCPLocalIP [MAXLEN_IPv6];
	BOOL				  bEventLog;
    char                  szConsolePwd [12];    // password for GUI
	BOOL                  bPortOption;			// experimental port option
	DWORD				  nGuiRemanence;
	BOOL                  bIgnoreLastBlockAck;
	BOOL                  bIPv4;
	BOOL                  bIPv6;
	
	// unsaved settings
	DWORD				  dwMaxTftpTransfers;
    char                  szWorkingDirectory [_MAX_PATH];
    DWORD                 dwRefreshInterval;
	unsigned short		  uConsolePort;
	BOOL                  bTftpOnPhysicalIf;

	// should be last
	unsigned              uRunningServices;
};

extern struct S_Tftpd32Settings sSettings;          // The settings,used anywhere in the code
extern struct S_Tftpd32Settings sGuiSettings;

BOOL Tftpd32ReadSettings (void);
BOOL Tftpd32SaveSettings (void);
BOOL Tftpd32DestroySettings (void);

#define TFTPD32_BEFORE_MAIN_KEY "SOFTWARE"
#define TFTPD32_MAIN_KEY        "SOFTWARE\\TFTPD32"
// whish to create a executable which has a other registry key
#ifdef  TFTPD33
#  define TFTPD32_MAIN_KEY      "SOFTWARE\\TFTPD33"
#endif

#define KEY_WINDOW_POS      "LastWindowPos"


#define KEY_LEASE_NUMLEASES           "Lease_NumLeases"
#define KEY_LEASE_PREFIX              "Lease_"
#define KEY_LEASE_MAC                 "_MAC"
#define KEY_LEASE_IP                  "_IP"
#define KEY_LEASE_ALLOC               "_InitialOfferTime"
#define KEY_LEASE_RENEW               "_LeaseStartTime"

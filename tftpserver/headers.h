//////////////////////////////////////////////////////
//
// Projet TFTPD32.  Feb 99  Ph.jounin
// File Headers.h :   Gestion du protocole
//
// released under artistic license (see license.txt)
// 
//////////////////////////////////////////////////////

// #define TFTP_CLIENT_ONLY 1


#define WIN32_LEAN_AND_MEAN // this will assume smaller exe
#define _CRT_SECURE_NO_DEPRECATE

#pragma pack()

#include <windows.h>
#include <windowsx.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <IPHlpApi.h>
#include <commctrl.h>
#include <shellapi.h>

#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <process.h>

#include "custom.h"
#include "Tftp.h"
#include "tftp_struct.h"

#include "settings.h"



typedef unsigned char  u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned long  u_int32_t;


#define LOGSIZE 512

// Synchronous log via OutputDebugString

int UdpSend(int nFromPort, struct sockaddr *sa_to, int sa_len, const char *data, int len);


//////////////////////////////////////////////////////
//
// Projet TFTPD32.  Feb 99  Ph.jounin
// File Headers.h :   Gestion du protocole
//
// released under artistic license (see license.txt)
// 
//////////////////////////////////////////////////////

#pragma once

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

#include "Tftp.h"
#include "tftp_struct.h"


typedef unsigned char  u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned long  u_int32_t;

#define  DIR_TEXT_FILE          "dir.txt"

#  define TFTP_RETRANSMIT         6
#define TFTP_MAXRETRIES          50 // do not resent same block more than # times

#define  PLURAL(a)  ((a)>1 ? "s" : "")
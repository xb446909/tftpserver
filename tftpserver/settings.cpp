//////////////////////////////////////////////////////
//
// Projet TFTPD32.  Mai 98 Ph.jounin
// File tftp_sec.c:   Settings
//
// source released under European Union Public License
//
//////////////////////////////////////////////////////

// registry key :
//       HKEY_LOCAL_MACHINE\SOFTWARE\TFTPD32

// some shortcurts

#include "stdafx.h"

#include <stdio.h>
#include "headers.h"


#define  RESET_DEFAULT_TEXT  "Reset current configuration\nand destroy registry entries ?"


struct S_Tftpd32Settings sSettings =
{
	  ".",                   // Base directory
	  TFTPD32_DEF_LOG_LEVEL, // Log level
	  TFTP_TIMEOUT,          // default timeout
	  TFTP_RETRANSMIT,       // def retransmission7
	  0,                     // WinSize
	  SECURITY_STD,          // Security
	  TFTP_DEFPORT,          // Tftp Port
	  FALSE,                 // Do not Hide
	  TRUE,                  // RFC 1782-1785 Negociation
	  FALSE,                 // PXE Compatibility
	  TRUE,                  // show progress bar ?
	  FALSE,                 // do not create dir.txt file
	  FALSE,                 // do not create MD5 file
	  FALSE,                 // do not resume
	  TRUE,                  // Unix like files "/tftpboot/.."
	  FALSE,                 // Do not beep for long transfert
	  FALSE,                 // Virtual Root is not enabled
	  "",                    // do not filter TFTP'slistening interface
	  TFTPD32_TFTP_SERVER,  // all services are enabled
	  0,  0,                 // use ports assigned by Windows
	  1,                     // persistant leases
	  1,                     // ping address before assignation
	  0,                     // Do not double answer
	  "",                    // do not filter DHCP'slistening interface
	  FALSE,				 // report errors into event log
	  DFLT_CONSOLE_PWD,      // console password
	  FALSE,                 // do not support port option
	  5,					 // after 5 seconds delete Tftp record
	  FALSE,				 // wait for ack of last TFTP packet
	  TRUE,					 // IPv4
	  TRUE,					 // IPv6

	  // unsaved
	  100,                   // Max Simultaneous Transfers
	  ".",                   // Working Directory
	  2000,                  // refresh Interval
	  0,		 // default port
};



/////////////////////////////////////////////////////////
// Read settings :
// Parse the settings table and call ReadKey
/////////////////////////////////////////////////////////
BOOL Tftpd32ReadSettings(void)
{
	return TRUE;
} // Tftpd32ReadSettings


/////////////////////////////////////////////////////////
// Save Settings into ini file/registry
/////////////////////////////////////////////////////////
BOOL Tftpd32SaveSettings(void)
{
	return TRUE;

} // Tftpd32SaveSettings


/////////////////////////////////////////////////////////
// Delete all Tftpd32's settings 
/////////////////////////////////////////////////////////
BOOL Tftpd32DestroySettings(void)
{
	return TRUE;
}  // Tftpd32DestroySettings



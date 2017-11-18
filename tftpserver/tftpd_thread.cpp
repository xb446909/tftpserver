//////////////////////////////////////////////////////
//
// Projet TFTPD32.  Mai 98 Ph.jounin - June 2006
// File tftp.c:   TFTP protocol management
//
// source released under European Union Public License
//
//////////////////////////////////////////////////////

#include "stdafx.h"

#undef min
#define min(a,b)  (((a) < (b)) ? (a) : (b))


// #define DEB_TEST

#include "headers.h"
#include <process.h>
#include <stdio.h>

#include "tftpd_thread.h"
#include "CTftpServer.h"

extern char g_szWorkingDirectory[MAX_PATH];

////////////////////////////////////////////////////////
struct errmsg {
	int e_code;
	const char *e_msg;
} errmsgs[] = {
	{ EUNDEF,   "Undefined error code" },
	{ ENOTFOUND, "File not found" },
	{ EACCESS,  "Access violation" },
	{ ENOSPACE, "Disk full or allocation exceeded" },
	{ EBADOP,   "Illegal TFTP operation" },
	{ EBADID,   "Unknown transfer ID" },
	{ EEXISTS,  "File already exists" },
	{ ENOUSER,  "No such user" },
	{ ECANCELLED, "Cancelled by administrator" },
	{ -1,       0 }
};

/*
 * Send a nak packet (error message).
 * Error code passed in is one of the
 * standard TFTP codes, or a UNIX errno
 * offset by 100.
 */
int nak(struct LL_TftpInfo *pTftp, int error)
{
	struct tftphdr *tp;
	int length;
	struct errmsg *pe;

	if (pTftp->r.skt == INVALID_SOCKET) return 0;

	tp = (struct tftphdr *)pTftp->b.buf;
	tp->th_opcode = htons((u_short)TFTP_ERROR);
	tp->th_code = htons((u_short)error);
	for (pe = errmsgs; pe->e_code >= 0; pe++)
		if (pe->e_code == error)
			break;
	if (pe->e_code < 0) {
		pe->e_msg = strerror(error - 100);
		tp->th_code = EUNDEF;   /* set 'undef' errorcode */
	}
	strcpy(tp->th_msg, pe->e_msg);
	length = strlen(pe->e_msg);
	// padd with 2 null char
	*(short *)& tp->th_msg[length] = 0;
	length += 2 + offsetof(struct tftphdr, th_msg);
#if (defined DEBUG || defined DEB_TEST)
	BinDump(pTftp->b.buf, length, "NAK:");
#endif
	return send(pTftp->r.skt, pTftp->b.buf, length, 0) != length ? -1 : 0;
} // nak



///////////////////////////////////////
// Alter file name
///////////////////////////////////////
static void SecFileName(char *szFile)
{
	char *p;
	// Translation de '/' en '\'
	for (p = szFile; *p != 0; p++)
		if (*p == '/')  *p = '\\';

	if (szFile[1] == ':')   szFile[0] = toupper(szFile[0]);
	// Si option Virtual Root : Suppression de '\\'
	// sera trait??partir du répertoire courant
	if (szFile[0] == '\\')
		memmove(szFile, szFile + 1, strlen(szFile));
} // SecFileName



/////////////////////
// TftpExtendFileName
//       add current directory to the file name
//       current directory is saved into the combobox
/////////////////////
static char *TftpExtendFileName(struct LL_TftpInfo *pTftp,
	const char         *szShortName,
	char               *szExtName,
	int                 nSize)
{
	int  nLength;
	strcpy(szExtName, g_szWorkingDirectory);
	nLength = strlen(szExtName);
	if (nLength > 0 && szExtName[nLength - 1] != '\\')  szExtName[nLength++] = '\\';
	// virtual root has already been processed 
	strcpy(szExtName + nLength, szShortName);
	return szExtName;
} // TftpExtendFileName


/////////////////////
// TftpSysError : report errror in a system call
/////////////////////
static int TftpSysError(struct LL_TftpInfo *pTftp, int nTftpErr, char *szText)
{
	// tp points on the connect frame --> file name
	struct tftphdr *tp = (struct tftphdr *) pTftp->b.cnx_frame;

	LOG("File <%s> : error %d in system call %s", tp->th_stuff, GetLastError(), szText);
	nak(pTftp, nTftpErr);
	return FALSE;
} // TftpSysError


/////////////////////
// TftpSelect : wait for incoming data
//          with a pseudo exponential timeout
/////////////////////
static int TftpSelect(struct LL_TftpInfo *pTftp)
{
	int          Rc;
	fd_set          readfds;
	struct timeval sTimeout;

	FD_ZERO(&readfds);
	FD_SET(pTftp->r.skt, &readfds);

	sTimeout.tv_usec = 0;
	switch (pTftp->c.nTimeOut)
	{
	case 0:  sTimeout.tv_sec = (pTftp->s.dwTimeout + 3) / 4;
		break;
	case 1:  sTimeout.tv_sec = (pTftp->s.dwTimeout + 1) / 2;
		break;
	default: sTimeout.tv_sec = pTftp->s.dwTimeout;
	}
	Rc = select(1, &readfds, NULL, NULL, &sTimeout);
	if (Rc == SOCKET_ERROR) return TftpSysError(pTftp, EUNDEF, "select");
	return Rc; // TRUE if something is ready
} // TftpSelect


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
			if (strcmp(FindData.cFileName, DIR_TEXT_FILE) == 0) continue;
			FileTimeToLocalFileTime(&FindData.ftCreationTime, &FtLocal);
			FileTimeToSystemTime(&FtLocal, &SysTime);
			GetDateFormatA(LOCALE_SYSTEM_DEFAULT,
				DATE_SHORTDATE,
				&SysTime,
				NULL,
				szDate, sizeof szDate);
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
	char szDirFile[MAX_PATH];
	sprintf(szDirFile, "%s\\%s", g_szWorkingDirectory, DIR_TEXT_FILE);
	hDirFile = CreateFileA(szDirFile,
		GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_ARCHIVE | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	if (hDirFile == INVALID_HANDLE_VALUE)
	{
		LOG("Could not open file (error %d)", GetLastError());
		return 0;
	}
	// Walk through directory
	ScanDir(hDirFile, g_szWorkingDirectory);
	CloseHandle(hDirFile);
	return 0;
}


/////////////////////////////
// Parse connect datagram :
//      deep into the protocol
/////////////////////////////
int DecodConnectData(struct LL_TftpInfo *pTftp)
{

	struct tftphdr *tp, *tpack;
	char *p, *pValue, *pAck;
	int   opcode;
	int   Ark, Len;
	int   Rc;
	BOOL  bOptionsAccepted = FALSE;
	char  szExtendedName[2 * _MAX_PATH];

	// pad end of struct with 0
	memset(pTftp->b.padding, 0, sizeof pTftp->b.padding);
	// map the datagram on a Tftp structure
	tp = (struct tftphdr *)pTftp->b.cnx_frame;


	//-------------------------   Verify Frame validity  -------------------------
	// terminate the strings
	// PJO: 01 january 2017
	//tp->th_stuff [TFTP_SEGSIZE - TFTP_DATA_HEADERSIZE - 1] = 0;	// suppress done above

	// read or write request
	opcode = ntohs(tp->th_opcode);
	if (opcode != TFTP_RRQ  && opcode != TFTP_WRQ)
	{
		LOG("Unexpected request %d from peer", opcode);
		LOG("Returning EBADOP to Peer");
		nak(pTftp, EBADOP);
		return CNX_FAILED;
	}

	// ensure file name is strictly under _MAX_PATH (strnlen will terminates)
	if ((Ark = strnlen(tp->th_stuff, _MAX_PATH)) >= _MAX_PATH)
	{
		LOG("File name too long, return EBADOP to peer");
		nak(pTftp, EBADOP);
		return CNX_FAILED;
	}

	// Tftpd32 does not support file names with percent sign : it avoids buffer overflows vulnerabilities
	if (strchr(tp->th_stuff, '%') != NULL)
	{
		LOG("Error: Tftpd32 does not handle filenames with a percent sign");
		nak(pTftp, EACCESS);
		return  CNX_FAILED;
	}
	LOG("FileName is <%s>", tp->th_stuff);

	// create file index
	if (opcode == TFTP_RRQ)
		CreateIndexFile();

	// OK now parse the frame
	// it should have the following format   <FILE>\0<MODE>\0EXTENSION\0....

	// next word : Mode
	p = &tp->th_stuff[++Ark];		// ++Ark to point after the null char
	// ensure file name is strictly under the longest mode (0 included)
	Len = strnlen(p, sizeof("netascii"));
	if (Len >= sizeof("netascii"))
	{
		LOG("mode is too long, return EBADOP to peer");
		nak(pTftp, EBADOP);
		return CNX_FAILED;
	}
	Ark += Len;

	if (IS_OPT(p, "netascii") || IS_OPT(p, "ascii"))
		pTftp->s.TftpMode = TFTP_NETASCII;
	else if (IS_OPT(p, "mail"))
		pTftp->s.TftpMode = TFTP_MAIL;
	else if (IS_OPT(p, "octet") || IS_OPT(p, "binary"))
		pTftp->s.TftpMode = TFTP_BINARY;
	else
	{
		LOG(0, "Uncorrect message");
		nak(pTftp, EBADOP);
		return CNX_FAILED;
	}
	LOG("Mode is <%s>", p);

	Ark++;	// p[Ark] points on beginning of next word

	LOG("%s request for file <%s>. Mode %s",
		opcode == TFTP_RRQ ? "Read" : "Write", tp->th_stuff, p);

	// input file parsing
	//   --> change / to \, modify directory if VirtualRoot is on
	SecFileName(tp->th_stuff);

	// get full name
	TftpExtendFileName(pTftp, tp->th_stuff, szExtendedName, sizeof szExtendedName);
	LOG("final name : <%s>", szExtendedName);
	// ensure again extended file name is under _MAX_PATH (strlen will terminates)
	// NB: we also may call CreateFileW instaed of CreateFileA, but Windows95/Me will not support
	if (strnlen(szExtendedName, _MAX_PATH) >= _MAX_PATH)
	{
		LOG("File name too long, return EBADOP to peer");
		nak(pTftp, EBADOP);
		return CNX_FAILED;
	}

	// OK now we have the complete file name
	// will Windows be able to open/create it ?
	pTftp->r.hFile = CreateFileA(szExtendedName,
		opcode == TFTP_RRQ ? GENERIC_READ : GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		opcode == TFTP_RRQ ? OPEN_EXISTING : CREATE_ALWAYS,
		FILE_ATTRIBUTE_ARCHIVE | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	if (pTftp->r.hFile == INVALID_HANDLE_VALUE)
	{
		TftpSysError(pTftp, opcode == TFTP_RRQ ? ENOTFOUND : EACCESS, "CreateFile");
		return  CNX_FAILED;
	}
	pTftp->st.dwTransferSize = GetFileSize(pTftp->r.hFile, NULL);

	///////////////////////////////////
	// File has been correctly created/opened
	// --> Transfer will be accepted
	// but we still have to check the negotiated options
	// This procedure build the OACK packet
	///////////////////////////////////////////////////
	// next words : Options (RFC 1782)

	memset(pTftp->b.ackbuf, 0, sizeof pTftp->b.ackbuf);
	tpack = (struct tftphdr *) pTftp->b.ackbuf;
	tpack->th_opcode = htons(TFTP_OACK);
	pAck = tpack->th_stuff;

	// ---------------------------------
	// loop to handle options
	while (Ark < TFTP_SEGSIZE)
	{
		if (tp->th_stuff[Ark] == 0) { Ark++; continue; }  // points on next word
		p = &tp->th_stuff[Ark];
		LOG("Option is <%s>", p);

		// ----------
		// get 2 words (RFC 1782) : KeyWord + Value
		// p points already on Keyword, search for Value
		for (; Ark < TFTP_SEGSIZE && tp->th_stuff[Ark] != 0; Ark++);		// pass keyword
		pValue = &tp->th_stuff[Ark + 1];
		for (Ark++; Ark < TFTP_SEGSIZE && tp->th_stuff[Ark] != 0; Ark++); // pass value
		// keyword or value beyond limits : do not use
		if (Ark >= TFTP_SEGSIZE || tp->th_stuff[Ark] != 0)
			break;

		LOG("Option <%s>: value <%s>", p, pValue);

		if (IS_OPT(p, TFTP_OPT_BLKSIZE))
		{
			unsigned dwDataSize;
			dwDataSize = atoi(pValue);
			if (dwDataSize < TFTP_MINSEGSIZE)            // BLKSIZE < 8 --> refus
				LOG("<%s> proposed by client %d, refused by Tftpd32", p, dwDataSize);
			if (dwDataSize > TFTP_MAXSEGSIZE)            // BLKSIZE > 16384 --> 16384
			{
				LOG("<%s> proposed by client %d, reply with %d", p, dwDataSize, TFTP_MAXSEGSIZE);
				pTftp->s.dwPacketSize = TFTP_MAXSEGSIZE;
			}
			if (dwDataSize >= TFTP_MINSEGSIZE   && dwDataSize <= TFTP_MAXSEGSIZE)
			{
				LOG("<%s> changed to <%s>", p, pValue);
				pTftp->s.dwPacketSize = dwDataSize;
			}
			if (dwDataSize >= TFTP_MINSEGSIZE)
			{
				strcpy(pAck, p), pAck += strlen(pAck) + 1;
				sprintf(pAck, "%d", pTftp->s.dwPacketSize), pAck += strlen(pAck) + 1;
				bOptionsAccepted = TRUE;
			}
		}  // blksize options

		if (IS_OPT(p, TFTP_OPT_TIMEOUT))
		{
			unsigned dwTimeout;
			dwTimeout = atoi(pValue);
			if (dwTimeout >= 1 && dwTimeout <= 255)  // RFCs values
			{
				LOG("<%s> changed to <%s>", p, pValue);
				strcpy(pAck, p), pAck += strlen(pAck) + 1;
				strcpy(pAck, pValue), pAck += strlen(pAck) + 1;
				bOptionsAccepted = TRUE;
				pTftp->s.dwTimeout = dwTimeout;
			}
		}  // timeout options

		if (IS_OPT(p, TFTP_OPT_TSIZE))
		{
			strcpy(pAck, p), pAck += strlen(p) + 1;
			// vérue si read request -> on envoie la taille du fichier
			if (opcode == TFTP_RRQ)
			{
				sprintf(pAck, "%d", pTftp->st.dwTransferSize);
				pAck += strlen(pAck) + 1;
				pTftp->s.dwFileSize = pTftp->st.dwTransferSize;
			}
			else
			{
				strcpy(pAck, pValue), pAck += strlen(pAck) + 1;
				pTftp->s.dwFileSize = pTftp->st.dwTransferSize = atoi(pValue); // Trust client
			}

			LOG("<%s> changed to <%u>", p, pTftp->st.dwTransferSize);
			bOptionsAccepted = TRUE;
		}  // file size options

	} // for all otptions

	// bOptionsAccepted is TRUE if at least one option is accepted -> an OACK is to be sent
	// else the OACK packet is dropped and the ACK or DATA packet is to be sent
	// Another annoying protocol requirement is to begin numbering to 1 for an upload
	// and to 0 for a dwonload.
	pTftp->c.dwBytes = (DWORD)(pAck - tpack->th_stuff);

	pTftp->c.nCount = pTftp->c.nLastToSend = opcode == TFTP_RRQ && !bOptionsAccepted ? 1 : 0;
	pTftp->c.nLastBlockOfFile = pTftp->st.dwTransferSize / pTftp->s.dwPacketSize + 1;
	pTftp->s.ExtraWinSize = 0;

	if (bOptionsAccepted)
	{
		char szLog[TFTP_SEGSIZE];
		int  bKey;
		// build log from OACK segment
		memset(szLog, 0, sizeof szLog);
		memcpy(szLog, tpack->th_stuff, (int)(pAck - tpack->th_stuff));
		for (Ark = 0, bKey = 0; Ark < (int)(pAck - tpack->th_stuff); Ark++)
			if (szLog[Ark] == 0)
				szLog[Ark] = (bKey++) & 1 ? ',' : '=';
		LOG("OACK: <%s>", szLog);
		LOG("Size of OACK string : <%d>", pTftp->c.dwBytes);
		Rc = opcode == TFTP_RRQ ? CNX_OACKTOSENT_RRQ : CNX_OACKTOSENT_WRQ;
	}
	else    Rc = opcode == TFTP_RRQ ? CNX_SENDFILE : CNX_ACKTOSEND;

	return Rc;
} // DecodConnectData



/////////////////////////////
// send the OACK packet
/////////////////////////////
int TftpSendOack(struct LL_TftpInfo *pTftp)
{
	int Rc;

	assert(pTftp != NULL);

	// OACK packet is in ackbuf
	pTftp->c.dwBytes += sizeof(short);
	LOG("send OACK %d bytes", pTftp->c.dwBytes);
	Rc = send(pTftp->r.skt, pTftp->b.ackbuf, pTftp->c.dwBytes, 0);
	if (Rc < 0 || (unsigned)Rc != pTftp->c.dwBytes)
	{
		Rc = GetLastError();
		LOG(0, "send : Error %d", WSAGetLastError());
		return FALSE;
	}

	return TRUE;        // job done
} // TftpSendOack


/////////////////////////////
// display report after successfull end of transfer
/////////////////////////////
static void TftpEndOfTransfer(struct LL_TftpInfo *pTftp)
{
	struct tftphdr *tp;
	int nBlock;

	tp = (struct tftphdr *)pTftp->b.cnx_frame;
	// calcul du nb de block avec correction pour l'envoi
	nBlock = ntohs(tp->th_opcode) != TFTP_RRQ ? pTftp->c.nCount : pTftp->c.nCount - 1;
	LOG("<%s>: %s %d blk%s, %d bytes in %d s. %d blk%s resent",
		tp->th_stuff,
		ntohs(tp->th_opcode) == TFTP_RRQ ? "sent" : "rcvd",
		nBlock,
		PLURAL(nBlock),
		pTftp->st.dwTotalBytes,
		(int)(time(NULL) - pTftp->st.StartTime),
		pTftp->st.dwTotalTimeOut,
		PLURAL(pTftp->st.dwTotalTimeOut));

	if (pTftp->r.hFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(pTftp->r.hFile);
	}

} // TftpEndOfTransfer


 //////////////////////////////////////////////////////////////////////////
 //                                                                      //
 // TFTP protocol : file transfer                                        //
 //                                                                      //
 //////////////////////////////////////////////////////////////////////////



	////////////////////////
	//   DOWNLOAD (From Server to client)
	////////////////////////
int TftpSendFile(struct LL_TftpInfo *pTftp)
{
	int Rc;
	struct tftphdr *tp;

	assert(pTftp != NULL);
	pTftp->c.nLastToSend = 1;

	// loop on send/recv for current packet until
	//       dwRetries set to 0 (correct ack received)
	//        dwRetries > MAXRETRIES  (incorrect dialog)
	//       to many Timeout
	// Note : nTimeout counts only the timeout, nRetries count the nb of times
	// the same block has been sent
 // pTftp->c.nCount is the last block sent

	pTftp->c.nTimeOut = pTftp->c.nRetries = 0;
	// for the stats, early acknowledgements are already sent
	pTftp->st.dwTotalBytes = pTftp->s.ExtraWinSize * pTftp->s.dwPacketSize;
	do
	{
		// On Timeout:  cancel anticipation window
		if (pTftp->c.nTimeOut > 0)  pTftp->c.nLastToSend = pTftp->c.nCount;

		////////////////////////
		//   Send blocks #Count to #Count+window
		////////////////////////
		if (pTftp->c.nCount > 0)      // if pTftp->c.nLastToSend is 0 wait for ack#0 first
			for (;
				pTftp->c.nLastToSend <= min(pTftp->c.nCount + pTftp->s.ExtraWinSize, pTftp->c.nLastBlockOfFile);
				pTftp->c.nLastToSend++)
		{
			tp = (struct tftphdr *)pTftp->b.buf;
			Rc = (SetFilePointer(pTftp->r.hFile, pTftp->s.dwPacketSize * (pTftp->c.nLastToSend - 1), NULL, FILE_BEGIN) != (unsigned)-1
				&& ReadFile(pTftp->r.hFile, tp->th_data, pTftp->s.dwPacketSize, &pTftp->c.dwBytes, NULL));
			if (!Rc)
				return TftpSysError(pTftp, EUNDEF, "ReadFile");

			tp->th_opcode = htons(TFTP_DATA);
			tp->th_block = htons((unsigned short)pTftp->c.nLastToSend);
			//DoDebugSendBlock(pTftp); // empty ifndef DEBUG

			Rc = send(pTftp->r.skt, pTftp->b.buf, pTftp->c.dwBytes + TFTP_DATA_HEADERSIZE, 0);

			if (Rc < 0 || (unsigned)Rc != pTftp->c.dwBytes + TFTP_DATA_HEADERSIZE)
				return TftpSysError(pTftp, EUNDEF, "send");
			pTftp->c.nRetries++;
		} // send block Count to Count+windowSize

		////////////////////////
		//   receive ACK from first peer
		////////////////////////
		if (TftpSelect(pTftp))       // something has been received
		{
			// retrieve the message (will not block since select has returned)
			Rc = recv(pTftp->r.skt, pTftp->b.ackbuf, sizeof pTftp->b.ackbuf, 0);
			if (Rc <= 0)     return TftpSysError(pTftp, EUNDEF, "recv");

			tp = (struct tftphdr *) pTftp->b.ackbuf;

			//////////////////////////////////////////////
			// read the message
			//////////////////////////////////////////////
			if (Rc < TFTP_ACK_HEADERSIZE)
			{
				LOG("rcvd packet too short");
			}
			else if (ntohs(tp->th_opcode) == TFTP_ERROR)
			{
				LOG("Peer returns ERROR <%s> -> aborting transfer", tp->th_msg);
				return FALSE;
			}
			else if (ntohs(tp->th_opcode) == TFTP_ACK)
			{
				//DoDebugRcvAck(pTftp);    // empty ifndef DEBUG
				// the right ack has been received
				if (ntohs(tp->th_block) == (unsigned short)pTftp->c.nCount)
				{
					pTftp->st.dwTotalTimeOut += pTftp->c.nTimeOut;
					pTftp->c.nTimeOut = 0;
					pTftp->c.nRetries = 0;
					if (pTftp->c.nCount != 0)  // do not count OACK data block
						pTftp->st.dwTotalBytes += pTftp->c.dwBytes;  // set to 0 on error
					pTftp->c.nCount++;        // next block
				} // message was the ack of the last sent block
				else
				{
					// fixes the Sorcerer's Apprentice Syndrome
					// if an this is an ACK of an already acked packet
					// the message is silently dropped
					if (pTftp->c.nRetries < 3
						&& pTftp->c.nCount != 1        // Do not pass the test for the 1st block
						&& ntohs(tp->th_block) == (unsigned short)(pTftp->c.nCount - 1))
					{
						LOG("Ack block %d ignored (received twice)",
							(unsigned short)(pTftp->c.nCount - 1), NULL);
					}
					// Added 29 June 2006: discard an ack of a block which has still not been sent
					// only for unicast transfers
					// works only for the 65535 first blocks
					else if (!pTftp->c.bMCast && (unsigned)ntohs(tp->th_block) > (DWORD) pTftp->c.nCount)
					{
						LOG("Ack of block #%d received (last block sent #%d !)",
							ntohs(tp->th_block), pTftp->c.nCount);
					}

				}   // bad ack received
			} // ack received
			else
			{
				LOG("ignore unknown opcode %d received", ntohs(tp->th_opcode));
			} // unknown packet rcvd
		} //something received
		else
		{
			LOG("timeout while waiting for ack blk #%d", pTftp->c.nCount);
			pTftp->c.nTimeOut++;
		}   // nothing received
	} // for loop
	while (pTftp->c.nLastToSend <= pTftp->c.nLastBlockOfFile	 // not eof or eof but not acked
		&& pTftp->c.nRetries < TFTP_MAXRETRIES                 // same block sent N times (dog guard)
		&&  pTftp->c.nTimeOut < TFTP_RETRANSMIT);        // N timeout without answer

// reason of exiting the loop
	if (pTftp->c.nRetries >= TFTP_MAXRETRIES)	// watch dog
	{
		LOG("MAX RETRIES while waiting for Ack block %d. file <%s>",
			(unsigned short)pTftp->c.nCount, ((struct tftphdr *) pTftp->b.cnx_frame)->th_stuff);
		nak(pTftp, EUNDEF);  // return standard
		return FALSE;
	}
	else if (pTftp->c.nTimeOut >= TFTP_RETRANSMIT  &&  pTftp->c.nLastToSend > pTftp->c.nLastBlockOfFile)
	{
		LOG("WARNING : Last block #%d not acked for file <%s>",
			(unsigned short)pTftp->c.nCount, ((struct tftphdr *) pTftp->b.cnx_frame)->th_stuff);
		pTftp->st.dwTotalTimeOut += pTftp->c.nTimeOut;
	}
	else if (pTftp->c.nTimeOut >= TFTP_RETRANSMIT)
	{
		LOG("TIMEOUT waiting for Ack block #%d ", pTftp->c.nCount);
		nak(pTftp, EUNDEF);  // return standard
		return FALSE;
	}
	LOG("Count %d, Last pkt %d ", pTftp->c.nCount, pTftp->c.nLastToSend);

	TftpEndOfTransfer(pTftp);
	return TRUE;
} // TftpSendFile




	////////////////////////
	//   UPLOAD (From client to Server)
	////////////////////////
int TftpRecvFile(struct LL_TftpInfo *pTftp, BOOL bOACK)
{
	int Rc;
	struct tftphdr *tp;

	assert(pTftp != NULL);

	// if no OACK ready, we have to send an ACK to acknowledge the transfer
	tp = (struct tftphdr *) pTftp->b.ackbuf;
	if (!bOACK)
	{
		tp->th_opcode = htons(TFTP_ACK);
		tp->th_block = 0;
		Rc = send(pTftp->r.skt, pTftp->b.ackbuf, TFTP_ACK_HEADERSIZE, 0);
		if (Rc != (int)TFTP_ACK_HEADERSIZE)
			return TftpSysError(pTftp, EUNDEF, "send");
	} // ACK to send

	pTftp->c.nRetries = 0;
	pTftp->c.nTimeOut = 0;


	do     // stop if max retries, max timeout or received less than dwPacketSize bytes
	{
		// recv data block
		if (TftpSelect(pTftp))      // something has been received
		{
			Rc = recv(pTftp->r.skt, pTftp->b.buf, sizeof pTftp->b.buf, 0);
			if (Rc <= 0)      return TftpSysError(pTftp, EUNDEF, "recv");

			tp = (struct tftphdr *) pTftp->b.buf;

			if (Rc < TFTP_DATA_HEADERSIZE)
			{
				LOG("rcvd packet too short");
				nak(pTftp, EBADOP);
				return FALSE;
			}
			// it should be a data block
			if (ntohs(tp->th_opcode) != TFTP_DATA)
			{
				LOG("Peer sent unexpected message %d", ntohs(tp->th_opcode));
				nak(pTftp, EBADOP);
				return FALSE;
			}
			// some client have a bug and sent data block #0 --> accept it anyway after one resend

			if (ntohs(tp->th_block) == 0 && pTftp->c.nCount == 0)
			{
				pTftp->c.dwBytes = Rc - TFTP_DATA_HEADERSIZE;
				if (pTftp->st.dwTotalBytes == 0 && pTftp->c.nRetries == 0)
				{
					LOG("WARNING: First block sent by client is #0, should be #1, fixed by Tftpd32");
					pTftp->c.nCount--;  // this will fixed the pb
				}
				else continue;  // wait for next block
			} // ACK of block #0

		   // pTftp->c.nCount is the last block acked
			if (ntohs(tp->th_block) == (unsigned short)(pTftp->c.nCount + 1))
			{
				pTftp->c.nCount++;
				pTftp->st.dwTotalTimeOut += pTftp->c.nTimeOut;
				pTftp->c.nTimeOut = 0;
				pTftp->c.nRetries = 0;
				pTftp->c.dwBytes = Rc - TFTP_DATA_HEADERSIZE;
				Rc = WriteFile(pTftp->r.hFile, tp->th_data, pTftp->c.dwBytes, &pTftp->c.dwBytes, NULL);
				if (!Rc)
					return TftpSysError(pTftp, ENOSPACE, "write");

				// Stats
				pTftp->st.dwTotalBytes += pTftp->c.dwBytes;    // set to 0 on error
			} // # block received OK
		} // Something received
		else
		{
			LOG("timeout while waiting for data blk #%d", pTftp->c.nCount + 1);
			pTftp->c.nTimeOut++;
		}  // nothing received

	   // Send ACK of current block
		tp = (struct tftphdr *) pTftp->b.ackbuf;
		tp->th_opcode = htons((unsigned short)TFTP_ACK);
		tp->th_block = htons((unsigned short)pTftp->c.nCount);
		send(pTftp->r.skt, pTftp->b.ackbuf, TFTP_ACK_HEADERSIZE, 0);

	} // do
	while (pTftp->c.dwBytes == pTftp->s.dwPacketSize
		&&  pTftp->c.nRetries < TFTP_MAXRETRIES
		&&  pTftp->c.nTimeOut < TFTP_RETRANSMIT);

	// MAX RETRIES -> synchro error
	if (pTftp->c.nRetries >= TFTP_MAXRETRIES)
	{
		LOG("MAX RETRIES while waiting for Data block %d. file <%s>",
			(unsigned short)(pTftp->c.nCount + 1), ((struct tftphdr *) pTftp->b.cnx_frame)->th_stuff);
		nak(pTftp, EUNDEF);  // return standard
		return FALSE;
	}
	if (pTftp->c.nTimeOut >= TFTP_RETRANSMIT)
	{
		LOG("TIMEOUT while waiting for Data block %d, file <%s>",
			(unsigned short)(pTftp->c.nCount + 1), ((struct tftphdr *) pTftp->b.cnx_frame)->th_stuff);
		nak(pTftp, EUNDEF);  // return standard
		return FALSE;
	}
	TftpEndOfTransfer(pTftp);
	return TRUE;
}   // TftpRecvFile


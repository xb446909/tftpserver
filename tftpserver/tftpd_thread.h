#pragma once

enum e_TftpMode { TFTP_BINARY, TFTP_NETASCII, TFTP_MAIL };
enum e_TftpCnxDecod {
	CNX_OACKTOSENT_RRQ = 1000,
	CNX_OACKTOSENT_WRQ,
	CNX_SENDFILE,
	CNX_ACKTOSEND,
	CNX_FAILED
};

int DecodConnectData(struct LL_TftpInfo *pTftp);
int TftpRecvFile(struct LL_TftpInfo *pTftp, BOOL bOACK);
int TftpSendFile(struct LL_TftpInfo *pTftp);
int TftpSendOack(struct LL_TftpInfo *pTftp);
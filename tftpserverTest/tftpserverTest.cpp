// tftpserverTest.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <Windows.h>
#include "..\tftpserver\tftpserver.h"

#ifdef _DEBUG
#pragma comment(lib, ".\\..\\Debug\\tftpserver.lib")
#else
#pragma comment(lib, ".\\..\\Release\\tftpserver.lib")
#endif // _DEBUG

#pragma comment(lib, "Ws2_32.lib")



int main()
{
	StartTftpServices(".\\TftpServer.ini");
	Sleep(2000);
	StopTftpServices();

    return 0;
}


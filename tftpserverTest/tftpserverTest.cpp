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
	StartTftpd32Services(".\\TftpServer.ini");
	while (1)
	{
		Sleep(1000);
	}
    return 0;
}


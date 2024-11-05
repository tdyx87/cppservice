/****************************** Module Header ******************************\
* Module Name:  SampleService.cpp
* Project:      CppWindowsService
* Copyright (c) Microsoft Corporation.
* 
* Provides a sample service class that derives from the service base class - 
* CServiceBase. The sample service logs the service start and stop 
* information to the Application event log, and shows how to run the main 
* function of the service in a thread pool worker thread.
* 
* This source is subject to the Microsoft Public License.
* See http://www.microsoft.com/en-us/openness/resources/licenses.aspx#MPL.
* All other rights reserved.
* 
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, 
* EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED 
* WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
\***************************************************************************/

#pragma region Includes
#include "SampleService.h"
#include "ThreadPool.h"
#include <Windows.h>
#include <tchar.h>
#include <WtsApi32.h>
#include <Userenv.h>

#pragma comment(lib, "WtsApi32.lib")
#pragma comment(lib, "Userenv.lib")

#pragma endregion


CSampleService::CSampleService(PWSTR pszServiceName, 
                               BOOL fCanStop, 
                               BOOL fCanShutdown, 
                               BOOL fCanPauseContinue)
: CServiceBase(pszServiceName, fCanStop, fCanShutdown, fCanPauseContinue)
{
    m_fStopping = FALSE;

    // Create a manual-reset event that is not signaled at first to indicate 
    // the stopped signal of the service.
    m_hStoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (m_hStoppedEvent == NULL)
    {
        throw GetLastError();
    }
}


CSampleService::~CSampleService(void)
{
    if (m_hStoppedEvent)
    {
        CloseHandle(m_hStoppedEvent);
        m_hStoppedEvent = NULL;
    }
}


//
//   FUNCTION: CSampleService::OnStart(DWORD, LPWSTR *)
//
//   PURPOSE: The function is executed when a Start command is sent to the 
//   service by the SCM or when the operating system starts (for a service 
//   that starts automatically). It specifies actions to take when the 
//   service starts. In this code sample, OnStart logs a service-start 
//   message to the Application log, and queues the main service function for 
//   execution in a thread pool worker thread.
//
//   PARAMETERS:
//   * dwArgc   - number of command line arguments
//   * lpszArgv - array of command line arguments
//
//   NOTE: A service application is designed to be long running. Therefore, 
//   it usually polls or monitors something in the system. The monitoring is 
//   set up in the OnStart method. However, OnStart does not actually do the 
//   monitoring. The OnStart method must return to the operating system after 
//   the service's operation has begun. It must not loop forever or block. To 
//   set up a simple monitoring mechanism, one general solution is to create 
//   a timer in OnStart. The timer would then raise events in your code 
//   periodically, at which time your service could do its monitoring. The 
//   other solution is to spawn a new thread to perform the main service 
//   functions, which is demonstrated in this code sample.
//
void CSampleService::OnStart(DWORD dwArgc, LPWSTR *lpszArgv)
{
    // Log a service start message to the Application log.
    WriteEventLogEntry(L"CppWindowsService in OnStart", 
        EVENTLOG_INFORMATION_TYPE);

    // Queue the main service function for execution in a worker thread.
    CThreadPool::QueueUserWorkItem(&CSampleService::ServiceWorkerThread, this);
}


//
//   FUNCTION: CSampleService::ServiceWorkerThread(void)
//
//   PURPOSE: The method performs the main function of the service. It runs 
//   on a thread pool worker thread.
//

int CSampleService::_CreateProcessAsService(LPWSTR path, LPWSTR lpCommandLine)
{
	HANDLE hToken = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken))
	{
		return 0;
	}

	HANDLE hTokenDup = NULL;
	bool bRet = DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL, SecurityIdentification, TokenPrimary, &hTokenDup);
	if (!bRet || hTokenDup == NULL)
	{
		CloseHandle(hToken);
		return 0;
	}
	DWORD sessionid = WTSGetActiveConsoleSessionId();
	if (!SetTokenInformation(hTokenDup, TokenSessionId, &sessionid, sizeof(DWORD)))
	{
		CloseHandle(hToken);
		CloseHandle(hTokenDup);
		return 0;
	}
	LPVOID lpEnvironment = NULL;
	if (!CreateEnvironmentBlock(&lpEnvironment, hTokenDup, FALSE))
	{
		CloseHandle(hToken);
		CloseHandle(hTokenDup);
		return 0;
	}

	if (!lpEnvironment)
	{
		CloseHandle(hToken);
		CloseHandle(hTokenDup);
		return 0;
	}
	STARTUPINFO si = { 0 };
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(STARTUPINFO);
	si.wShowWindow = SW_SHOW;
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.lpDesktop = _T("WinSta0\\Default");// current user desktop

	PROCESS_INFORMATION pi = { 0 };
	ZeroMemory(&pi, sizeof(pi));
	if (!CreateProcessAsUser(hTokenDup, path, lpCommandLine, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE | NORMAL_PRIORITY_CLASS, lpEnvironment, NULL, &si, &pi))// for hollowing CREATE_SUSPENDED | CREATE_NO_WINDOW 
	{
		DWORD error = GetLastError();
		wchar_t buffer[256];
		swprintf(buffer, L"CreateProcessAsUser failed %d %d", sessionid, error);
		WriteEventLogEntry(buffer,
			EVENTLOG_INFORMATION_TYPE);
		return 0;
	}

	DestroyEnvironmentBlock(lpEnvironment);

	// clear memory
	CloseHandle(hToken);
	CloseHandle(hTokenDup);

	return 1;
}

int CSampleService::_CreateProcessAsUser(LPWSTR path, LPWSTR lpCommandLine)
{
	DWORD pId = 0;// result

	LUID luid; // local uniq id for process

	HANDLE TokenHandle = NULL;

	TOKEN_PRIVILEGES NewState = { 0 };
	TOKEN_PRIVILEGES PreviousState = { 0 };

	HANDLE phToken = NULL;
	HANDLE phNewToken = NULL;

	STARTUPINFO si = { 0 };
	si.cb = sizeof(STARTUPINFO);
	si.lpDesktop = _T("WinSta0\\Default");// current user desktop

	LPVOID lpEnvironment = NULL;
	PROCESS_INFORMATION pi = { 0 };

	DWORD ReturnLength;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &TokenHandle))// read current process token
	{
		WriteEventLogEntry(L"CppWindowsService in run failed notepad.exe 1",
			EVENTLOG_INFORMATION_TYPE);
		return -1;
	}
	if (!LookupPrivilegeValue(NULL, SE_TCB_NAME, &luid))
	{
		WriteEventLogEntry(L"CppWindowsService in run failed notepad.exe 2",
			EVENTLOG_INFORMATION_TYPE);
		return -1;
	}

	NewState.PrivilegeCount = 1;
	NewState.Privileges[0].Luid = luid;
	NewState.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	if (!AdjustTokenPrivileges(TokenHandle, FALSE, &NewState, sizeof(TOKEN_PRIVILEGES), &PreviousState, &ReturnLength))// change proc privileges to user
	{
		WriteEventLogEntry(L"CppWindowsService in run failed notepad.exe 3",
			EVENTLOG_INFORMATION_TYPE);
		return -1;
	}
	DWORD sessionId = WTSGetActiveConsoleSessionId();

	if (sessionId == 0xFFFFFFFF)
	{
		WriteEventLogEntry(L"CppWindowsService in run failed notepad.exe 4",
			EVENTLOG_INFORMATION_TYPE);
		return -1;
	}

	if (!WTSQueryUserToken(sessionId, &phToken))
	{
		DWORD error = GetLastError();
		wchar_t buffer[256];
		swprintf(buffer, L"WTSQueryUserToken failed %d %d", sessionId, error);
		WriteEventLogEntry(buffer,
			EVENTLOG_INFORMATION_TYPE);
		return -1;
	}

	if (!DuplicateTokenEx(phToken, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &phNewToken))
	{
		WriteEventLogEntry(L"CppWindowsService in run failed notepad.exe 6",
			EVENTLOG_INFORMATION_TYPE);
		return -1;
	}

	if (!CreateEnvironmentBlock(&lpEnvironment, phNewToken, TRUE))
	{
		WriteEventLogEntry(L"CppWindowsService in run failed notepad.exe 7",
			EVENTLOG_INFORMATION_TYPE);
		return -1;
	}

	if (!CreateProcessAsUser(phNewToken, path, lpCommandLine, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, lpEnvironment, NULL, &si, &pi))// for hollowing CREATE_SUSPENDED | CREATE_NO_WINDOW 
	{
		DWORD error = GetLastError();
		wchar_t buffer[256];
		swprintf(buffer, L"CreateProcessAsUser failed %d %d", sessionId, error);
		WriteEventLogEntry(buffer,
			EVENTLOG_INFORMATION_TYPE);
		return -1;
	}

	pId = pi.dwProcessId;

	AdjustTokenPrivileges(TokenHandle, FALSE, &PreviousState, sizeof(TOKEN_PRIVILEGES), NULL, NULL);// return proc privileges to system
	DestroyEnvironmentBlock(lpEnvironment);

	// clear memory
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	CloseHandle(phToken);
	CloseHandle(phNewToken);
	CloseHandle(TokenHandle);
	WriteEventLogEntry(L"CppWindowsService in run success notepad.exe",
		EVENTLOG_INFORMATION_TYPE);
	return pId;
}


void CSampleService::ServiceWorkerThread(void)
{
    // Periodically check if the service is stopping.
	//int pid = _CreateProcessAsUser(L"F:\\project\\gitlab\\print_audit\\bin\\x64\\Release\\printfile.exe", L" D:\\pdf\\{3D36AB76-C5BD-44CD-9BC1-A8357CEB1ED9}");
	int pid = _CreateProcessAsService(L"C:\\Windows\\System32\\notepad.exe", L"");
	if (pid == -1)
	{
		WriteEventLogEntry(L"CppWindowsService in run failed notepad.exe",
			EVENTLOG_INFORMATION_TYPE);
	}
	else
	{
		WriteEventLogEntry(L"CppWindowsService in run success notepad.exe",
			EVENTLOG_INFORMATION_TYPE);
	}


    while (!m_fStopping)
    {
        // Perform main service function here...
		//_CreateProcessAsUser(L"F:\\project\\gitlab\\print_audit\\bin\\x64\\Release\\injector.exe", L"");
		//_CreateProcessAsUser(L"F:\\project\\gitlab\\print_audit\\bin\\x86\\Release\\injector.exe", L"");

        ::Sleep(20000);  // Simulate some lengthy operations.
		_CreateProcessAsUser(L"C:\\Windows\\System32\\notepad.exe", L"");

		/*int pid = _CreateProcessAsUser(L"C:\\SetPrinter.exe", L"");
		if (pid == -1)
		{
			WriteEventLogEntry(L"CppWindowsService in run failed notepad.exe",
				EVENTLOG_INFORMATION_TYPE);
		}
		else
		{
			WriteEventLogEntry(L"CppWindowsService in run success notepad.exe",
				EVENTLOG_INFORMATION_TYPE);
		}*/
    }


	

    // Signal the stopped event.
    SetEvent(m_hStoppedEvent);
}


//
//   FUNCTION: CSampleService::OnStop(void)
//
//   PURPOSE: The function is executed when a Stop command is sent to the 
//   service by SCM. It specifies actions to take when a service stops 
//   running. In this code sample, OnStop logs a service-stop message to the 
//   Application log, and waits for the finish of the main service function.
//
//   COMMENTS:
//   Be sure to periodically call ReportServiceStatus() with 
//   SERVICE_STOP_PENDING if the procedure is going to take long time. 
//
void CSampleService::OnStop()
{
    // Log a service stop message to the Application log.
    WriteEventLogEntry(L"CppWindowsService in OnStop", 
        EVENTLOG_INFORMATION_TYPE);

    // Indicate that the service is stopping and wait for the finish of the 
    // main service function (ServiceWorkerThread).
    m_fStopping = TRUE;
    if (WaitForSingleObject(m_hStoppedEvent, INFINITE) != WAIT_OBJECT_0)
    {
        throw GetLastError();
    }
}
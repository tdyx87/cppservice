#pragma once

void InstallService(PWSTR pszServiceName, 
                    PWSTR pszDisplayName, 
                    DWORD dwStartType,
                    PWSTR pszDependencies, 
                    PWSTR pszAccount, 
                    PWSTR pszPassword);

void UninstallService(PWSTR pszServiceName);

VOID DoStartSvc(PWSTR pszServiceName);

VOID DoStopSvc(PWSTR pszServiceName);
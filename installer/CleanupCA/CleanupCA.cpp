// CleanupCA.cpp - MSI uninstall cleanup: remove leftover autostart value Run\PopKiller.
// Called only on uninstall (REMOVE~="ALL"); Impersonate=yes so HKCU targets the uninstalling user.
// Does not include msi.h (its header deps differ across SDKs); the CA handle argument is unused.
#include <windows.h>
#pragma comment(lib, "advapi32.lib")

static void DeleteRunValue(HKEY root, const wchar_t* subkey)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        RegDeleteValueW(k, L"PopKiller");
        RegCloseKey(k);
    }
}

extern "C" __declspec(dllexport) UINT __stdcall DeleteAutostart(unsigned long /*hInstall*/)
{
    DeleteRunValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    DeleteRunValue(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    return ERROR_SUCCESS;
}

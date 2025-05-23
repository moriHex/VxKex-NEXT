#include "buildcfg.h"
#include <KexComm.h>
#include <KxCfgHlp.h>

KXCFGDECLSPEC BOOLEAN KXCFGAPI KxCfgQueryLoggingSettings(
	OUT	PBOOLEAN	IsEnabled OPTIONAL,
	OUT	PWSTR		LogDir OPTIONAL,
	IN	ULONG		LogDirCch)
{
	HKEY VxKexUserKeyHandle;
	ULONG ErrorCode;

	ASSERT (!!LogDir == !!LogDirCch);

	if (IsEnabled != NULL) {
		*IsEnabled = TRUE;
	}

	if (LogDir != NULL) {
		LogDir[0] = '\0';
	}

	VxKexUserKeyHandle = KxCfgOpenVxKexRegistryKey(
		TRUE,
		KEY_READ,
		NULL);

	ASSERT (VxKexUserKeyHandle != NULL);

	if (!VxKexUserKeyHandle) {
		ErrorCode = RegCreateKeyEx(
			HKEY_CURRENT_USER,
			L"Software\\VXsoft\\VxKex",
			0,
			NULL,
			0,
			KEY_READ | KEY_WRITE | KEY_WOW64_64KEY,
			NULL,
			&VxKexUserKeyHandle,
			NULL);

		ASSERT (ErrorCode == ERROR_SUCCESS);

		ASSERT (VxKexUserKeyHandle != NULL);

		if (!VxKexUserKeyHandle) return FALSE;
	}

	if (IsEnabled != NULL) {
		ULONG EnableLogging;

		ErrorCode = RegReadI32(VxKexUserKeyHandle, NULL, L"EnableLogging", &EnableLogging);
		ASSERT (ErrorCode == ERROR_SUCCESS || ErrorCode == ERROR_FILE_NOT_FOUND);

		*IsEnabled = !!EnableLogging;
	}

	if (LogDir != NULL) {
		ErrorCode = RegReadString(VxKexUserKeyHandle, NULL, L"LogDir", LogDir, LogDirCch);
		ASSERT (ErrorCode == ERROR_SUCCESS || ErrorCode == ERROR_FILE_NOT_FOUND);
	}

	SafeClose(VxKexUserKeyHandle);
	return TRUE;
}

//
// If LogDir is NULL, it will be set to "%localappdata%\vxkex\logs".
// Environment variables are expanded in LogDir.
//
KXCFGDECLSPEC BOOLEAN KXCFGAPI KxCfgConfigureLoggingSettings(
	IN	BOOLEAN		Enabled,
	IN	PCWSTR		LogDir OPTIONAL,
	IN	HANDLE		TransactionHandle OPTIONAL)
{
	HKEY VxKexUserKeyHandle;
	ULONG LogDirExpandedCch;
	WCHAR LogDirExpanded[MAX_PATH];
	ULONG ErrorCode;

	if (!LogDir || LogDir[0] == '\0') {
		LogDir = L"%LOCALAPPDATA%\\VxKex\\Logs";
	}

	LogDirExpandedCch = ExpandEnvironmentStrings(
		LogDir,
		LogDirExpanded,
		ARRAYSIZE(LogDirExpanded));

	ASSERT (LogDirExpandedCch != 0);

	if (LogDirExpandedCch == 0) {
		return FALSE;
	}

	VxKexUserKeyHandle = KxCfgOpenVxKexRegistryKey(
		TRUE,
		KEY_READ | KEY_WRITE,
		TransactionHandle);

	ASSERT (VxKexUserKeyHandle != NULL);

	if (!VxKexUserKeyHandle) {
		return FALSE;
	}

	try {
		ErrorCode = RegWriteI32(VxKexUserKeyHandle, NULL, L"EnableLogging", Enabled);
		ASSERT (ErrorCode == ERROR_SUCCESS);

		if (ErrorCode != ERROR_SUCCESS) {
			SetLastError(ErrorCode);
			return FALSE;
		}

		ErrorCode = RegWriteString(VxKexUserKeyHandle, NULL, L"LogDir", LogDirExpanded);
		ASSERT (ErrorCode == ERROR_SUCCESS);

		if (ErrorCode != ERROR_SUCCESS) {
			SetLastError(ErrorCode);
			return FALSE;
		}
	} finally {
		SafeClose(VxKexUserKeyHandle);
	}

	//
	// Refresh the disk cleanup handler, since it depends on the location of
	// the log directory.
	//

	KxCfgInstallDiskCleanupHandler(NULL, NULL, TransactionHandle);

	return TRUE;
}
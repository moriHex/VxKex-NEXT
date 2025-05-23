﻿#include "vxlview.h"
#include "resource.h"
#include "backendp.h"

STATIC PWSTR FailureFormattingText_ENG[] = {
	L"Failed to convert \"%s\" to a NT filename (%s)",
	L"Failed to open %s (%s)",
	L"Failed to query information about %s. %s.",
	L"There are no entries in the log file you selected.\r\n"
	L"Please select a log file which is not empty.",
	L"Failed to allocate memory to store the log entry cache. "
	L"Try closing other applications or browser tabs before trying again.",
};

STATIC PWSTR FailureFormattingText_CHS[] = {
	L"无法将“%s”转换为 NT 文件名（%s）",
	L"无法打开 %s（%s）",
	L"查询有关 %s 的信息失败。%s。",
	L"您选择的日志文件中没有条目。\r\n"
	L"请选择一个非空的日志文件。",
	L"未能分配内存存储日志条目缓存。"
	L"请尝试关闭其他应用程序或浏览器标签页，然后重试。",
};

STATIC PWSTR FailureFormattingText_CHT[] = {
	L"無法將「%s」轉換為 NT 檔案名（%s）",
	L"無法開啟 %s（%s）",
	L"查詢有關 %s 的資訊失敗。%s。",
	L"您選擇的日誌檔案中沒有條目。\r\n"
	L"請選擇一個非空的日誌檔案。",
	L"未能分配內存儲存日誌條目緩存。"
	L"請嘗試關閉其他應用程式或瀏覽器標籤頁，然後重試。",
};

//
// This file contains functions to interact with VXLL, maintain a cache
// of log entries, and filter/sort the log entries.
//

PBACKENDSTATE State = NULL;

//
// This function must be called before any other function in this file is used.
// Otherwise, the application will crash.
//
VOID InitializeBackend(
	VOID)
{
	STATIC BACKENDSTATE StateInternal;
	ZeroMemory(&StateInternal, sizeof(StateInternal));
	State = &StateInternal;
}

//
// Prepare for application exit. Call before quitting the application.
//
VOID CleanupBackend(
	VOID)
{
	if (State) {
		//
		// The only thing we do is close the log file, because it is necessary for
		// the smooth operation of future applications that handle the log file.
		// All allocated memory will be released by the operating system so there is
		// no reason to waste time trying to clean it.
		//
		VxlCloseLog(&State->LogHandle);
	}
}

//
// Return TRUE if a log file is currently opened, or FALSE otherwise.
//
BOOLEAN IsLogFileOpened(
	VOID)
{
	if (State->LogHandle) {
		return TRUE;
	} else {
		return FALSE;
	}
}

//
// Open a log file.
//
BOOLEAN OpenLogFile(
	IN	PCWSTR	LogFileNameWin32)
{
	NTSTATUS Status;
	VXLHANDLE NewLogHandle;
	UNICODE_STRING LogFileNameNt;
	OBJECT_ATTRIBUTES ObjectAttributes;
	PPLOGENTRYCACHEENTRY NewLogEntryCache;
	ULONG NewNumberOfLogEntries;
	ULONG SizeOfNewNumberOfLogEntries;
	ULONG Index;

	PPWSTR FailureFormattingText;

	NewLogHandle = NULL;
	NewLogEntryCache = NULL;

	if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) FailureFormattingText = FailureFormattingText_CHS;
	else if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) FailureFormattingText = FailureFormattingText_CHS;
	else FailureFormattingText = FailureFormattingText_ENG;

	if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) SetWindowText(StatusBarWindow, L"正在打开文件，请稍候...");
	else if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) SetWindowText(StatusBarWindow, L"正在開啟檔案，請稍候...");
	else SetWindowText(StatusBarWindow, L"Opening file, please wait...");

	//
	// Convert Win32 file name to NT
	//

	Status = RtlDosPathNameToNtPathName_U_WithStatus(
		LogFileNameWin32,
		&LogFileNameNt,
		NULL,
		NULL);

	if (!NT_SUCCESS(Status)) {
		ErrorBoxF(FailureFormattingText[0], LogFileNameWin32, KexRtlNtStatusToString(Status));
		goto OpenFailure;
	}

	InitializeObjectAttributes(
		&ObjectAttributes, 
		&LogFileNameNt, 
		OBJ_CASE_INSENSITIVE, 
		NULL, 
		NULL);

	//
	// Open the log file
	//

	Status = VxlOpenLog(
		&NewLogHandle,
		NULL,
		&ObjectAttributes,
		GENERIC_READ,
		FILE_OPEN);

	RtlFreeUnicodeString(&LogFileNameNt);

	if (!NT_SUCCESS(Status)) {
		ErrorBoxF(FailureFormattingText[1], LogFileNameWin32, KexRtlNtStatusToString(Status));
		goto OpenFailure;
	}

	//
	// Find how many log entries there are in the file and allocate memory for
	// the log-entry cache.
	//
	SizeOfNewNumberOfLogEntries = sizeof(NewNumberOfLogEntries);
	Status = VxlQueryInformationLog(
		NewLogHandle,
		LogTotalNumberOfEvents,
		&NewNumberOfLogEntries,
		&SizeOfNewNumberOfLogEntries);

	if (!NT_SUCCESS(Status)) {
		ErrorBoxF(FailureFormattingText[2], LogFileNameWin32, KexRtlNtStatusToString(Status));
		goto OpenFailure;
	}

	if (NewNumberOfLogEntries == 0) {
		MessageBoxF(0, TD_INFORMATION_ICON, NULL, NULL,
					FailureFormattingText[3]);
		goto OpenFailure;
	}

	NewLogEntryCache = SafeAlloc(PLOGENTRYCACHEENTRY, NewNumberOfLogEntries);
	if (!NewLogEntryCache) {
		ErrorBoxF(FailureFormattingText[4]);
		goto OpenFailure;
	}

	RtlZeroMemory(NewLogEntryCache, NewNumberOfLogEntries * sizeof(PLOGENTRYCACHEENTRY));

	//
	// copy values of all New* variables into the global ones
	//

	if (State->LogHandle) {
		VxlCloseLog(&State->LogHandle);
	}

	SafeFree(State->FilteredLookupCache);

	for (Index = 0; Index < State->NumberOfLogEntries; ++Index) {
		SafeFree(State->LogEntryCache[Index]);
	}

	SafeFree(State->LogEntryCache);

	RtlZeroMemory(State, sizeof(*State));
	State->NumberOfLogEntries = NewNumberOfLogEntries;
	State->LogHandle = NewLogHandle;
	State->LogEntryCache = NewLogEntryCache;

	//
	// perform other misc. actions such as updating the UI text and whatever
	//

	UpdateMainMenu();

	if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) {
		SetWindowText(StatusBarWindow, L"完成。");
		StatusBar_SetTextF(StatusBarWindow, 1, L"文件中有 %lu 个条目",
						   State->NumberOfLogEntries);
	} else if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) {
		SetWindowText(StatusBarWindow, L"完成。");
		StatusBar_SetTextF(StatusBarWindow, 1, L"檔案中有 %lu 個條目",
						   State->NumberOfLogEntries);
	} else {
		SetWindowText(StatusBarWindow, L"Finished.");
		StatusBar_SetTextF(StatusBarWindow, 1, L"%lu entry(ies) in file",
						   State->NumberOfLogEntries);
	}
	
	SetWindowTextF(MainWindow, L"%s - %s", FRIENDLYAPPNAME, LogFileNameWin32);
	PopulateSourceComponents(State->LogHandle);
	ResetFilterControls();

	return TRUE;

OpenFailure:
	if (NewLogHandle) {
		VxlCloseLog(&NewLogHandle);
	}

	if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) SetWindowText(StatusBarWindow, L"无法打开日志文件。");
	else if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) SetWindowText(StatusBarWindow, L"無法開啟日誌檔案。");
	else SetWindowText(StatusBarWindow, L"Couldn't open the log file.");
	return FALSE;
}

//
// Open a dialog to ask the user for a log file, and then open it.
//
BOOLEAN OpenLogFileWithPrompt(
	VOID)
{
	BOOLEAN Success;
	OPENFILENAME OpenDialogInfo;
	WCHAR OpenFileName[MAX_PATH];

	ZeroMemory(&OpenDialogInfo, sizeof(OpenDialogInfo));
	OpenFileName[0] = '\0';
	OpenDialogInfo.lStructSize				= sizeof(OpenDialogInfo);
	OpenDialogInfo.hwndOwner				= MainWindow;
	if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) {
		OpenDialogInfo.lpstrFilter				= L"VXLog 文件（*.vxl）\0*.vxl\0所有文件（*.*）\0*.*\0";
		OpenDialogInfo.lpstrTitle				= L"选择日志文件...";
	} else if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) {
		OpenDialogInfo.lpstrFilter				= L"VXLog 檔案（*.vxl）\0*.vxl\0所有檔案（*.*）\0*.*\0";
		OpenDialogInfo.lpstrTitle				= L"選擇日誌檔案...";
	} else {
		OpenDialogInfo.lpstrFilter				= L"VXLog Files (*.vxl)\0*.vxl\0All Files (*.*)\0*.*\0";
		OpenDialogInfo.lpstrTitle				= L"Select a log file...";
	}
	OpenDialogInfo.nMaxFile					= ARRAYSIZE(OpenFileName);
	OpenDialogInfo.lpstrFile				= OpenFileName;
	OpenDialogInfo.Flags					= OFN_PATHMUSTEXIST;
	OpenDialogInfo.lpstrDefExt				= L"vxl";

	Success = GetOpenFileName(&OpenDialogInfo);

	if (Success) {
		Success = OpenLogFile(OpenFileName);
	}

	return Success;
}

// Free the UNICODE_STRING by calling RtlFreeUnicodeString after you're done with it
NTSTATUS ConvertCacheEntryToText(
	IN	PLOGENTRYCACHEENTRY	CacheEntry,
	OUT	PUNICODE_STRING		ExportedText,
	IN	BOOLEAN				LongForm)
{
	HRESULT Result;
	PVXLLOGENTRY LogEntry;
	SIZE_T RemainingBytes;

	ASSERT (CacheEntry != NULL);
	ASSERT (ExportedText != NULL);

	LogEntry = &CacheEntry->LogEntry;

	ExportedText->Length = 0;
	ExportedText->MaximumLength = LogEntry->Text.Length + LogEntry->TextHeader.Length + (256 * sizeof(WCHAR));
	ExportedText->Buffer = (PWCHAR) SafeAlloc(BYTE, ExportedText->MaximumLength);

	if (LongForm) {
		Result = StringCbPrintfEx(
			ExportedText->Buffer,
			ExportedText->MaximumLength,
			NULL,
			&RemainingBytes,
			0,
			L"Date/Time: %s\r\n"
			L"Source: PID %lu, TID %lu, %s\\%s:%s (in function %s)\r\n"
			L"%wZ%s%wZ\r\n",
			CacheEntry->ShortDateTimeAsString,
			(ULONG) LogEntry->ClientId.UniqueProcess,
			(ULONG) LogEntry->ClientId.UniqueThread,
			State->LogHandle->Header->SourceComponents[LogEntry->SourceComponentIndex],
			State->LogHandle->Header->SourceFiles[LogEntry->SourceFileIndex],
			CacheEntry->SourceLineAsString,
			State->LogHandle->Header->SourceFunctions[LogEntry->SourceFunctionIndex],
			&LogEntry->TextHeader,
			LogEntry->Text.Length != 0 ? L"\r\n\r\n" : L"",
			&LogEntry->Text);
	} else {
		Result = StringCbPrintfEx(
			ExportedText->Buffer,
			ExportedText->MaximumLength,
			NULL,
			&RemainingBytes,
			0,
			L"[%s %04lx:%04lx %s\\%s:%s (%s)] %wZ%s%s\r\n",
			CacheEntry->ShortDateTimeAsString,
			(ULONG) LogEntry->ClientId.UniqueProcess,
			(ULONG) LogEntry->ClientId.UniqueThread,
			State->LogHandle->Header->SourceComponents[LogEntry->SourceComponentIndex],
			State->LogHandle->Header->SourceFiles[LogEntry->SourceFileIndex],
			CacheEntry->SourceLineAsString,
			State->LogHandle->Header->SourceFunctions[LogEntry->SourceFunctionIndex],
			&LogEntry->TextHeader,
			LogEntry->Text.Length != 0 ? L" // " : L"",
			LogEntry->Text.Buffer != NULL ? LogEntry->Text.Buffer : L"");
	}

	if (Result == STRSAFE_E_INSUFFICIENT_BUFFER) {
		ExportedText->Length = ExportedText->MaximumLength - sizeof(WCHAR);
		return STATUS_BUFFER_OVERFLOW;
	} else if (FAILED(Result)) {
		ExportedText->Length = 0;
		return STATUS_DATA_ERROR;
	}

	ExportedText->Length = ExportedText->MaximumLength - (USHORT) RemainingBytes;
	return STATUS_SUCCESS;
}

//
// Export the currently opened log to a text file.
//
VOID ExportLog(
	IN	PCWSTR	TextFileName)
{
	RtlCreateUserThread(
		NtCurrentProcess(),
		NULL,
		FALSE,
		0,
		0,
		0,
		ExportLogThreadProc,
		(PVOID) TextFileName,
		NULL,
		NULL);
}

//
// Open a dialog to ask the user for a save file location, and then
// export the currently opened log.
//
BOOLEAN ExportLogWithPrompt(
	VOID)
{
	BOOLEAN Success;
	HRESULT Result;
	OPENFILENAME SaveDialogInfo;
	STATIC WCHAR SaveFileName[MAX_PATH];
	PCWSTR FileNameFormat = L"Exported log from %s.txt";

	ZeroMemory(&SaveDialogInfo, sizeof(SaveDialogInfo));

	//
	// Set a default file name for the exported .txt file.
	//

	Result = StringCchPrintf(
		SaveFileName,
		ARRAYSIZE(SaveFileName),
		FileNameFormat,
		State->LogHandle->Header->SourceApplication);
	if (FAILED(Result)) {
		StringCchCopy(SaveFileName, ARRAYSIZE(SaveFileName), L"Exported Log.txt");
	}

	PathReplaceIllegalCharacters(SaveFileName, '_', FALSE);

	SaveDialogInfo.lStructSize				= sizeof(SaveDialogInfo);
	SaveDialogInfo.hwndOwner				= MainWindow;
	if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) SaveDialogInfo.lpstrFilter				= L"文本文件（*.txt）\0*.txt\0所有文件（*.*）\0*.*\0";
	else if (CURRENTLANG == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) SaveDialogInfo.lpstrFilter				= L"文本檔案（*.txt）\0*.txt\0所有檔案（*.*）\0*.*\0";
	else SaveDialogInfo.lpstrFilter				= L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
	SaveDialogInfo.nMaxFile					= ARRAYSIZE(SaveFileName);
	SaveDialogInfo.lpstrFile				= SaveFileName;
	SaveDialogInfo.Flags					= OFN_OVERWRITEPROMPT;
	SaveDialogInfo.lpstrDefExt				= L"txt";

	Success = GetSaveFileName(&SaveDialogInfo);

	if (Success) {
		ExportLog(SaveFileName);
	}

	return Success;
}

VOID SetBackendFilters(
	IN	PBACKENDFILTERS	Filters)
{
	CopyMemory(&State->Filters, Filters, sizeof(*Filters));
	RebuildFilterCache();
	ListView_SetItemCount(ListViewWindow, State->EstimatedNumberOfFilteredLogEntries);
}

ULONG GetLogEntryRawIndex(
	IN	ULONG	EntryIndex)
{
	ULONG DisplayIndex;
	ULONG RawIndex;

	//
	// if we already cached this, we don't need to evaluate filters
	//
	if (State->FilteredLookupCache[EntryIndex] != ((ULONG) -1)) {
		return State->FilteredLookupCache[EntryIndex];
	}

	for (DisplayIndex = 0;
		 DisplayIndex < State->EstimatedNumberOfFilteredLogEntries && DisplayIndex <= EntryIndex;
		 DisplayIndex++) {

		//
		// Run every entry against the filters up to the requested display index,
		// if it hasn't been filtered already.
		//
		if (State->FilteredLookupCache[DisplayIndex] != -1) {
			continue;
		}

		if (DisplayIndex != 0) {
			RawIndex = State->FilteredLookupCache[DisplayIndex - 1] + 1;
		} else {
			RawIndex = 0;
		}

		for (; RawIndex < State->NumberOfLogEntries; RawIndex++) {
			PLOGENTRYCACHEENTRY CacheEntry;

			CacheEntry = GetLogEntryRaw(RawIndex);

			if (!CacheEntry) {
				continue;
			}

			if (LogEntryMatchesFilters(CacheEntry)) {
				State->FilteredLookupCache[DisplayIndex] = RawIndex;
				break;
			}

			if (RawIndex == (State->NumberOfLogEntries - 1)) {
				// we have reached the end, there are no more filtered entries
				ListView_SetItemCountEx(ListViewWindow, DisplayIndex, LVSICF_NOINVALIDATEALL);
			}
		}
	}

	return State->FilteredLookupCache[EntryIndex];
}

// This function is rather inefficient as it was hacked in after the core backend
// was already designed. It's used for the Ctrl+G "go to raw entry" functionality.
ULONG GetLogEntryIndexFromRawIndex(
	IN	ULONG	RawIndex)
{
	ULONG RawIndexOfItem;
	ULONG Index;

	Index = 0;

	do {
		RawIndexOfItem = GetLogEntryRawIndex(Index);

		if (RawIndexOfItem == RawIndex) {
			return Index;
		}

		++Index;
	} until (RawIndexOfItem == RawIndex || RawIndexOfItem == -1);

	return (ULONG) -1;
}

//
// Get a log entry, respecting the current filters.
//
PLOGENTRYCACHEENTRY GetLogEntry(
	IN	ULONG	EntryIndex)
{
	ULONG RawIndex;

	RawIndex = GetLogEntryRawIndex(EntryIndex);

	if (RawIndex == -1) {
		return NULL;
	}

	return GetLogEntryRaw(RawIndex);
}
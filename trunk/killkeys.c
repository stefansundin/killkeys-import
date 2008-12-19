/*
	KillKeys - Disable keys from working
	Copyright (C) 2008  Stefan Sundin (recover89@gmail.com)
	
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

#define UNICODE
#define _UNICODE

#include <stdio.h>
#include <stdlib.h>
#define _WIN32_IE 0x0600
#include <windows.h>
#include <shlwapi.h>
#include <wininet.h>

//App
#define APP_NAME      L"KillKeys"
#define APP_VERSION   "0.4"
#define APP_URL       L"http://killkeys.googlecode.com/"
#define APP_UPDATEURL L"http://killkeys.googlecode.com/svn/wiki/latest-stable.txt"
//#define DEBUG

//Localization
#ifndef L10N_FILE
#define L10N_FILE "localization/en-US/strings.h"
#endif
#include L10N_FILE
#if L10N_VERSION != 1
#error Localization not up to date!
#endif

//Messages
#define WM_ICONTRAY            WM_USER+1
#define SWM_TOGGLE             WM_APP+1
#define SWM_HIDE               WM_APP+2
#define SWM_AUTOSTART_ON       WM_APP+3
#define SWM_AUTOSTART_OFF      WM_APP+4
#define SWM_AUTOSTART_HIDE_ON  WM_APP+5
#define SWM_AUTOSTART_HIDE_OFF WM_APP+6
#define SWM_UPDATE             WM_APP+7
#define SWM_ABOUT              WM_APP+8
#define SWM_EXIT               WM_APP+9

//Balloon stuff missing in MinGW
#define NIIF_USER 4
#define NIN_BALLOONSHOW        WM_USER+2
#define NIN_BALLOONHIDE        WM_USER+3
#define NIN_BALLOONTIMEOUT     WM_USER+4
#define NIN_BALLOONUSERCLICK   WM_USER+5

//Boring stuff
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
static HICON icon[2];
static NOTIFYICONDATA traydata;
static unsigned int WM_TASKBARCREATED=0;
static unsigned int WM_ADDTRAY=0;
static int tray_added=0;
static int hide=0;
static int update=0;
struct {
	int CheckForUpdate;
} settings={0};
static wchar_t txt[100];

//Cool stuff
static HINSTANCE hinstDLL=NULL;
static HHOOK keyhook=NULL;
static int *keys=NULL;
static int numkeys=0;
static int *keys_fullscreen=NULL;
static int numkeys_fullscreen=0;

//Error message handling
static int showerror=1;

LRESULT CALLBACK ErrorMsgProc(INT nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HCBT_ACTIVATE) {
		//Edit the caption of the buttons
		SetDlgItemText((HWND)wParam,IDYES,L"Copy error");
		SetDlgItemText((HWND)wParam,IDNO,L"OK");
	}
	return 0;
}

void Error(wchar_t *func, wchar_t *info, int errorcode, int line) {
	if (showerror) {
		//Format message
		wchar_t errormsg[100];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,errorcode,0,errormsg,sizeof(errormsg)/sizeof(wchar_t),NULL);
		errormsg[wcslen(errormsg)-2]='\0'; //Remove that damn newline at the end of the formatted error message
		swprintf(txt,L"%s failed in file %s, line %d.\nError: %s (%d)\n\n%s", func, TEXT(__FILE__), line, errormsg, errorcode, info);
		//Display message
		HHOOK hhk=SetWindowsHookEx(WH_CBT, &ErrorMsgProc, 0, GetCurrentThreadId());
		int response=MessageBox(NULL, txt, APP_NAME" Error", MB_ICONERROR|MB_YESNO|MB_DEFBUTTON2);
		UnhookWindowsHookEx(hhk);
		if (response == IDYES) {
			//Copy message to clipboard
			OpenClipboard(NULL);
			EmptyClipboard();
			wchar_t *data=LocalAlloc(LMEM_FIXED,(wcslen(txt)+1)*sizeof(wchar_t));
			memcpy(data,txt,(wcslen(txt)+1)*sizeof(wchar_t));
			SetClipboardData(CF_UNICODETEXT,data);
			CloseClipboard();
		}
	}
}

//Check for update
DWORD WINAPI _CheckForUpdate() {
	//Open connection
	HINTERNET http, file;
	if ((http=InternetOpen(APP_NAME" - "APP_VERSION,INTERNET_OPEN_TYPE_DIRECT,NULL,NULL,0)) == NULL) {
		#ifdef DEBUG
		Error(L"InternetOpen()",L"Could not establish connection.\nPlease check for update manually at "APP_URL,GetLastError(),__LINE__);
		#endif
		return;
	}
	if ((file=InternetOpenUrl(http,APP_UPDATEURL,NULL,0,INTERNET_FLAG_NO_AUTH|INTERNET_FLAG_NO_AUTO_REDIRECT|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_UI,0)) == NULL) {
		#ifdef DEBUG
		Error(L"InternetOpenUrl()",L"Could not establish connection.\nPlease check for update manually at "APP_URL,GetLastError(),__LINE__);
		#endif
		return;
	}
	//Read file
	char data[20];
	DWORD numread;
	if (InternetReadFile(file,data,sizeof(data),&numread) == FALSE) {
		#ifdef DEBUG
		Error(L"InternetReadFile()",L"Could not read file.\nPlease check for update manually at "APP_URL,GetLastError(),__LINE__);
		#endif
		return;
	}
	data[numread]='\0';
	//Get error code
	wchar_t code[4];
	DWORD len=sizeof(code);
	HttpQueryInfo(file,HTTP_QUERY_STATUS_CODE,&code,&len,NULL);
	//Close connection
	InternetCloseHandle(file);
	InternetCloseHandle(http);
	
	//Make sure the server returned 200
	if (wcscmp(code,L"200")) {
		#ifdef DEBUG
		swprintf(txt,L"Server returned %s error when checking for update.\nPlease check for update manually at "APP_URL,code);
		MessageBox(NULL, txt, APP_NAME, MB_ICONWARNING|MB_OK);
		#endif
		return;
	}
	
	//New version available?
	if (strcmp(data,APP_VERSION)) {
		update=1;
		traydata.uFlags|=NIF_INFO;
		UpdateTray();
		traydata.uFlags^=NIF_INFO;
	}
}

void CheckForUpdate() {
	CreateThread(NULL,0,_CheckForUpdate,NULL,0,NULL);
}

//Entry point
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow) {
	//Look for previous instance
	WM_ADDTRAY=RegisterWindowMessage(L"AddTray");
	HWND previnst;
	if ((previnst=FindWindow(APP_NAME,NULL)) != NULL) {
		SendMessage(previnst,WM_ADDTRAY,0,0);
		PostMessage(previnst,WM_USER+2,0,0); //Compatibility with old versions (this will be removed in the future)
		return 0;
	}
	
	//Check command line
	if (!strcmp(szCmdLine,"-hide")) {
		hide=1;
	}
	
	//Create window class
	WNDCLASSEX wnd;
	wnd.cbSize=sizeof(WNDCLASSEX);
	wnd.style=0;
	wnd.lpfnWndProc=WindowProc;
	wnd.cbClsExtra=0;
	wnd.cbWndExtra=0;
	wnd.hInstance=hInst;
	wnd.hIcon=NULL;
	wnd.hIconSm=NULL;
	wnd.hCursor=LoadImage(NULL, IDC_ARROW, IMAGE_CURSOR, 0, 0, LR_DEFAULTCOLOR|LR_SHARED);
	wnd.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
	wnd.lpszMenuName=NULL;
	wnd.lpszClassName=APP_NAME;
	
	//Register class
	RegisterClassEx(&wnd);
	
	//Create window
	HWND hwnd=CreateWindowEx(0, wnd.lpszClassName, APP_NAME, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInst, NULL);
	
	//Load icons
	if ((icon[0] = LoadImage(hInst, L"tray-disabled", IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR)) == NULL) {
		Error(L"LoadImage('tray-disabled')",L"Fatal error.",GetLastError(),__LINE__);
		PostQuitMessage(1);
	}
	if ((icon[1] = LoadImage(hInst, L"tray-enabled", IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR)) == NULL) {
		Error(L"LoadImage('tray-enabled')",L"Fatal error.",GetLastError(),__LINE__);
		PostQuitMessage(1);
	}
	
	//Create icondata
	traydata.cbSize=sizeof(NOTIFYICONDATA);
	traydata.uID=0;
	traydata.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
	traydata.hWnd=hwnd;
	traydata.uCallbackMessage=WM_ICONTRAY;
	//Balloon tooltip
	traydata.uTimeout=10000;
	wcsncpy(traydata.szInfoTitle,APP_NAME,sizeof(traydata.szInfoTitle)/sizeof(wchar_t));
	wcsncpy(traydata.szInfo,L10N_UPDATE_BALLOON,sizeof(traydata.szInfo)/sizeof(wchar_t));
	traydata.dwInfoFlags=NIIF_USER;
	
	//Register TaskbarCreated so we can re-add the tray icon if explorer.exe crashes
	WM_TASKBARCREATED=RegisterWindowMessage(L"TaskbarCreated");
	
	//Update tray icon
	UpdateTray();
	
	//Hook keyboard
	HookKeyboard();
	
	//Add tray if hooking failed, even though -hide was supplied
	if (hide && !keyhook) {
		hide=0;
		UpdateTray();
	}
	
	//Load settings
	wchar_t path[MAX_PATH];
	GetModuleFileName(NULL, path, sizeof(path));
	PathRenameExtension(path,L".ini");
	GetPrivateProfileString(L"Update",L"CheckForUpdate",L"0",txt,sizeof(txt)/sizeof(wchar_t),path);
	swscanf(txt,L"%d",&settings.CheckForUpdate);
	
	//Check for update
	if (settings.CheckForUpdate) {
		CheckForUpdate();
	}
	
	//Message loop
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.wParam;
}

void ShowContextMenu(HWND hwnd) {
	POINT pt;
	GetCursorPos(&pt);
	HMENU hMenu=CreatePopupMenu();
	
	//Toggle
	InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_TOGGLE, (keyhook?L10N_MENU_DISABLE:L10N_MENU_ENABLE));
	
	//Hide
	InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_HIDE, L10N_MENU_HIDE);
	
	//Check autostart
	int autostart_enabled=0, autostart_hide=0;
	//Open key
	HKEY key;
	RegOpenKeyEx(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key);
	//Read value
	wchar_t autostart_value[MAX_PATH+10];
	DWORD len=sizeof(autostart_value);
	DWORD res=RegQueryValueEx(key,APP_NAME,NULL,NULL,(LPBYTE)autostart_value,&len);
	//Close key
	RegCloseKey(key);
	//Get path
	wchar_t path[MAX_PATH];
	GetModuleFileName(NULL,path,MAX_PATH);
	//Compare
	wchar_t pathcmp[MAX_PATH+10];
	swprintf(pathcmp,L"\"%s\"",path);
	if (!wcscmp(pathcmp,autostart_value)) {
		autostart_enabled=1;
	}
	else {
		swprintf(pathcmp,L"\"%s\" -hide",path);
		if (!wcscmp(pathcmp,autostart_value)) {
			autostart_enabled=1;
			autostart_hide=1;
		}
	}
	//Autostart
	HMENU hAutostartMenu=CreatePopupMenu();
	InsertMenu(hAutostartMenu, -1, MF_BYPOSITION|(autostart_enabled?MF_CHECKED:0), (autostart_enabled?SWM_AUTOSTART_OFF:SWM_AUTOSTART_ON), L10N_MENU_AUTOSTART);
	InsertMenu(hAutostartMenu, -1, MF_BYPOSITION|(autostart_hide?MF_CHECKED:0), (autostart_hide?SWM_AUTOSTART_HIDE_OFF:SWM_AUTOSTART_HIDE_ON), L10N_MENU_HIDE);
	InsertMenu(hMenu, -1, MF_BYPOSITION|MF_POPUP, (UINT)hAutostartMenu, L10N_MENU_AUTOSTART);
	InsertMenu(hMenu, -1, MF_BYPOSITION|MF_SEPARATOR, 0, NULL);
	
	//Update
	if (update) {
		InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_UPDATE, L10N_MENU_UPDATE);
		InsertMenu(hMenu, -1, MF_BYPOSITION|MF_SEPARATOR, 0, NULL);
	}
	
	//About
	InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_ABOUT, L10N_MENU_ABOUT);
	
	//Exit
	InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_EXIT, L10N_MENU_EXIT);

	//Track menu
	SetForegroundWindow(hwnd);
	TrackPopupMenu(hMenu, TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL );
	DestroyMenu(hMenu);
}

int UpdateTray() {
	wcsncpy(traydata.szTip,(keyhook?L10N_TRAY_ENABLED:L10N_TRAY_DISABLED),sizeof(traydata.szTip)/sizeof(wchar_t));
	traydata.hIcon=icon[keyhook?1:0];
	
	//Only add or modify if not hidden or if balloon will be displayed
	if (!hide || traydata.uFlags&NIF_INFO) {
		if (Shell_NotifyIcon((tray_added?NIM_MODIFY:NIM_ADD),&traydata) == FALSE) {
			Error(L"Shell_NotifyIcon(NIM_ADD/NIM_MODIFY)",L"Failed to add tray icon.",GetLastError(),__LINE__);
			return 1;
		}
		
		//Success
		tray_added=1;
	}
}

int RemoveTray() {
	if (!tray_added) {
		//Tray not added
		return 1;
	}
	
	if (Shell_NotifyIcon(NIM_DELETE,&traydata) == FALSE) {
		Error(L"Shell_NotifyIcon(NIM_DELETE)",L"Failed to remove tray icon.",GetLastError(),__LINE__);
		return 1;
	}
	
	//Success
	tray_added=0;
}

void SetAutostart(int on, int hide) {
	//Open key
	HKEY key;
	int error;
	if ((error=RegOpenKeyEx(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&key)) != ERROR_SUCCESS) {
		Error(L"RegOpenKeyEx(HKEY_CURRENT_USER,'Software\\Microsoft\\Windows\\CurrentVersion\\Run')",L"Error opening the registry.",error,__LINE__);
		return;
	}
	if (on) {
		//Get path
		wchar_t path[MAX_PATH];
		if (GetModuleFileName(NULL,path,MAX_PATH) == 0) {
			Error(L"GetModuleFileName(NULL)",L"",GetLastError(),__LINE__);
			return;
		}
		//Add
		wchar_t value[MAX_PATH+10];
		swprintf(value,(hide?L"\"%s\" -hide":L"\"%s\""),path);
		if ((error=RegSetValueEx(key,APP_NAME,0,REG_SZ,(LPBYTE)value,(wcslen(value)+1)*sizeof(wchar_t))) != ERROR_SUCCESS) {
			Error(L"RegSetValueEx('"APP_NAME"')",L"",error,__LINE__);
			return;
		}
	}
	else {
		//Remove
		if ((error=RegDeleteValue(key,APP_NAME)) != ERROR_SUCCESS) {
			Error(L"RegDeleteValue('"APP_NAME"')",L"",error,__LINE__);
			return;
		}
	}
	//Close key
	RegCloseKey(key);
}

//Hook
_declspec(dllexport) LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
		int vkey=((PKBDLLHOOKSTRUCT)lParam)->vkCode;
		
		HWND hwnd=GetForegroundWindow();
		int stop=0;
		int fullscreen=0;
		int i;
		
		//Get window and desktop size
		RECT wnd;
		if (GetWindowRect(hwnd,&wnd) == 0) {
			#ifdef DEBUG
			Error(L"GetWindowRect(hwnd)",L"LowLevelKeyboardProc()",GetLastError(),__LINE__);
			#endif
			return CallNextHookEx(NULL, nCode, wParam, lParam); 
		}
		RECT desk;
		if (GetWindowRect(GetDesktopWindow(),&desk) == 0) {
			#ifdef DEBUG
			Error(L"GetWindowRect(GetDesktopWindow())",L"LowLevelKeyboardProc()",GetLastError(),__LINE__);
			#endif
			return CallNextHookEx(NULL, nCode, wParam, lParam); 
		}
		
		//Are we in a fullscreen window?
		if (wnd.left == desk.left && wnd.top == desk.top && wnd.right == desk.right && wnd.bottom == desk.bottom) {
			fullscreen=1;
		}
		//The desktop (explorer.exe) doesn't count as a fullscreen window.
		//HWND desktop = FindWindow(L"WorkerW", NULL); //This is the window that's focused after pressing [the windows button]+D
		HWND desktop = FindWindow(L"Progman", L"Program Manager");
		if (fullscreen && desktop != NULL) {
			DWORD desktop_pid, hwnd_pid;
			GetWindowThreadProcessId(desktop,&desktop_pid);
			GetWindowThreadProcessId(hwnd,&hwnd_pid);
			if (desktop_pid == hwnd_pid) {
				fullscreen=0;
			}
		}
		
		//Check if the key should be blocked
		if (!fullscreen) {
			for (i=0; i < numkeys; i++) {
				if (vkey == keys[i]) {
					stop=1;
					break;
				}
			}
		}
		else {
			for (i=0; i < numkeys_fullscreen; i++) {
				if (vkey == keys_fullscreen[i]) {
					stop=1;
					break;
				}
			}
		}
		
		//Stop this key
		if (stop) {
			return 1;
		}
	}
	
    return CallNextHookEx(NULL, nCode, wParam, lParam); 
}

int HookKeyboard() {
	if (keyhook) {
		//Hook already installed
		return 1;
	}
	
	//Read config.txt to buffer
	FILE *config;
	if ((config=fopen("config.txt","rb")) == NULL) {
		Error(L"fopen('config.txt')",L"This probably means that config.txt is missing. You can try downloading "APP_NAME" again.",GetLastError(),__LINE__);
		return 1;
	}
	fseek(config,0,SEEK_END);
	int length=ftell(config);
	fseek(config,0,SEEK_SET);
	char *buffer=malloc(length+1);
	fread(buffer,1,length,config);
	fclose(config);
	buffer[length]='\0';
	
	//Parse buffer
	numkeys=0;
	numkeys_fullscreen=0;
	int keys_alloc=0;
	//Loop buffer
	int **add_keys=&keys; //We need a pointer to a pointer to be able to realloc without having to know if add_keys points to keys or keys_fullscreen
	int *add_numkeys=&numkeys;
	int pos=0;
	while (buffer[pos] != '\0') {
		if (buffer[pos] == '\r') {
			//Ignore \r
			pos++;
		}
		else if (buffer[pos] == '\n') {
			//Switch to keys_fullscreen or stop parsing
			if (*add_keys == keys) {
				add_keys=&keys_fullscreen;
				add_numkeys=&numkeys_fullscreen;
				keys_alloc=0;
			}
			else {
				break;
			}
			pos++;
		}
		else if (buffer[pos] == ' ') {
			//Ignore space
			pos++;
		}
		else {
			//Parse key
			int temp;
			int numbytesread;
			if (sscanf(buffer+pos,"%02X%n",&temp,&numbytesread) != EOF) {
				//Make sure we have enough space
				if (*add_numkeys == keys_alloc) {
					keys_alloc+=100;
					if ((*add_keys=realloc(*add_keys,keys_alloc*sizeof(int))) == NULL) {
						Error(L"realloc(*add_keys)",L"Out of memory?",GetLastError(),__LINE__);
						break;
					}
				}
				//Store key
				(*add_keys)[(*add_numkeys)++]=temp;
				pos+=numbytesread;
			}
		}
	}
	
	//Free buffer
	free(buffer);
	
	//Load library
	wchar_t path[MAX_PATH]=APP_NAME;
	GetModuleFileName(NULL, path, sizeof(path));
	if ((hinstDLL=LoadLibrary(path)) == NULL) {
		Error(L"LoadLibrary()",L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.",GetLastError(),__LINE__);
		return 1;
	}
	
	//Get address to keyboard hook (beware name mangling)
	HOOKPROC procaddr;
	if ((procaddr=(HOOKPROC)GetProcAddress(hinstDLL,"LowLevelKeyboardProc@12")) == NULL) {
		Error(L"GetProcAddress('LowLevelKeyboardProc@12')",L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.",GetLastError(),__LINE__);
		return 1;
	}
	
	//Set up the hook
	if ((keyhook=SetWindowsHookEx(WH_KEYBOARD_LL,procaddr,hinstDLL,0)) == NULL) {
		Error(L"SetWindowsHookEx(WH_KEYBOARD_LL)",L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.",GetLastError(),__LINE__);
		return 1;
	}
	
	//Success
	UpdateTray();
	return 0;
}

int UnhookKeyboard() {
	if (!keyhook) {
		//Hook not installed
		return 1;
	}
	
	//Reset keys
	numkeys=0;
	numkeys_fullscreen=0;
	free(keys);
	free(keys_fullscreen);
	keys=NULL;
	keys_fullscreen=NULL;
	
	//Remove keyboard hook
	if (UnhookWindowsHookEx(keyhook) == 0) {
		Error(L"UnhookWindowsHookEx(keyhook)",L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.",GetLastError(),__LINE__);
		return 1;
	}
	
	//Unload library
	if (FreeLibrary(hinstDLL) == 0) {
		Error(L"FreeLibrary()",L"Check the "APP_NAME" website if there is an update, if the latest version doesn't fix this, please report it.",GetLastError(),__LINE__);
		return 1;
	}
	
	//Success
	keyhook=NULL;
	UpdateTray();
	return 0;
}

void ToggleState() {
	if (keyhook) {
		UnhookKeyboard();
	}
	else {
		HookKeyboard();
	}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_ICONTRAY) {
		if (lParam == WM_LBUTTONDOWN) {
			ToggleState();
		}
		else if (lParam == WM_RBUTTONDOWN) {
			ShowContextMenu(hwnd);
		}
		else if (lParam == NIN_BALLOONTIMEOUT) {
			if (hide) {
				RemoveTray();
			}
		}
		else if (lParam == NIN_BALLOONUSERCLICK) {
			hide=0;
			SendMessage(hwnd,WM_COMMAND,SWM_UPDATE,0);
		}
	}
	else if (msg == WM_ADDTRAY) {
		hide=0;
		UpdateTray();
	}
	else if (msg == WM_TASKBARCREATED) {
		tray_added=0;
		UpdateTray();
	}
	else if (msg == WM_COMMAND) {
		int wmId=LOWORD(wParam), wmEvent=HIWORD(wParam);
		if (wmId == SWM_TOGGLE) {
			ToggleState();
		}
		else if (wmId == SWM_HIDE) {
			hide=1;
			RemoveTray();
		}
		else if (wmId == SWM_AUTOSTART_ON) {
			SetAutostart(1,0);
		}
		else if (wmId == SWM_AUTOSTART_OFF) {
			SetAutostart(0,0);
		}
		else if (wmId == SWM_AUTOSTART_HIDE_ON) {
			SetAutostart(1,1);
		}
		else if (wmId == SWM_AUTOSTART_HIDE_OFF) {
			SetAutostart(1,0);
		}
		else if (wmId == SWM_UPDATE) {
			if (MessageBox(NULL, L10N_UPDATE_DIALOG, APP_NAME, MB_ICONINFORMATION|MB_YESNO) == IDYES) {
				ShellExecute(NULL, L"open", APP_URL, NULL, NULL, SW_SHOWNORMAL);
			}
		}
		else if (wmId == SWM_ABOUT) {
			if (keyhook) {
				wcscpy(txt, L10N_ABOUT_KEYS1);
				int i;
				for (i=0; i < numkeys; i++) {
					swprintf(txt, L"%s %02X", txt, keys[i]);
				}
				wcscat(txt, "\n"L10N_ABOUT_KEYS2);
				for (i=0; i < numkeys_fullscreen; i++) {
					swprintf(txt, L"%s %02X", txt, keys_fullscreen[i]);
				}
			}
			else {
				wcscpy(txt, L10N_ABOUT_KEYS3);
			}
			wchar_t buffer[1000];
			swprintf(buffer, L10N_ABOUT, txt);
			MessageBox(NULL, buffer, L10N_ABOUT_TITLE, MB_ICONINFORMATION|MB_OK);
		}
		else if (wmId == SWM_EXIT) {
			DestroyWindow(hwnd);
		}
	}
	else if (msg == WM_DESTROY) {
		showerror=0;
		UnhookKeyboard();
		RemoveTray();
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}
#include <assert.h>
#define NOIME // prevent windows.h from including imm.h
#include <Windows.h>
#include <LibIME/imm32/imm.h> // include our imm.h from DDK
#include <winreg.h>
#include <shlobj.h>
#include <windowsx.h>
#include "resource.h"
#include <msctf.h>

#include "ChewingImeModule.h"
#include "ChewingTextService.h"
#include <libIME/KeyEvent.h>
#include <libIME/EditSession.h>

// defined in DllEntry.cpp
extern Chewing::ImeModule* g_imeModule;
static wchar_t g_chewingIMEClass[] = L"ChewingIme";
static ITfThreadMgr* g_threadMgr = NULL;
static TfClientId g_clientId = 0;
static Chewing::TextService* g_textService = NULL;

LRESULT CALLBACK imeUiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch(msg) {
	case WM_CREATE:
		return TRUE;
	}
	return ::DefWindowProc(hwnd, msg, wp, lp);
}

BOOL registerUIClass() {
	WNDCLASSEXW wc;
	wc.cbSize			= sizeof(WNDCLASSEXW);
	wc.style			= CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS| CS_IME;
	wc.lpfnWndProc		= (WNDPROC)imeUiWndProc;
	wc.cbClsExtra		= 0;
	wc.cbWndExtra		= 2 * sizeof(LONG);
	wc.hInstance		= g_imeModule->hInstance();
	wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
	wc.hIcon			= NULL;
	wc.lpszMenuName		= (LPTSTR)NULL;
	wc.lpszClassName	= g_chewingIMEClass;
	wc.hbrBackground	= NULL;
	wc.hIconSm			= NULL;
	if(!RegisterClassExW( (LPWNDCLASSEXW)&wc))
		return FALSE;
	return TRUE;
}

BOOL WINAPI ImeInquire(LPIMEINFO lpIMEInfo, LPTSTR lpszUIClass, LPCTSTR lpszOptions) {
	lpIMEInfo->fdwConversionCaps = IME_CMODE_NOCONVERSION|IME_CMODE_FULLSHAPE|IME_CMODE_CHINESE;
	lpIMEInfo->fdwSentenceCaps = IME_SMODE_NONE;
	lpIMEInfo->fdwUICaps = UI_CAP_2700;
	lpIMEInfo->fdwSCSCaps = 0;
	lpIMEInfo->fdwSelectCaps = SELECT_CAP_CONVERSION;
	lpIMEInfo->fdwProperty = IME_PROP_AT_CARET|IME_PROP_KBD_CHAR_FIRST|
							 IME_PROP_CANDLIST_START_FROM_1|IME_PROP_COMPLETE_ON_UNSELECT
							 |IME_PROP_END_UNLOAD|IME_PROP_UNICODE;
	wcscpy(lpszUIClass, g_chewingIMEClass);
	return TRUE;
}

BOOL WINAPI ImeConfigure(HKL hkl, HWND hWnd, DWORD dwMode, LPVOID pRegisterWord) {
	g_imeModule->onConfigure(hWnd);
	return TRUE;
}

DWORD WINAPI ImeConversionList(HIMC, LPCTSTR, LPCANDIDATELIST, DWORD dwBufLen, UINT uFlag) {
	return 0;
}

BOOL WINAPI ImeDestroy(UINT) {
	return TRUE;
}

LRESULT WINAPI ImeEscape(HIMC, UINT, LPVOID) {
	return FALSE;
}

#if 0

void ToggleChiEngMode(HIMC hIMC) {
	bool isChinese;
//	if(g_enableShift)
//	{
		DWORD conv, sentence;
		ImmGetConversionStatus(hIMC, &conv, &sentence);
		isChinese = !!(conv & IME_CMODE_CHINESE);
		if(isChinese)
			conv &= ~IME_CMODE_CHINESE;
		else
			conv |= IME_CMODE_CHINESE;
		ImmSetConversionStatus(hIMC, conv, sentence);
//	}
//	else
//	{
//		isChinese = !LOBYTE(GetKeyState(VK_CAPITAL));
//		BYTE scan = MapVirtualKey(VK_CAPITAL, 0);
//		keybd_event(VK_CAPITAL, MapVirtualKey(VK_CAPITAL, 0), 0, 0 );	// Capslock on/off
//		keybd_event(VK_CAPITAL, MapVirtualKey(VK_CAPITAL, 0), KEYEVENTF_KEYUP, 0);	// Capslock on/off
//	}

	isChinese = !isChinese;

	if(g_chewing) {
		if(isChinese)	// We need Chinese mode
		{
			if(!g_chewing->ChineseMode())	{// Chewing is in English mode
				g_chewing->Capslock();
				if(! LOBYTE(GetKeyState(VK_CAPITAL)) && g_enableCapsLock)	// no CapsLock
						g_chewing->Capslock();	// Switch Chewing to Chinese mode
			}
		}
		else	// We need English mode
		{
			if( g_chewing->ChineseMode())	// Chewing is in Chinese mode
				g_chewing->Capslock();	// Switch Chewing to English mode
		}
	}
}

void ToggleFullShapeMode(HIMC hIMC) {
	DWORD conv, sentence;
	ImmGetConversionStatus(hIMC, &conv, &sentence);
	bool isFullShape = !!(conv & IME_CMODE_FULLSHAPE);
	if(isFullShape)
		conv &= ~IME_CMODE_FULLSHAPE;
	else
		conv |= IME_CMODE_FULLSHAPE;
	ImmSetConversionStatus(hIMC, conv, sentence);
}
#endif

BOOL WINAPI ImeProcessKey(HIMC hIMC, UINT uVirKey, LPARAM lParam, CONST BYTE *lpbKeyState) {
	::OutputDebugStringW(L"ImeProcessKey\n");
	if(!hIMC)
		return FALSE;
	if(!g_textService)
		return FALSE;

	Ime::KeyEvent keyEvent(uVirKey, lParam, lpbKeyState);
	TfEditCookie cookie = 0;
	Ime::EditSession* session = new Ime::EditSession(g_textService, NULL);
	bool ret = g_textService->onKeyDown(keyEvent, session);
	session->Release();

	return ret;
}

BOOL WINAPI ImeSelect(HIMC hIMC, BOOL fSelect) {
	if(fSelect) {
		if(CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&g_threadMgr) == S_OK) {
			g_threadMgr->Activate(&g_clientId);
			if(!g_textService) {
				g_textService = new Chewing::TextService(g_imeModule, hIMC);
				g_textService->Activate(g_threadMgr, g_clientId);
			}
		}
	}
	else {
		if(g_textService) {
			g_textService->Deactivate();
			g_textService->Release();
			g_textService = NULL;
			g_clientId = 0;
		}
		if(g_threadMgr) {
			g_threadMgr->Deactivate();
			g_threadMgr->Release();
			g_threadMgr = NULL;
		}
	}
	return TRUE;
}

//  Activates or deactivates an input context and notifies the IME of the newly active input context. 
//  The IME can use the notification for initialization.
BOOL WINAPI ImeSetActiveContext(HIMC hIMC, BOOL fFlag) {
	::OutputDebugStringW(L"ImeSetActiveContext\n");
	return TRUE;
}

UINT WINAPI ImeToAsciiEx(UINT uVirtKey, UINT uScaCode, CONST LPBYTE lpbKeyState, LPDWORD lpdwTransBuf, UINT fuState, HIMC) {
	::OutputDebugStringW(L"ImeToAsciiEx\n");
	return FALSE;
}

BOOL WINAPI NotifyIME(HIMC hIMC, DWORD dwAction, DWORD dwIndex, DWORD dwValue) {
	if(!hIMC)
		return FALSE;
#if 0
	switch(dwAction) {
	case NI_OPENCANDIDATE:
		break;
	case NI_CLOSECANDIDATE:
		break;
	case NI_SELECTCANDIDATESTR:
		break;
	case NI_CHANGECANDIDATELIST:
		break;
	case NI_SETCANDIDATE_PAGESTART:
		break;
	case NI_SETCANDIDATE_PAGESIZE:
		break;
	case NI_CONTEXTUPDATED) {
			switch(dwValue) {
			case IMC_SETCANDIDATEPOS:
				break;
			case IMC_SETCOMPOSITIONFONT:
				break;
			case IMC_SETCOMPOSITIONWINDOW:
				break;
			case IMC_SETCONVERSIONMODE:
				break;
			case IMC_SETSENTENCEMODE:
				break;
			case IMC_SETOPENSTATUS :
				break;
			}
			break;
		}
	case NI_COMPOSITIONSTR) {
			if (g_isWinLogon)
				return FALSE;
			IMCLock imc(hIMC);
			CompStr* cs = imc.getCompStr();
			if(!cs)
				return FALSE;
			switch(dwIndex) {
			case CPS_COMPLETE:
				return CommitBuffer(imc);
				break;
			case CPS_CONVERT:
				break;
			case CPS_CANCEL:
				cs->setCompStr(L"");
				cs->setZuin(L"");
				break;
			}
		}
		break;
	}
#endif
	return TRUE;
}

BOOL WINAPI ImeRegisterWord(LPCTSTR, DWORD, LPCTSTR) {
	return 0;
}

BOOL WINAPI ImeUnregisterWord(LPCTSTR, DWORD, LPCTSTR) {
	return 0;
}

UINT WINAPI ImeGetRegisterWordStyle(UINT nItem, LPSTYLEBUF) {
	return 0;
}

DWORD WINAPI ImeGetImeMenuItems( HIMC  hIMC,  DWORD  dwFlags,  DWORD  dwType, LPIMEMENUITEMINFO  lpImeParentMenu, LPIMEMENUITEMINFO  lpImeMenu, DWORD  dwSize) {
	return 0;
}

UINT WINAPI ImeEnumRegisterWord(REGISTERWORDENUMPROC, LPCTSTR, DWORD, LPCTSTR, LPVOID) {
	return 0;
}

BOOL WINAPI ImeSetCompositionString(HIMC, DWORD dwIndex, LPCVOID lpComp, DWORD, LPCVOID lpRead, DWORD) {
	return FALSE;
}

void CALLBACK Install() {
	wchar_t path[MAX_PATH];
	::GetModuleFileNameW(g_imeModule->hInstance(), path, MAX_PATH);
	wchar_t name[] = L"����(�c��)-Chewing";
	HKL hkl = ::ImmInstallIMEW(path, name);
	DWORD err = ::GetLastError();
	wchar_t buf[1024];
	wsprintf(buf, L"file: '%s'\nname: '%s'\nhkl: %p\nErr: %d", path, name, hkl, err);
	::MessageBox(0, buf, 0, 0);
}

void CALLBACK Uninstall() {
	// TODO
}

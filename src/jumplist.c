
#if CYGWIN_VERSION_API_MINOR >= 74

#undef WINVER
#define WINVER 0x0601
#include "std.h"
//#undef NTDDI_VERSION
//#define NTDDI_VERSION NTDDI_WIN7
#ifndef ___PROCESSOR_NUMBER_DEFINED
#define ___PROCESSOR_NUMBER_DEFINED
typedef struct _PROCESSOR_NUMBER {
  WORD Group;
  BYTE Number;
  BYTE Reserved;
} PROCESSOR_NUMBER, *PPROCESSOR_NUMBER;
#endif

#include <shlobj.h>
#include <propkey.h>  // PKEY_Title

#include "jumplist.h"
#include "charset.h"
#include "config.h"


static inline wchar *
last_error()
{
  int err = GetLastError();
  if (err) {
    static wchar winmsg[1024];  // constant and < 1273 or 1705 => issue #530
    FormatMessageW(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
      0, err, 0, winmsg, lengthof(winmsg), 0
    );
    return winmsg;
  }
  else
    return W("");
}


static HRESULT
clear_jumplist(void)
{
  HRESULT hr = S_OK;
  ICustomDestinationList *pCustomDestinationList;

  hr = CoCreateInstance(&CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER, &IID_ICustomDestinationList, (void **)&pCustomDestinationList);
  if (FAILED(hr)) {
    return hr;
  }

  hr = pCustomDestinationList->lpVtbl->DeleteList((void *)pCustomDestinationList, NULL);

  pCustomDestinationList->lpVtbl->Release((void *)pCustomDestinationList);
  return hr;
}

static HRESULT
InitPropVariantFromString(PCWSTR psz, PROPVARIANT *ppropvar)
{
  HRESULT hres = 0;

  //hres = SHStrDupW(psz, &ppropvar->pwszVal);
  ppropvar->pwszVal = wcsdup(psz);
  if(SUCCEEDED(hres))
      ppropvar->vt = VT_LPWSTR;
  else
      PropVariantInit(ppropvar);

  return hres;
}

static HRESULT
register_task(IObjectCollection *pobjs, wstring title, wstring cmd, wstring icon, int ii)
{
  HRESULT hr = S_OK;
  IShellLinkW *pShellLink;

  if (!cmd || !*cmd)
    return S_OK;

  wstring show_title = (!title || !*title) ? cmd : title;

  wchar exe_path[MAX_PATH + 1];
  if (GetModuleFileNameW(NULL, exe_path, MAX_PATH) == 0)
    return S_FALSE;

  //printf("register_task <%ls>: <%ls> <%ls>\n", title, exe_path, cmd);
  hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&pShellLink);
  if (SUCCEEDED(hr)) {
    do {
      // set title
      IPropertyStore *pPropertyStore;
      hr = pShellLink->lpVtbl->QueryInterface((void *)pShellLink, &IID_IPropertyStore, (void **)&pPropertyStore);
      if (SUCCEEDED(hr)) {
        PROPVARIANT propVariant;

        hr = InitPropVariantFromString(show_title, &propVariant);
        if (SUCCEEDED(hr)) {
          hr = pPropertyStore->lpVtbl->SetValue((void *)pPropertyStore, &PKEY_Title, &propVariant);
          if (SUCCEEDED(hr)) {
            pPropertyStore->lpVtbl->Commit((void *)pPropertyStore);
          }
        }
      }
      if (FAILED(hr))
        break;

      // set icon path and index
      if (icon) {
        hr = pShellLink->lpVtbl->SetIconLocation((void *)pShellLink, icon, ii);
        if (FAILED(hr))
          break;
      }

      // set full path of mintty.exe
      hr = pShellLink->lpVtbl->SetPath((void *)pShellLink, exe_path);
      if (FAILED(hr))
        break;

      // set arguments
      hr = pShellLink->lpVtbl->SetArguments((void *)pShellLink, cmd);
      if (FAILED(hr))
        break;

      // finally, register this column into the jump list
      hr = pobjs->lpVtbl->AddObject((void *)pobjs, (IUnknown *)pShellLink);
    } while (0);

    pShellLink->lpVtbl->Release((void *)pShellLink);
  }

  return hr;
}

void *
init_jumplist()
{
  IObjectCollection * pobjs;
  HRESULT hr = CoCreateInstance(&CLSID_EnumerableObjectCollection, 
                                NULL, CLSCTX_INPROC_SERVER, 
                                &IID_IObjectCollection, (void **)&pobjs);
  //printf("create_jumplist CoCreateInstance %ld\n", (long)hr);
  if (SUCCEEDED(hr))
    return pobjs;
  else
    return 0;
}

static HRESULT
create_jumplist(wstring appid, IObjectCollection *pobjs)
{
  HRESULT hr = S_OK;
  ICustomDestinationList *pCustomDestinationList;

  hr = CoCreateInstance(&CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER, &IID_ICustomDestinationList, (void **)&pCustomDestinationList);
  //printf("create_jumplist CoCreateInstance %ld\n", (long)hr);
  if (SUCCEEDED(hr)) {
    // register all custom tasks
    hr = pCustomDestinationList->lpVtbl->SetAppID((void *)pCustomDestinationList, appid);
    //printf("create_jumplist SetAppID(%ls) %ld (%ld %ls)\n", appid, (long)hr, (long)GetLastError(), last_error());
    hr = S_OK;  // ignore failure of SetAppID
    if (SUCCEEDED(hr)) {
      UINT nSlots;
      IObjectArray *pRemovedList;

      hr = pCustomDestinationList->lpVtbl->BeginList((void *)pCustomDestinationList, &nSlots, &IID_IObjectArray, (void **)&pRemovedList);
      //printf("create_jumplist BeginList %ld\n", (long)hr);
      if (SUCCEEDED(hr)) {
        IObjectArray *pObjectArray;

        hr = pobjs->lpVtbl->QueryInterface((void *)pobjs, &IID_IObjectArray, (void **)&pObjectArray);
        if (SUCCEEDED(hr)) {
          hr = pCustomDestinationList->lpVtbl->AddUserTasks((void *)pCustomDestinationList, pObjectArray);
        //printf("create_jumplist AddUserTasks %ld\n", (long)hr);

          pObjectArray->lpVtbl->Release((void *)pObjectArray);
        }
        // should we commit only within previous SUCCEEDED?
        pCustomDestinationList->lpVtbl->CommitList((void *)pCustomDestinationList);
        //printf("create_jumplist CommitList %ld\n", (long)hr);

        pRemovedList->lpVtbl->Release((void *)pRemovedList);
      }
    }

    pCustomDestinationList->lpVtbl->Release((void *)pCustomDestinationList);
  }

  return hr;
}

HRESULT
setup_jumplist(wstring appid, int n, wstring titles[], wstring cmds[], wstring icons[], int ii[])
{
  OSVERSIONINFO ver;
  ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
  GetVersionEx(&ver);
  //printf("setup_jumplist\n");

  // if running under the machine older than Windows 7, silently return.
  if (!((ver.dwMajorVersion == 6 && ver.dwMinorVersion >= 1) || ver.dwMajorVersion >= 7)) {
    return S_OK;
  }

  HRESULT hr = S_OK;

  hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (FAILED(hr))
    return hr;

  hr = clear_jumplist();
  hr = S_OK;
  if (SUCCEEDED(hr)) {
    IObjectCollection * pobjs = init_jumplist();
    //printf("setup_jumplist items %p\n", pobjs);
    if (pobjs) {
      for (int i = 0; i < n; ++i) {
        hr = register_task(pobjs, titles[i], cmds[i], icons[i], ii[i]);
        if (FAILED(hr)) {
          break;
        }
      }
      if (SUCCEEDED(hr))
        hr = create_jumplist(appid, pobjs);

      pobjs->lpVtbl->Release((void *)pobjs);
    }
  }

  CoUninitialize();
  return hr;
}

#else

HRESULT
setup_jumplist(wstring appid, int n, wstring titles[], wstring cmds[], wstring icons[], int ii[])
{
  (void)appid; (void)n; (void)titles; (void)cmds; (void)icons; (void)ii;
  return S_OK;
}

#endif


/* $Id$ */
/** @file
 * IPRT - Utility for running a (simple) FTP server.
 */

/*
 * Copyright (C) 2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

/*
 * Use this setup to best see what's going on:
 *
 *    VBOX_LOG=rt_ftp=~0
 *    VBOX_LOG_DEST="nofile stderr"
 *    VBOX_LOG_FLAGS="unbuffered enabled thread msprog"
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <signal.h>

#include <iprt/ftp.h>

#include <iprt/net.h> /* To make use of IPv4Addr in RTGETOPTUNION. */

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/vfs.h>

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif


/*********************************************************************************************************************************
*   Definitations                                                                                                                *
*********************************************************************************************************************************/
typedef struct FTPSERVERDATA
{
    /** The absolute path of the FTP server's root directory. */
    char szPathRootAbs[RTPATH_MAX];
    /** The relative current working directory (CWD) to szRootDir. */
    char szCWD[RTPATH_MAX];
    RTFILE hFile;
} FTPSERVERDATA;
typedef FTPSERVERDATA *PFTPSERVERDATA;

typedef struct FTPDIRHANDLE
{
    /** The VFS (chain) handle to use for this directory. */
    RTVFSDIR hVfsDir;
} FTPDIRHANDLE;
typedef FTPDIRHANDLE *PFTPDIRHANDLE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Set by the signal handler when the FTP server shall be terminated. */
static volatile bool  g_fCanceled  = false;
static FTPSERVERDATA  g_FTPServerData;


#ifdef RT_OS_WINDOWS
static BOOL WINAPI signalHandler(DWORD dwCtrlType)
{
    bool fEventHandled = FALSE;
    switch (dwCtrlType)
    {
        /* User pressed CTRL+C or CTRL+BREAK or an external event was sent
         * via GenerateConsoleCtrlEvent(). */
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_C_EVENT:
            ASMAtomicWriteBool(&g_fCanceled, true);
            fEventHandled = TRUE;
            break;
        default:
            break;
        /** @todo Add other events here. */
    }

    return fEventHandled;
}
#else /* !RT_OS_WINDOWS */
/**
 * Signal handler that sets g_fCanceled.
 *
 * This can be executed on any thread in the process, on Windows it may even be
 * a thread dedicated to delivering this signal.  Don't do anything
 * unnecessary here.
 */
static void signalHandler(int iSignal)
{
    NOREF(iSignal);
    ASMAtomicWriteBool(&g_fCanceled, true);
}
#endif

/**
 * Installs a custom signal handler to get notified
 * whenever the user wants to intercept the program.
 *
 * @todo Make this handler available for all VBoxManage modules?
 */
static int signalHandlerInstall(void)
{
    g_fCanceled = false;

    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)signalHandler, TRUE /* Add handler */))
    {
        rc = RTErrConvertFromWin32(GetLastError());
        RTMsgError("Unable to install console control handler, rc=%Rrc\n", rc);
    }
#else
    signal(SIGINT,   signalHandler);
    signal(SIGTERM,  signalHandler);
# ifdef SIGBREAK
    signal(SIGBREAK, signalHandler);
# endif
#endif
    return rc;
}

/**
 * Uninstalls a previously installed signal handler.
 */
static int signalHandlerUninstall(void)
{
    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)NULL, FALSE /* Remove handler */))
    {
        rc = RTErrConvertFromWin32(GetLastError());
        RTMsgError("Unable to uninstall console control handler, rc=%Rrc\n", rc);
    }
#else
    signal(SIGINT,   SIG_DFL);
    signal(SIGTERM,  SIG_DFL);
# ifdef SIGBREAK
    signal(SIGBREAK, SIG_DFL);
# endif
#endif
    return rc;
}

static DECLCALLBACK(int) onUserConnect(PRTFTPCALLBACKDATA pData, const char *pcszUser)
{
    RT_NOREF(pData, pcszUser);

    RTPrintf("User '%s' connected\n", pcszUser);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) onUserAuthenticate(PRTFTPCALLBACKDATA pData, const char *pcszUser, const char *pcszPassword)
{
    RT_NOREF(pData, pcszUser, pcszPassword);

    RTPrintf("Authenticating user '%s' ...\n", pcszUser);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) onUserDisonnect(PRTFTPCALLBACKDATA pData, const char *pcszUser)
{
    RT_NOREF(pData);

    RTPrintf("User '%s' disconnected\n", pcszUser);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) onFileOpen(PRTFTPCALLBACKDATA pData, const char *pcszPath, uint32_t fMode, void **ppvHandle)
{
    RT_NOREF(ppvHandle);

    PFTPSERVERDATA pThis = (PFTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(FTPSERVERDATA));

    return RTFileOpen(&pThis->hFile, pcszPath, fMode);
}

static DECLCALLBACK(int) onFileRead(PRTFTPCALLBACKDATA pData, void *pvHandle, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RT_NOREF(pvHandle);

    PFTPSERVERDATA pThis = (PFTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(FTPSERVERDATA));

    return RTFileRead(pThis->hFile, pvBuf, cbToRead, pcbRead);
}

static DECLCALLBACK(int) onFileClose(PRTFTPCALLBACKDATA pData, void *pvHandle)
{
    RT_NOREF(pvHandle);

    PFTPSERVERDATA pThis = (PFTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(FTPSERVERDATA));

    int rc = RTFileClose(pThis->hFile);
    if (RT_SUCCESS(rc))
    {
        pThis->hFile = NIL_RTFILE;
    }

    return rc;
}

static DECLCALLBACK(int) onFileGetSize(PRTFTPCALLBACKDATA pData, const char *pcszPath, uint64_t *puSize)
{
    PFTPSERVERDATA pThis = (PFTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(FTPSERVERDATA));

    char *pszStat = NULL;
    if (RTStrAPrintf(&pszStat, "%s/%s", pThis->szPathRootAbs, pcszPath) <= 0)
        return VERR_NO_MEMORY;

    RTPrintf("Retrieving file size for '%s' ...\n", pcszPath);

    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pcszPath,
                        RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        rc = RTFileQuerySize(hFile, puSize);
        if (RT_SUCCESS(rc))
            RTPrintf("File size is: %RU64\n", *puSize);
        RTFileClose(hFile);
    }

    RTStrFree(pszStat);

    return rc;
}

static DECLCALLBACK(int) onFileStat(PRTFTPCALLBACKDATA pData, const char *pcszPath, PRTFSOBJINFO pFsObjInfo)
{
    PFTPSERVERDATA pThis = (PFTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(FTPSERVERDATA));

    char *pszStat = NULL;
    if (RTStrAPrintf(&pszStat, "%s/%s", pThis->szPathRootAbs, pcszPath) <= 0)
        return VERR_NO_MEMORY;

    RTPrintf("Stat for '%s'\n", pszStat);

    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszStat,
                        RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO fsObjInfo;
        rc = RTFileQueryInfo(hFile, &fsObjInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc))
        {
            if (pFsObjInfo)
                *pFsObjInfo = fsObjInfo;
        }

        RTFileClose(hFile);
    }

    RTStrFree(pszStat);

    return rc;
}

static DECLCALLBACK(int) onPathSetCurrent(PRTFTPCALLBACKDATA pData, const char *pcszCWD)
{
    PFTPSERVERDATA pThis = (PFTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(FTPSERVERDATA));

    RTPrintf("Setting current directory to '%s'\n", pcszCWD);

    /** @todo BUGBUG Santiy checks! */

    return RTStrCopy(pThis->szCWD, sizeof(pThis->szCWD), pcszCWD);
}

static DECLCALLBACK(int) onPathGetCurrent(PRTFTPCALLBACKDATA pData, char *pszPWD, size_t cbPWD)
{
    PFTPSERVERDATA pThis = (PFTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(FTPSERVERDATA));

    RTPrintf("Current directory is: '%s'\n", pThis->szCWD);

    return RTStrCopy(pszPWD, cbPWD, pThis->szCWD);
}

static DECLCALLBACK(int) onPathUp(PRTFTPCALLBACKDATA pData)
{
    RT_NOREF(pData);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) onDirOpen(PRTFTPCALLBACKDATA pData, const char *pcszPath, void **ppvHandle)
{
    PFTPSERVERDATA pThis = (PFTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(FTPSERVERDATA));

    PFTPDIRHANDLE pHandle = (PFTPDIRHANDLE)RTMemAllocZ(sizeof(FTPDIRHANDLE));
    if (!pHandle)
        return VERR_NO_MEMORY;

    /* Construct absolute path. */
    char *pszPathAbs = NULL;
    if (RTStrAPrintf(&pszPathAbs, "%s/%s", pThis->szPathRootAbs, pcszPath) <= 0)
        return VERR_NO_MEMORY;

    RTPrintf("Opening directory '%s'\n", pszPathAbs);

    int rc = RTVfsChainOpenDir(pszPathAbs, 0 /*fFlags*/, &pHandle->hVfsDir, NULL /* poffError */, NULL /* pErrInfo */);
    if (RT_SUCCESS(rc))
    {
        *ppvHandle = pHandle;
    }
    else
    {
        RTMemFree(pHandle);
    }

    RTStrFree(pszPathAbs);

    return rc;
}

static DECLCALLBACK(int) onDirClose(PRTFTPCALLBACKDATA pData, void *pvHandle)
{
    RT_NOREF(pData);

    PFTPDIRHANDLE pHandle = (PFTPDIRHANDLE)pvHandle;
    AssertPtrReturn(pHandle, VERR_INVALID_POINTER);

    RTVfsDirRelease(pHandle->hVfsDir);

    RTMemFree(pHandle);
    pHandle = NULL;

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) onDirRead(PRTFTPCALLBACKDATA pData, void *pvHandle, char **ppszEntry,
                                   PRTFSOBJINFO pInfo, char **ppszOwner, char **ppszGroup, char **ppszTarget)
{
    RT_NOREF(pData);
    RT_NOREF(ppszTarget); /* No symlinks yet */

    PFTPDIRHANDLE pHandle = (PFTPDIRHANDLE)pvHandle;
    AssertPtrReturn(pHandle, VERR_INVALID_POINTER);

    size_t          cbDirEntryAlloced = sizeof(RTDIRENTRYEX);
    PRTDIRENTRYEX   pDirEntry         = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntryAlloced);
    if (!pDirEntry)
        return VERR_NO_MEMORY;

    int rc;

    for (;;)
    {
        size_t cbDirEntry = cbDirEntryAlloced;
        rc = RTVfsDirReadEx(pHandle->hVfsDir, pDirEntry, &cbDirEntry, RTFSOBJATTRADD_UNIX);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_BUFFER_OVERFLOW)
            {
                RTMemTmpFree(pDirEntry);
                cbDirEntryAlloced = RT_ALIGN_Z(RT_MIN(cbDirEntry, cbDirEntryAlloced) + 64, 64);
                pDirEntry  = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntryAlloced);
                if (pDirEntry)
                    continue;
            }
            else if (rc != VERR_NO_MORE_FILES)
                break;
        }

        if (RT_SUCCESS(rc))
        {
            if (pDirEntry->Info.Attr.u.Unix.uid != NIL_RTUID)
            {
                RTFSOBJINFO OwnerInfo;
                rc = RTVfsDirQueryPathInfo(pHandle->hVfsDir,
                                           pDirEntry->szName, &OwnerInfo, RTFSOBJATTRADD_UNIX_OWNER, RTPATH_F_ON_LINK);
                if (   RT_SUCCESS(rc)
                    && OwnerInfo.Attr.u.UnixOwner.szName[0])
                {
                    *ppszOwner = RTStrDup(&OwnerInfo.Attr.u.UnixOwner.szName[0]);
                    if (!*ppszOwner)
                        rc = VERR_NO_MEMORY;
                }
            }

            if (   RT_SUCCESS(rc)
                && pDirEntry->Info.Attr.u.Unix.gid != NIL_RTGID)
            {
                RTFSOBJINFO GroupInfo;
                rc = RTVfsDirQueryPathInfo(pHandle->hVfsDir,
                                           pDirEntry->szName, &GroupInfo, RTFSOBJATTRADD_UNIX_GROUP, RTPATH_F_ON_LINK);
                if (   RT_SUCCESS(rc)
                    && GroupInfo.Attr.u.UnixGroup.szName[0])
                {
                    *ppszGroup = RTStrDup(&GroupInfo.Attr.u.UnixGroup.szName[0]);
                    if (!*ppszGroup)
                        rc = VERR_NO_MEMORY;
                }
            }
        }

        *ppszEntry = RTStrDup(pDirEntry->szName);
        AssertPtrReturn(*ppszEntry, VERR_NO_MEMORY);

        *pInfo = pDirEntry->Info;

        break;

    } /* for */

    RTMemTmpFree(pDirEntry);
    pDirEntry = NULL;

    return rc;
}

int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /* Use some sane defaults. */
    char     szAddress[64] = "localhost";
    uint16_t uPort         = 2121;

    RT_ZERO(g_FTPServerData);

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--address",      'a', RTGETOPT_REQ_IPV4ADDR }, /** @todo Use a string for DNS hostnames? */
        /** @todo Implement IPv6 support? */
        { "--port",         'p', RTGETOPT_REQ_UINT16 },
        { "--root-dir",     'r', RTGETOPT_REQ_STRING },
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING }
    };

    RTEXITCODE      rcExit          = RTEXITCODE_SUCCESS;
    unsigned        uVerbosityLevel = 1;

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            case 'a':
                RTStrPrintf2(szAddress, sizeof(szAddress), "%RU8.%RU8.%RU8.%RU8", /** @todo Improve this. */
                             ValueUnion.IPv4Addr.au8[0], ValueUnion.IPv4Addr.au8[1], ValueUnion.IPv4Addr.au8[2], ValueUnion.IPv4Addr.au8[3]);
                break;

            case 'p':
                uPort = ValueUnion.u16;
                break;

            case 'r':
                RTStrCopy(g_FTPServerData.szPathRootAbs, sizeof(g_FTPServerData.szPathRootAbs), ValueUnion.psz);
                break;

            case 'v':
                uVerbosityLevel++;
                break;

            case 'h':
                RTPrintf("Usage: %s [options]\n"
                         "\n"
                         "Options:\n"
                         "  -a, --address (default: localhost)\n"
                         "      Specifies the address to use for listening.\n"
                         "  -p, --port (default: 2121)\n"
                         "      Specifies the port to use for listening.\n"
                         "  -r, --root-dir (default: current dir)\n"
                         "      Specifies the root directory being served.\n"
                         "  -v, --verbose\n"
                         "      Controls the verbosity level.\n"
                         "  -h, -?, --help\n"
                         "      Display this help text and exit successfully.\n"
                         "  -V, --version\n"
                         "      Display the revision and exit successfully.\n"
                         , RTPathFilename(argv[0]));
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("$Revision$\n");
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    if (!strlen(g_FTPServerData.szPathRootAbs))
    {
        /* By default use the current directory as serving root directory. */
        rc = RTPathGetCurrent(g_FTPServerData.szPathRootAbs, sizeof(g_FTPServerData.szPathRootAbs));
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Retrieving current directory failed: %Rrc", rc);
    }

    /* Initialize CWD. */
    RTStrPrintf2(g_FTPServerData.szCWD, sizeof(g_FTPServerData.szCWD), "/");

    /* Install signal handler. */
    rc = signalHandlerInstall();
    if (RT_SUCCESS(rc))
    {
        /*
         * Create the FTP server instance.
         */
        RTFTPSERVERCALLBACKS Callbacks;
        RT_ZERO(Callbacks);

        Callbacks.pfnOnUserConnect      = onUserConnect;
        Callbacks.pfnOnUserAuthenticate = onUserAuthenticate;
        Callbacks.pfnOnUserDisconnect   = onUserDisonnect;
        Callbacks.pfnOnFileOpen         = onFileOpen;
        Callbacks.pfnOnFileRead         = onFileRead;
        Callbacks.pfnOnFileClose        = onFileClose;
        Callbacks.pfnOnFileGetSize      = onFileGetSize;
        Callbacks.pfnOnFileStat         = onFileStat;
        Callbacks.pfnOnPathSetCurrent   = onPathSetCurrent;
        Callbacks.pfnOnPathGetCurrent   = onPathGetCurrent;
        Callbacks.pfnOnPathUp           = onPathUp;
        Callbacks.pfnOnDirOpen          = onDirOpen;
        Callbacks.pfnOnDirClose         = onDirClose;
        Callbacks.pfnOnDirRead          = onDirRead;

        RTFTPSERVER hFTPServer;
        rc = RTFtpServerCreate(&hFTPServer, szAddress, uPort, &Callbacks,
                               &g_FTPServerData, sizeof(g_FTPServerData));
        if (RT_SUCCESS(rc))
        {
            RTPrintf("Starting FTP server at %s:%RU16 ...\n", szAddress, uPort);
            RTPrintf("Root directory is '%s'\n", g_FTPServerData.szPathRootAbs);

            RTPrintf("Running FTP server ...\n");

            for (;;)
            {
                RTThreadSleep(200);

                if (g_fCanceled)
                    break;
            }

            RTPrintf("Stopping FTP server ...\n");

            int rc2 = RTFtpServerDestroy(hFTPServer);
            if (RT_SUCCESS(rc))
                rc = rc2;

            RTPrintf("Stopped FTP server\n");
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTFTPServerCreate failed: %Rrc", rc);

        int rc2 = signalHandlerUninstall();
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    /* Set rcExit on failure in case we forgot to do so before. */
    if (RT_FAILURE(rc))
        rcExit = RTEXITCODE_FAILURE;

    return rcExit;
}


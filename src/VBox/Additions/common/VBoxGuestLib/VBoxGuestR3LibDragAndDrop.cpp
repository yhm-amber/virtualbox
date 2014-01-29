/* $Id$ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Drag & Drop.
 */

/*
 * Copyright (C) 2011-2013 Oracle Corporation
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/path.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/uri.h>
#include <iprt/thread.h>

#include <iprt/cpp/list.h>
#include <iprt/cpp/ministring.h>

#include "VBGLR3Internal.h"
#include "VBox/HostServices/DragAndDropSvc.h"

/* Here all the communication with the host over HGCM is handled platform
 * neutral. Also the receiving of URIs content (directory trees and files) is
 * done here. So the platform code of the guests, should not take care of that.
 *
 * Todo:
 * - Sending dirs/files in the G->H case
 * - Maybe the EOL converting of text mime-types (not fully sure, eventually
 *   better done on the host side)
 */

static int vbglR3DnDPathSanitize(char *pszPath, size_t cbPath);

/******************************************************************************
 *    Private internal functions                                              *
 ******************************************************************************/

static int vbglR3DnDCreateDropDir(char* pszDropDir, size_t cbSize)
{
    AssertPtrReturn(pszDropDir, VERR_INVALID_POINTER);
    AssertReturn(cbSize,        VERR_INVALID_PARAMETER);

    /** @todo On Windows we also could use the registry to override
     *        this path, on Posix a dotfile and/or a guest property
     *        can be used. */

    /* Get the users temp directory. Don't use the user's root directory (or
     * something inside it) because we don't know for how long/if the data will
     * be kept after the guest OS used it. */
    int rc = RTPathTemp(pszDropDir, cbSize);
    if (RT_FAILURE(rc))
        return rc;

    /* Append our base drop directory. */
    rc = RTPathAppend(pszDropDir, cbSize, "VirtualBox Dropped Files");
    if (RT_FAILURE(rc))
        return rc;

    /* Create it when necessary. */
    if (!RTDirExists(pszDropDir))
    {
        rc = RTDirCreateFullPath(pszDropDir, RTFS_UNIX_IRWXU);
        if (RT_FAILURE(rc))
            return rc;
    }

    /* The actually drop directory consist of the current time stamp and a
     * unique number when necessary. */
    char pszTime[64];
    RTTIMESPEC time;
    if (!RTTimeSpecToString(RTTimeNow(&time), pszTime, sizeof(pszTime)))
        return VERR_BUFFER_OVERFLOW;
    rc = vbglR3DnDPathSanitize(pszTime, sizeof(pszTime));
    if (RT_FAILURE(rc))
        return rc;

    rc = RTPathAppend(pszDropDir, cbSize, pszTime);
    if (RT_FAILURE(rc))
        return rc;

    /* Create it (only accessible by the current user) */
    return RTDirCreateUniqueNumbered(pszDropDir, cbSize, RTFS_UNIX_IRWXU, 3, '-');
}

static int vbglR3DnDQueryNextHostMessageType(uint32_t uClientId, uint32_t *puMsg, uint32_t *pcParms, bool fWait)
{
    AssertPtrReturn(puMsg,   VERR_INVALID_POINTER);
    AssertPtrReturn(pcParms, VERR_INVALID_POINTER);

    DragAndDropSvc::VBOXDNDNEXTMSGMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.result      = VERR_WRONG_ORDER;
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = DragAndDropSvc::GUEST_DND_GET_NEXT_HOST_MSG;
    Msg.hdr.cParms      = 3;

    Msg.msg.SetUInt32(0);
    Msg.num_parms.SetUInt32(0);
    Msg.block.SetUInt32(fWait);

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.hdr.result;
        if (RT_SUCCESS(rc))
        {
            /* Fetch results */
            rc = Msg.msg.GetUInt32(puMsg);         AssertRC(rc);
            rc = Msg.num_parms.GetUInt32(pcParms); AssertRC(rc);
        }
    }

    return rc;
}

static int vbglR3DnDHGProcessActionMessage(uint32_t  uClientId,
                                           uint32_t  uMsg,
                                           uint32_t *puScreenId,
                                           uint32_t *puX,
                                           uint32_t *puY,
                                           uint32_t *puDefAction,
                                           uint32_t *puAllActions,
                                           char     *pszFormats,
                                           uint32_t  cbFormats,
                                           uint32_t *pcbFormatsRecv)
{
    AssertPtrReturn(puScreenId,     VERR_INVALID_POINTER);
    AssertPtrReturn(puX,            VERR_INVALID_POINTER);
    AssertPtrReturn(puY,            VERR_INVALID_POINTER);
    AssertPtrReturn(puDefAction,    VERR_INVALID_POINTER);
    AssertPtrReturn(puAllActions,   VERR_INVALID_POINTER);
    AssertPtrReturn(pszFormats,     VERR_INVALID_POINTER);
    AssertReturn(cbFormats,         VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbFormatsRecv, VERR_INVALID_POINTER);

    DragAndDropSvc::VBOXDNDHGACTIONMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = uMsg;
    Msg.hdr.cParms      = 7;

    Msg.uScreenId.SetUInt32(0);
    Msg.uX.SetUInt32(0);
    Msg.uY.SetUInt32(0);
    Msg.uDefAction.SetUInt32(0);
    Msg.uAllActions.SetUInt32(0);
    Msg.pvFormats.SetPtr(pszFormats, cbFormats);
    Msg.cFormats.SetUInt32(0);

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.hdr.result;
        if (RT_SUCCESS(rc))
        {
            /* Fetch results */
            rc = Msg.uScreenId.GetUInt32(puScreenId);     AssertRC(rc);
            rc = Msg.uX.GetUInt32(puX);                   AssertRC(rc);
            rc = Msg.uY.GetUInt32(puY);                   AssertRC(rc);
            rc = Msg.uDefAction.GetUInt32(puDefAction);   AssertRC(rc);
            rc = Msg.uAllActions.GetUInt32(puAllActions); AssertRC(rc);
            rc = Msg.cFormats.GetUInt32(pcbFormatsRecv);  AssertRC(rc);
            /* A little bit paranoia */
            AssertReturn(cbFormats >= *pcbFormatsRecv, VERR_TOO_MUCH_DATA);
        }
    }

    return rc;
}

static int vbglR3DnDHGProcessLeaveMessage(uint32_t uClientId)
{
    DragAndDropSvc::VBOXDNDHGLEAVEMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = DragAndDropSvc::HOST_DND_HG_EVT_LEAVE;
    Msg.hdr.cParms      = 0;

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
        rc = Msg.hdr.result;

    return rc;
}

static int vbglR3DnDHGProcessCancelMessage(uint32_t uClientId)
{
    DragAndDropSvc::VBOXDNDHGCANCELMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = DragAndDropSvc::HOST_DND_HG_EVT_CANCEL;
    Msg.hdr.cParms      = 0;

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
        rc = Msg.hdr.result;

    return rc;
}

static int vbglR3DnDHGProcessSendDirMessage(uint32_t  uClientId,
                                            char     *pszDirname,
                                            uint32_t  cbDirname,
                                            uint32_t *pcbDirnameRecv,
                                            uint32_t *pfMode)
{
    AssertPtrReturn(pszDirname,     VERR_INVALID_POINTER);
    AssertReturn(cbDirname,         VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbDirnameRecv, VERR_INVALID_POINTER);
    AssertPtrReturn(pfMode,         VERR_INVALID_POINTER);

    DragAndDropSvc::VBOXDNDHGSENDDIRMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = DragAndDropSvc::HOST_DND_HG_SND_DIR;
    Msg.hdr.cParms      = 3;

    Msg.pvName.SetPtr(pszDirname, cbDirname);
    Msg.cName.SetUInt32(0);
    Msg.fMode.SetUInt32(0);

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.hdr.result;
        if (RT_SUCCESS(Msg.hdr.result))
        {
            /* Fetch results */
            rc = Msg.cName.GetUInt32(pcbDirnameRecv); AssertRC(rc);
            rc = Msg.fMode.GetUInt32(pfMode);         AssertRC(rc);
            /* A little bit paranoia */
            AssertReturn(cbDirname >= *pcbDirnameRecv, VERR_TOO_MUCH_DATA);
        }
    }

    return rc;
}

static int vbglR3DnDHGProcessSendFileMessage(uint32_t  uClientId,
                                             char     *pszFilename,
                                             uint32_t  cbFilename,
                                             uint32_t *pcbFilenameRecv,
                                             void     *pvData,
                                             uint32_t  cbData,
                                             uint32_t *pcbDataRecv,
                                             uint32_t *pfMode)
{
    AssertPtrReturn(pszFilename,     VERR_INVALID_POINTER);
    AssertReturn(cbFilename,         VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbFilenameRecv, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,          VERR_INVALID_POINTER);
    AssertReturn(cbData,             VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbDataRecv,     VERR_INVALID_POINTER);
    AssertPtrReturn(pfMode,          VERR_INVALID_POINTER);

    DragAndDropSvc::VBOXDNDHGSENDFILEMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = DragAndDropSvc::HOST_DND_HG_SND_FILE;
    Msg.hdr.cParms      = 5;

    Msg.pvName.SetPtr(pszFilename, cbFilename);
    Msg.cName.SetUInt32(0);
    Msg.pvData.SetPtr(pvData, cbData);
    Msg.cData.SetUInt32(0);
    Msg.fMode.SetUInt32(0);

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.hdr.result;
        if (RT_SUCCESS(rc))
        {
            /* Fetch results */
            rc = Msg.cName.GetUInt32(pcbFilenameRecv); AssertRC(rc);
            rc = Msg.cData.GetUInt32(pcbDataRecv);     AssertRC(rc);
            rc = Msg.fMode.GetUInt32(pfMode);          AssertRC(rc);
            /* A little bit paranoia */
            AssertReturn(cbFilename >= *pcbFilenameRecv, VERR_TOO_MUCH_DATA);
            AssertReturn(cbData     >= *pcbDataRecv,     VERR_TOO_MUCH_DATA);
        }
    }

    return rc;
}

static int vbglR3DnDHGProcessURIMessages(uint32_t   uClientId,
                                         uint32_t  *puScreenId,
                                         char      *pszFormat,
                                         uint32_t   cbFormat,
                                         uint32_t  *pcbFormatRecv,
                                         void     **ppvData,
                                         uint32_t   cbData,
                                         size_t    *pcbDataRecv)
{
    AssertPtrReturn(ppvData, VERR_INVALID_POINTER);
    AssertPtrReturn(cbData, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbDataRecv, VERR_INVALID_POINTER);

    /* Make a string list out of the uri data. */
    RTCList<RTCString> uriList = RTCString(static_cast<char*>(*ppvData), *pcbDataRecv - 1).split("\r\n");
    if (uriList.isEmpty())
        return VINF_SUCCESS;

    uint32_t cbTmpData = _1M * 10; /** @todo r=andy 10MB, uh, really?? */
    void *pvTmpData = RTMemAlloc(cbTmpData);
    if (!pvTmpData)
        return VERR_NO_MEMORY;

    /* Create and query the drop target directory. */
    char pszDropDir[RTPATH_MAX];
    int rc = vbglR3DnDCreateDropDir(pszDropDir, sizeof(pszDropDir));
    if (RT_FAILURE(rc))
    {
        RTMemFree(pvTmpData);
        return rc;
    }

    /* Patch the old drop data with the new drop directory, so the drop target
     * can find the files. */
    RTCList<RTCString> guestUriList;
    for (size_t i = 0; i < uriList.size(); ++i)
    {
        const RTCString &strUri = uriList.at(i);
        /* Query the path component of a file URI. If this hasn't a
         * file scheme, null is returned. */
        char *pszFilePath = RTUriFilePath(strUri.c_str(), URI_FILE_FORMAT_AUTO);
        if (pszFilePath)
        {
            rc = vbglR3DnDPathSanitize(pszFilePath, strlen(pszFilePath));
            if (RT_FAILURE(rc))
                break;

            /** @todo Use RTPathJoin? */
            RTCString strFullPath = RTCString().printf("%s%c%s", pszDropDir, RTPATH_SLASH, pszFilePath);
            char *pszNewUri = RTUriFileCreate(strFullPath.c_str());
            if (pszNewUri)
            {
                guestUriList.append(pszNewUri);
                RTStrFree(pszNewUri);
            }
        }
        else
            guestUriList.append(strUri);
    }

    if (RT_SUCCESS(rc))
    {
        /* Cleanup the old data and write the new data back to the event. */
        RTMemFree(*ppvData);
        RTCString newData = RTCString::join(guestUriList, "\r\n") + "\r\n";

        *ppvData = RTStrDupN(newData.c_str(), newData.length());
        *pcbDataRecv = newData.length() + 1;
    }

    /* Lists for holding created files & directories in the case of a
     * rollback. */
    RTCList<RTCString> guestDirList;
    RTCList<RTCString> guestFileList;
    char pszPathname[RTPATH_MAX];
    uint32_t cbPathname = 0;
    bool fLoop = RT_SUCCESS(rc); /* No error occurred yet? */
    while (fLoop)
    {
        uint32_t uNextMsg;
        uint32_t cNextParms;
        rc = vbglR3DnDQueryNextHostMessageType(uClientId, &uNextMsg, &cNextParms, false /* fWait */);
        if (RT_SUCCESS(rc))
        {
            switch (uNextMsg)
            {
                case DragAndDropSvc::HOST_DND_HG_SND_DIR:
                {
                    uint32_t fMode = 0;
                    rc = vbglR3DnDHGProcessSendDirMessage(uClientId,
                                                          pszPathname,
                                                          sizeof(pszPathname),
                                                          &cbPathname,
                                                          &fMode);
                    if (RT_SUCCESS(rc))
                        rc = vbglR3DnDPathSanitize(pszPathname, sizeof(pszPathname));
                    if (RT_SUCCESS(rc))
                    {
                        char *pszNewDir = RTPathJoinA(pszDropDir, pszPathname);
                        if (pszNewDir)
                        {
                            rc = RTDirCreate(pszNewDir, (fMode & RTFS_UNIX_MASK) | RTFS_UNIX_IRWXU, 0);
                            if (!guestDirList.contains(pszNewDir))
                                guestDirList.append(pszNewDir);

                            RTStrFree(pszNewDir);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                    break;
                }
                case DragAndDropSvc::HOST_DND_HG_SND_FILE:
                {
                    uint32_t cbDataRecv;
                    uint32_t fMode = 0;
                    rc = vbglR3DnDHGProcessSendFileMessage(uClientId,
                                                           pszPathname,
                                                           sizeof(pszPathname),
                                                           &cbPathname,
                                                           pvTmpData,
                                                           cbTmpData,
                                                           &cbDataRecv,
                                                           &fMode);
                    if (RT_SUCCESS(rc))
                        rc = vbglR3DnDPathSanitize(pszPathname, sizeof(pszPathname));
                    if (RT_SUCCESS(rc))
                    {
                        char *pszNewFile = RTPathJoinA(pszDropDir, pszPathname);
                        if (pszNewFile)
                        {
                            RTFILE hFile;
                            /** @todo r=andy Keep the file open and locked during the actual file transfer. Otherwise this will
                             *               create all sorts of funny races because we don't know if the guest has
                             *               modified the file in between the file data send calls. */
                            rc = RTFileOpen(&hFile, pszNewFile,
                                            RTFILE_O_WRITE | RTFILE_O_APPEND | RTFILE_O_DENY_ALL | RTFILE_O_OPEN_CREATE);
                            if (RT_SUCCESS(rc))
                            {
                                rc = RTFileSeek(hFile, 0, RTFILE_SEEK_END, NULL);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = RTFileWrite(hFile, pvTmpData, cbDataRecv, 0);
                                    /* Valid UNIX mode? */
                                    if (   RT_SUCCESS(rc)
                                        && (fMode & RTFS_UNIX_MASK))
                                        rc = RTFileSetMode(hFile, (fMode & RTFS_UNIX_MASK) | RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR);
                                }
                                RTFileClose(hFile);
                                if (!guestFileList.contains(pszNewFile))
                                    guestFileList.append(pszNewFile);
                            }

                            RTStrFree(pszNewFile);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                    break;
                }
                case DragAndDropSvc::HOST_DND_HG_EVT_CANCEL:
                {
                    rc = vbglR3DnDHGProcessCancelMessage(uClientId);
                    if (RT_SUCCESS(rc))
                        rc = VERR_CANCELLED;
                    /* Break out of the loop. */
                }
                default:
                    fLoop = false;
                    break;
            }
        }
        else
        {
            if (rc == VERR_NO_DATA)
                rc = VINF_SUCCESS;
            break;
        }
    } /* while */

    RTMemFree(pvTmpData);

    /* Cleanup on failure or if the user has canceled. */
    if (RT_FAILURE(rc))
    {
        /* Remove any stuff created. */
        for (size_t i = 0; i < guestFileList.size(); ++i)
            RTFileDelete(guestFileList.at(i).c_str());
        for (size_t i = 0; i < guestDirList.size(); ++i)
            RTDirRemove(guestDirList.at(i).c_str());
        RTDirRemove(pszDropDir);
    }

    return rc;
}

static int vbglR3DnDHGProcessDataMessageInternal(uint32_t  uClientId,
                                                 uint32_t *puScreenId,
                                                 char     *pszFormat,
                                                 uint32_t  cbFormat,
                                                 uint32_t *pcbFormatRecv,
                                                 void     *pvData,
                                                 uint32_t  cbData,
                                                 uint32_t *pcbDataRecv)
{
    AssertPtrReturn(puScreenId,    VERR_INVALID_POINTER);
    AssertPtrReturn(pszFormat,     VERR_INVALID_POINTER);
    AssertReturn(cbFormat,         VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbFormatRecv, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,        VERR_INVALID_POINTER);
    AssertReturn(cbData,           VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbDataRecv,   VERR_INVALID_POINTER);

    DragAndDropSvc::VBOXDNDHGSENDDATAMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = DragAndDropSvc::HOST_DND_HG_SND_DATA;
    Msg.hdr.cParms      = 5;

    Msg.uScreenId.SetUInt32(0);
    Msg.pvFormat.SetPtr(pszFormat, cbFormat);
    Msg.cFormat.SetUInt32(0);
    Msg.pvData.SetPtr(pvData, cbData);
    Msg.cData.SetUInt32(0);

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.hdr.result;
        if (RT_SUCCESS(rc)
            || rc == VERR_BUFFER_OVERFLOW)
        {
            /* Fetch results */
            rc = Msg.uScreenId.GetUInt32(puScreenId);  AssertRC(rc);
            rc = Msg.cFormat.GetUInt32(pcbFormatRecv); AssertRC(rc);
            rc = Msg.cData.GetUInt32(pcbDataRecv);     AssertRC(rc);
            /* A little bit paranoia */
            AssertReturn(cbFormat >= *pcbFormatRecv, VERR_TOO_MUCH_DATA);
            AssertReturn(cbData   >= *pcbDataRecv,   VERR_TOO_MUCH_DATA);
        }
    }

    return rc;
}

static int vbglR3DnDHGProcessMoreDataMessageInternal(uint32_t  uClientId,
                                                     void     *pvData,
                                                     uint32_t  cbData,
                                                     uint32_t *pcbDataRecv)
{
    AssertPtrReturn(pvData,      VERR_INVALID_POINTER);
    AssertReturn(cbData,         VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbDataRecv, VERR_INVALID_POINTER);

    DragAndDropSvc::VBOXDNDHGSENDMOREDATAMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = DragAndDropSvc::HOST_DND_HG_SND_MORE_DATA;
    Msg.hdr.cParms      = 2;

    Msg.pvData.SetPtr(pvData, cbData);
    Msg.cData.SetUInt32(0);

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.hdr.result;
        if (   RT_SUCCESS(rc)
            || rc == VERR_BUFFER_OVERFLOW)
        {
            /* Fetch results */
            rc = Msg.cData.GetUInt32(pcbDataRecv); AssertRC(rc);
            /* A little bit paranoia */
            AssertReturn(cbData >= *pcbDataRecv, VERR_TOO_MUCH_DATA);
        }
    }
    return rc;
}

static int vbglR3DnDHGProcessSendDataMessageLoop(uint32_t  uClientId,
                                                 uint32_t *puScreenId,
                                                 char     *pszFormat,
                                                 uint32_t  cbFormat,
                                                 uint32_t *pcbFormatRecv,
                                                 void    **ppvData,
                                                 uint32_t  cbData,
                                                 size_t   *pcbDataRecv)
{
    uint32_t cbDataRecv = 0;
    int rc = vbglR3DnDHGProcessDataMessageInternal(uClientId,
                                                   puScreenId,
                                                   pszFormat,
                                                   cbFormat,
                                                   pcbFormatRecv,
                                                   *ppvData,
                                                   cbData,
                                                   &cbDataRecv);
    uint32_t cbAllDataRecv = cbDataRecv;
    while (rc == VERR_BUFFER_OVERFLOW)
    {
        uint32_t uNextMsg;
        uint32_t cNextParms;
        rc = vbglR3DnDQueryNextHostMessageType(uClientId, &uNextMsg, &cNextParms, false);
        if (RT_SUCCESS(rc))
        {
            switch(uNextMsg)
            {
                case DragAndDropSvc::HOST_DND_HG_SND_MORE_DATA:
                {
                    *ppvData = RTMemRealloc(*ppvData, cbAllDataRecv + cbData);
                    if (!*ppvData)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }
                    rc = vbglR3DnDHGProcessMoreDataMessageInternal(uClientId,
                                                                   &((char*)*ppvData)[cbAllDataRecv],
                                                                   cbData,
                                                                   &cbDataRecv);
                    cbAllDataRecv += cbDataRecv;
                    break;
                }
                case DragAndDropSvc::HOST_DND_HG_EVT_CANCEL:
                default:
                {
                    rc = vbglR3DnDHGProcessCancelMessage(uClientId);
                    if (RT_SUCCESS(rc))
                        rc = VERR_CANCELLED;
                    break;
                }
            }
        }
    }
    if (RT_SUCCESS(rc))
        *pcbDataRecv = cbAllDataRecv;

    return rc;
}

static int vbglR3DnDHGProcessSendDataMessage(uint32_t   uClientId,
                                             uint32_t  *puScreenId,
                                             char      *pszFormat,
                                             uint32_t   cbFormat,
                                             uint32_t  *pcbFormatRecv,
                                             void     **ppvData,
                                             uint32_t   cbData,
                                             size_t    *pcbDataRecv)
{
    int rc = vbglR3DnDHGProcessSendDataMessageLoop(uClientId,
                                                   puScreenId,
                                                   pszFormat,
                                                   cbFormat,
                                                   pcbFormatRecv,
                                                   ppvData,
                                                   cbData,
                                                   pcbDataRecv);
    if (RT_SUCCESS(rc))
    {
        /* Check if this is a uri-event. If so, let VbglR3 do all the actual
         * data transfer + file /directory creation internally without letting
         * the caller know. */
        if (RTStrNICmp(pszFormat, "text/uri-list", *pcbFormatRecv) == 0)
            rc = vbglR3DnDHGProcessURIMessages(uClientId,
                                               puScreenId,
                                               pszFormat,
                                               cbFormat,
                                               pcbFormatRecv,
                                               ppvData,
                                               cbData,
                                               pcbDataRecv);
    }

    return rc;
}

static int vbglR3DnDGHProcessRequestPendingMessage(uint32_t  uClientId,
                                                   uint32_t *puScreenId)
{
    AssertPtrReturn(puScreenId, VERR_INVALID_POINTER);

    DragAndDropSvc::VBOXDNDGHREQPENDINGMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = DragAndDropSvc::HOST_DND_GH_REQ_PENDING;
    Msg.hdr.cParms      = 1;

    Msg.uScreenId.SetUInt32(0);

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.hdr.result;
        if (RT_SUCCESS(rc))
        {
            /* Fetch results */
            rc = Msg.uScreenId.GetUInt32(puScreenId); AssertRC(rc);
        }
    }

    return rc;
}

static int vbglR3DnDGHProcessDroppedMessage(uint32_t  uClientId,
                                            char     *pszFormat,
                                            uint32_t  cbFormat,
                                            uint32_t *pcbFormatRecv,
                                            uint32_t *puAction)
{
    AssertPtrReturn(pszFormat,     VERR_INVALID_POINTER);
    AssertReturn(cbFormat,         VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbFormatRecv, VERR_INVALID_POINTER);
    AssertPtrReturn(puAction,      VERR_INVALID_POINTER);

    DragAndDropSvc::VBOXDNDGHDROPPEDMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.u32ClientID = uClientId;
    Msg.hdr.u32Function = DragAndDropSvc::HOST_DND_GH_EVT_DROPPED;
    Msg.hdr.cParms      = 3;

    Msg.pvFormat.SetPtr(pszFormat, cbFormat);
    Msg.cFormat.SetUInt32(0);
    Msg.uAction.SetUInt32(0);

    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.hdr.result;
        if (RT_SUCCESS(rc))
        {
            /* Fetch results */
            rc = Msg.cFormat.GetUInt32(pcbFormatRecv); AssertRC(rc);
            rc = Msg.uAction.GetUInt32(puAction);      AssertRC(rc);
            /* A little bit paranoia */
            AssertReturn(cbFormat >= *pcbFormatRecv, VERR_TOO_MUCH_DATA);
        }
    }

    return rc;
}

static int vbglR3DnDPathSanitize(char *pszPath, size_t cbPath)
{
    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    /* Filter out characters not allowed on Windows platforms, put in by
       RTTimeSpecToString(). */
    /** @todo Use something like RTPathSanitize() when available. Later. */
    RTUNICP aCpSet[] =
        { ' ', ' ', '(', ')', '-', '.', '0', '9', 'A', 'Z', 'a', 'z', '_', '_',
          0xa0, 0xd7af, '\0' };
    ssize_t cReplaced = RTStrPurgeComplementSet(pszPath, aCpSet, '_' /* Replacement */);
    if (cReplaced < 0)
        rc = VERR_INVALID_UTF8_ENCODING;
#endif
    return rc;
}

/******************************************************************************
 *    Public functions                                                        *
 ******************************************************************************/

VBGLR3DECL(int) VbglR3DnDConnect(uint32_t *pu32ClientId)
{
    AssertPtrReturn(pu32ClientId, VERR_INVALID_POINTER);

    /* Initialize header */
    VBoxGuestHGCMConnectInfo Info;
    RT_ZERO(Info.Loc.u);
    Info.result      = VERR_WRONG_ORDER;
    Info.u32ClientID = UINT32_MAX;  /* try make valgrind shut up. */
    /* Initialize parameter */
    Info.Loc.type    = VMMDevHGCMLoc_LocalHost_Existing;
    int rc = RTStrCopy(Info.Loc.u.host.achName, sizeof(Info.Loc.u.host.achName), "VBoxDragAndDropSvc");
    if (RT_FAILURE(rc)) return rc;
    /* Do request */
    rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CONNECT, &Info, sizeof(Info));
    if (RT_SUCCESS(rc))
    {
        rc = Info.result;
        if (RT_SUCCESS(rc))
            *pu32ClientId = Info.u32ClientID;
    }
    return rc;
}

VBGLR3DECL(int) VbglR3DnDDisconnect(uint32_t u32ClientId)
{
    VBoxGuestHGCMDisconnectInfo Info;
    Info.result      = VERR_WRONG_ORDER;
    Info.u32ClientID = u32ClientId;

    /* Do request */
    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_DISCONNECT, &Info, sizeof(Info));
    if (RT_SUCCESS(rc))
        rc = Info.result;

    return rc;
}

VBGLR3DECL(int) VbglR3DnDProcessNextMessage(uint32_t u32ClientId, CPVBGLR3DNDHGCMEVENT pEvent)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    uint32_t       uMsg       = 0;
    uint32_t       uNumParms  = 0;
    const uint32_t ccbFormats = _64K;
    const uint32_t ccbData    = _64K;
    int rc = vbglR3DnDQueryNextHostMessageType(u32ClientId, &uMsg, &uNumParms,
                                               true /* fWait */);
    if (RT_SUCCESS(rc))
    {
        switch(uMsg)
        {
            case DragAndDropSvc::HOST_DND_HG_EVT_ENTER:
            case DragAndDropSvc::HOST_DND_HG_EVT_MOVE:
            case DragAndDropSvc::HOST_DND_HG_EVT_DROPPED:
            {
                pEvent->uType = uMsg;
                pEvent->pszFormats = static_cast<char*>(RTMemAlloc(ccbFormats));
                if (!pEvent->pszFormats)
                    rc = VERR_NO_MEMORY;

                if (RT_SUCCESS(rc))
                    rc = vbglR3DnDHGProcessActionMessage(u32ClientId,
                                                         uMsg,
                                                         &pEvent->uScreenId,
                                                         &pEvent->u.a.uXpos,
                                                         &pEvent->u.a.uYpos,
                                                         &pEvent->u.a.uDefAction,
                                                         &pEvent->u.a.uAllActions,
                                                         pEvent->pszFormats,
                                                         ccbFormats,
                                                         &pEvent->cbFormats);
                break;
            }
            case DragAndDropSvc::HOST_DND_HG_EVT_LEAVE:
            {
                pEvent->uType = uMsg;
                rc = vbglR3DnDHGProcessLeaveMessage(u32ClientId);
                break;
            }
            case DragAndDropSvc::HOST_DND_HG_SND_DATA:
            {
                pEvent->uType = uMsg;
                pEvent->pszFormats = static_cast<char*>(RTMemAlloc(ccbFormats));
                if (!pEvent->pszFormats)
                    rc = VERR_NO_MEMORY;

                if (RT_SUCCESS(rc))
                {
                    pEvent->u.b.pvData = RTMemAlloc(ccbData);
                    if (!pEvent->u.b.pvData)
                    {
                        RTMemFree(pEvent->pszFormats);
                        pEvent->pszFormats = NULL;
                        rc = VERR_NO_MEMORY;
                    }
                }

                if (RT_SUCCESS(rc))
                    rc = vbglR3DnDHGProcessSendDataMessage(u32ClientId,
                                                           &pEvent->uScreenId,
                                                           pEvent->pszFormats,
                                                           ccbFormats,
                                                           &pEvent->cbFormats,
                                                           &pEvent->u.b.pvData,
                                                           ccbData,
                                                           &pEvent->u.b.cbData);
                break;
            }
            case DragAndDropSvc::HOST_DND_HG_EVT_CANCEL:
            {
                pEvent->uType = uMsg;
                rc = vbglR3DnDHGProcessCancelMessage(u32ClientId);
                break;
            }
            case DragAndDropSvc::HOST_DND_GH_REQ_PENDING:
            {
                pEvent->uType = uMsg;
                rc = vbglR3DnDGHProcessRequestPendingMessage(u32ClientId,
                                                             &pEvent->uScreenId);
                break;
            }
            case DragAndDropSvc::HOST_DND_GH_EVT_DROPPED:
            {
                pEvent->uType = uMsg;
                pEvent->pszFormats = static_cast<char*>(RTMemAlloc(ccbFormats));
                if (!pEvent->pszFormats)
                    rc = VERR_NO_MEMORY;

                if (RT_SUCCESS(rc))
                    rc = vbglR3DnDGHProcessDroppedMessage(u32ClientId,
                                                          pEvent->pszFormats,
                                                          ccbFormats,
                                                          &pEvent->cbFormats,
                                                          &pEvent->u.a.uDefAction);
                break;
            }
            default:
                rc = VERR_NOT_SUPPORTED;
                break;
        }
    }

    return rc;
}

VBGLR3DECL(int) VbglR3DnDHGAcknowledgeOperation(uint32_t u32ClientId, uint32_t uAction)
{
    DragAndDropSvc::VBOXDNDHGACKOPMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.result      = VERR_WRONG_ORDER;
    Msg.hdr.u32ClientID = u32ClientId;
    Msg.hdr.u32Function = DragAndDropSvc::GUEST_DND_HG_ACK_OP;
    Msg.hdr.cParms      = 1;
    /* Initialize parameter */
    Msg.uAction.SetUInt32(uAction);
    /* Do request */
    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
        rc = Msg.hdr.result;

    return rc;
}

VBGLR3DECL(int) VbglR3DnDHGRequestData(uint32_t u32ClientId, const char* pcszFormat)
{
    AssertPtrReturn(pcszFormat, VERR_INVALID_PARAMETER);

    DragAndDropSvc::VBOXDNDHGREQDATAMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.result      = VERR_WRONG_ORDER;
    Msg.hdr.u32ClientID = u32ClientId;
    Msg.hdr.u32Function = DragAndDropSvc::GUEST_DND_HG_REQ_DATA;
    Msg.hdr.cParms      = 1;
    /* Initialize parameter */
    Msg.pFormat.SetPtr((void*)pcszFormat, (uint32_t)strlen(pcszFormat) + 1);
    /* Do request */
    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
        rc = Msg.hdr.result;

    return rc;
}

VBGLR3DECL(int) VbglR3DnDGHAcknowledgePending(uint32_t u32ClientId,
                                              uint32_t uDefAction, uint32_t uAllActions,
                                              const char* pcszFormat)
{
    AssertPtrReturn(pcszFormat, VERR_INVALID_POINTER);

    DragAndDropSvc::VBOXDNDGHACKPENDINGMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.result      = VERR_WRONG_ORDER;
    Msg.hdr.u32ClientID = u32ClientId;
    Msg.hdr.u32Function = DragAndDropSvc::GUEST_DND_GH_ACK_PENDING;
    Msg.hdr.cParms      = 3;
    /* Initialize parameter */
    Msg.uDefAction.SetUInt32(uDefAction);
    Msg.uAllActions.SetUInt32(uAllActions);
    Msg.pFormat.SetPtr((void*)pcszFormat, (uint32_t)strlen(pcszFormat) + 1);
    /* Do request */
    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
        rc = Msg.hdr.result;

    return rc;
}

VBGLR3DECL(int) VbglR3DnDGHSendData(uint32_t u32ClientId, void *pvData, uint32_t cbData)
{
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData,    VERR_INVALID_PARAMETER);

    /** @todo Add URI support. Currently only data is send over to the host. For URI
     *        support basically the same as in the H->G case (see
     *        HostServices/DragAndDrop/dndmanager.h/cpp) has to be done:
     *        1. Parse the urilist
     *        2. Recursively send "create dir" and "transfer file" msg to the host
     *        3. Patch the urilist by removing all base dirnames
     *        4. On the host all needs to received and the urilist patched afterwards
     *           to point to the new location
     */

    DragAndDropSvc::VBOXDNDGHSENDDATAMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.result      = VERR_WRONG_ORDER;
    Msg.hdr.u32ClientID = u32ClientId;
    Msg.hdr.u32Function = DragAndDropSvc::GUEST_DND_GH_SND_DATA;
    Msg.hdr.cParms      = 2;
    Msg.uSize.SetUInt32(cbData);

    int rc          = VINF_SUCCESS;
    uint32_t cbMax  = _1M; /** @todo Remove 1 MB limit. */
    uint32_t cbSent = 0;

    while (cbSent < cbData)
    {
        /* Initialize parameter */
        uint32_t cbToSend = RT_MIN(cbData - cbSent, cbMax);
        Msg.pData.SetPtr(static_cast<uint8_t*>(pvData) + cbSent, cbToSend);
        /* Do request */
        rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            rc = Msg.hdr.result;
            /* Did the host cancel the event? */
            if (rc == VERR_CANCELLED)
                break;
        }
        else
            break;
        cbSent += cbToSend;
//        RTThreadSleep(500);
    }

    return rc;
}

VBGLR3DECL(int) VbglR3DnDGHErrorEvent(uint32_t u32ClientId, int rcOp)
{
    DragAndDropSvc::VBOXDNDGHEVTERRORMSG Msg;
    RT_ZERO(Msg);
    Msg.hdr.result      = VERR_WRONG_ORDER;
    Msg.hdr.u32ClientID = u32ClientId;
    Msg.hdr.u32Function = DragAndDropSvc::GUEST_DND_GH_EVT_ERROR;
    Msg.hdr.cParms      = 1;
    /* Initialize parameter */
    Msg.uRC.SetUInt32(rcOp);
    /* Do request */
    int rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_HGCM_CALL(sizeof(Msg)), &Msg, sizeof(Msg));
    if (RT_SUCCESS(rc))
        rc = Msg.hdr.result;

    return rc;
}

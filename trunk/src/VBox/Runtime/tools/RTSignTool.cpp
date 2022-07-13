/* $Id$ */
/** @file
 * IPRT - Signing Tool.
 */

/*
 * Copyright (C) 2006-2022 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/message.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/zero.h>
#ifndef RT_OS_WINDOWS
# include <iprt/formats/pecoff.h>
#endif
#include <iprt/crypto/applecodesign.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/key.h>
#include <iprt/crypto/x509.h>
#include <iprt/crypto/pkcs7.h>
#include <iprt/crypto/store.h>
#include <iprt/crypto/spc.h>
#ifdef VBOX
# include <VBox/sup.h> /* Certificates */
#endif
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
# include <iprt/win/imagehlp.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define OPT_HASH_PAGES                  1000
#define OPT_NO_HASH_PAGES               1001
#define OPT_CERT_FILE                   1002
#define OPT_KEY_FILE                    1003
#define OPT_ADD_CERT                    1004

#define OPT_TIMESTAMP_CERT_FILE         1010
#define OPT_TIMESTAMP_KEY_FILE          1011
#define OPT_TIMESTAMP_TYPE              1012
#define OPT_TIMESTAMP_OVERRIDE          1016


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Help detail levels. */
typedef enum RTSIGNTOOLHELP
{
    RTSIGNTOOLHELP_USAGE,
    RTSIGNTOOLHELP_FULL
} RTSIGNTOOLHELP;


/**
 * PKCS\#7 signature data.
 */
typedef struct SIGNTOOLPKCS7
{
    /** The raw signature. */
    uint8_t                    *pbBuf;
    /** Size of the raw signature. */
    size_t                      cbBuf;
    /** The filename.   */
    const char                 *pszFilename;
    /** The outer content info wrapper. */
    RTCRPKCS7CONTENTINFO        ContentInfo;
    /** Pointer to the decoded SignedData inside the ContentInfo member. */
    PRTCRPKCS7SIGNEDDATA        pSignedData;

    /** Newly encoded raw signature.
     * @sa SignToolPkcs7_Encode()  */
    uint8_t                    *pbNewBuf;
    /** Size of newly encoded raw signature. */
    size_t                      cbNewBuf;

} SIGNTOOLPKCS7;
typedef SIGNTOOLPKCS7 *PSIGNTOOLPKCS7;


/**
 * PKCS\#7 signature data for executable.
 */
typedef struct SIGNTOOLPKCS7EXE : public SIGNTOOLPKCS7
{
    /** The module handle. */
    RTLDRMOD                    hLdrMod;
} SIGNTOOLPKCS7EXE;
typedef SIGNTOOLPKCS7EXE *PSIGNTOOLPKCS7EXE;


/**
 * Data for the show exe (signature) command.
 */
typedef struct SHOWEXEPKCS7 : public SIGNTOOLPKCS7EXE
{
    /** The verbosity. */
    unsigned                    cVerbosity;
    /** The prefix buffer. */
    char                        szPrefix[256];
    /** Temporary buffer. */
    char                        szTmp[4096];
} SHOWEXEPKCS7;
typedef SHOWEXEPKCS7 *PSHOWEXEPKCS7;


/**
 * Certificate w/ public key + private key pair for signing.
 */
typedef struct SIGNTOOLKEYPAIR
{
    RTCRX509CERTIFICATE     Cert;
    PCRTCRX509CERTIFICATE   pCertificate;
    RTCRKEY                 hPrivateKey;

    SIGNTOOLKEYPAIR()
        : pCertificate(NULL)
        , hPrivateKey(NIL_RTCRKEY)
    {
        RT_ZERO(Cert);
    }

    ~SIGNTOOLKEYPAIR()
    {
        if (hPrivateKey != NIL_RTCRKEY)
        {
            RTCrKeyRelease(hPrivateKey);
            hPrivateKey = NIL_RTCRKEY;
        }
        if (pCertificate == &Cert)
        {
            RTCrX509Certificate_Delete(&Cert);
            pCertificate = NULL;
        }
    }

    bool isComplete(void) const
    {
        return pCertificate && hPrivateKey != NIL_RTCRKEY;
    }

    bool isNull(void) const
    {
        return pCertificate == NULL && hPrivateKey == NIL_RTCRKEY;
    }
} SIGNTOOLKEYPAIR;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static RTEXITCODE HandleHelp(int cArgs, char **papszArgs);
static RTEXITCODE HelpHelp(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel);
static RTEXITCODE HandleVersion(int cArgs, char **papszArgs);
static int HandleShowExeWorkerPkcs7DisplaySignerInfo(PSHOWEXEPKCS7 pThis, size_t offPrefix, PCRTCRPKCS7SIGNERINFO pSignerInfo);
static int HandleShowExeWorkerPkcs7Display(PSHOWEXEPKCS7 pThis, PRTCRPKCS7SIGNEDDATA pSignedData, size_t offPrefix,
                                           PCRTCRPKCS7CONTENTINFO pContentInfo);


/**
 * Deletes the structure.
 *
 * @param   pThis               The structure to initialize.
 */
static void SignToolPkcs7_Delete(PSIGNTOOLPKCS7 pThis)
{
    RTCrPkcs7ContentInfo_Delete(&pThis->ContentInfo);
    pThis->pSignedData = NULL;
    RTMemFree(pThis->pbBuf);
    pThis->pbBuf       = NULL;
    pThis->cbBuf       = 0;
    RTMemFree(pThis->pbNewBuf);
    pThis->pbNewBuf    = NULL;
    pThis->cbNewBuf    = 0;
}


/**
 * Deletes the structure.
 *
 * @param   pThis               The structure to initialize.
 */
static void SignToolPkcs7Exe_Delete(PSIGNTOOLPKCS7EXE pThis)
{
    if (pThis->hLdrMod != NIL_RTLDRMOD)
    {
        int rc2 = RTLdrClose(pThis->hLdrMod);
        if (RT_FAILURE(rc2))
            RTMsgError("RTLdrClose failed: %Rrc\n", rc2);
        pThis->hLdrMod = NIL_RTLDRMOD;
    }
    SignToolPkcs7_Delete(pThis);
}


/**
 * Decodes the PKCS #7 blob pointed to by pThis->pbBuf.
 *
 * @returns IPRT status code (error message already shown on failure).
 * @param   pThis               The PKCS\#7 signature to decode.
 * @param   fCatalog            Set if catalog file, clear if executable.
 */
static int SignToolPkcs7_Decode(PSIGNTOOLPKCS7 pThis, bool fCatalog)
{
    RTERRINFOSTATIC     ErrInfo;
    RTASN1CURSORPRIMARY PrimaryCursor;
    RTAsn1CursorInitPrimary(&PrimaryCursor, pThis->pbBuf, (uint32_t)pThis->cbBuf, RTErrInfoInitStatic(&ErrInfo),
                            &g_RTAsn1DefaultAllocator, 0, "WinCert");

    int rc = RTCrPkcs7ContentInfo_DecodeAsn1(&PrimaryCursor.Cursor, 0, &pThis->ContentInfo, "CI");
    if (RT_SUCCESS(rc))
    {
        if (RTCrPkcs7ContentInfo_IsSignedData(&pThis->ContentInfo))
        {
            pThis->pSignedData = pThis->ContentInfo.u.pSignedData;

            /*
             * Decode the authenticode bits.
             */
            if (!strcmp(pThis->pSignedData->ContentInfo.ContentType.szObjId, RTCRSPCINDIRECTDATACONTENT_OID))
            {
                PRTCRSPCINDIRECTDATACONTENT pIndData = pThis->pSignedData->ContentInfo.u.pIndirectDataContent;
                Assert(pIndData);

                /*
                 * Check that things add up.
                 */
                rc = RTCrPkcs7SignedData_CheckSanity(pThis->pSignedData,
                                                     RTCRPKCS7SIGNEDDATA_SANITY_F_AUTHENTICODE
                                                     | RTCRPKCS7SIGNEDDATA_SANITY_F_ONLY_KNOWN_HASH
                                                     | RTCRPKCS7SIGNEDDATA_SANITY_F_SIGNING_CERT_PRESENT,
                                                     RTErrInfoInitStatic(&ErrInfo), "SD");
                if (RT_SUCCESS(rc))
                {
                    rc = RTCrSpcIndirectDataContent_CheckSanityEx(pIndData,
                                                                  pThis->pSignedData,
                                                                  RTCRSPCINDIRECTDATACONTENT_SANITY_F_ONLY_KNOWN_HASH,
                                                                  RTErrInfoInitStatic(&ErrInfo));
                    if (RT_FAILURE(rc))
                        RTMsgError("SPC indirect data content sanity check failed for '%s': %Rrc - %s\n",
                                   pThis->pszFilename, rc, ErrInfo.szMsg);
                }
                else
                    RTMsgError("PKCS#7 sanity check failed for '%s': %Rrc - %s\n", pThis->pszFilename, rc, ErrInfo.szMsg);
            }
            else if (!strcmp(pThis->pSignedData->ContentInfo.ContentType.szObjId, RTCR_PKCS7_DATA_OID))
            { /* apple code signing */ }
            else if (!fCatalog)
                RTMsgError("Unexpected the signed content in '%s': %s (expected %s)", pThis->pszFilename,
                           pThis->pSignedData->ContentInfo.ContentType.szObjId, RTCRSPCINDIRECTDATACONTENT_OID);
        }
        else
            rc = RTMsgErrorRc(VERR_CR_PKCS7_NOT_SIGNED_DATA,
                              "PKCS#7 content is inside '%s' is not 'signedData': %s\n",
                              pThis->pszFilename, pThis->ContentInfo.ContentType.szObjId);
    }
    else
        RTMsgError("RTCrPkcs7ContentInfo_DecodeAsn1 failed on '%s': %Rrc - %s\n", pThis->pszFilename, rc, ErrInfo.szMsg);
    return rc;
}


/**
 * Reads and decodes PKCS\#7 signature from the given cat file.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error message
 *          on failure.
 * @param   pThis               The structure to initialize.
 * @param   pszFilename         The catalog (or any other DER PKCS\#7) filename.
 * @param   cVerbosity          The verbosity.
 */
static RTEXITCODE SignToolPkcs7_InitFromFile(PSIGNTOOLPKCS7 pThis, const char *pszFilename, unsigned cVerbosity)
{
    /*
     * Init the return structure.
     */
    RT_ZERO(*pThis);
    pThis->pszFilename = pszFilename;

    /*
     * Lazy bird uses RTFileReadAll and duplicates the allocation.
     */
    void *pvFile;
    int rc = RTFileReadAll(pszFilename, &pvFile, &pThis->cbBuf);
    if (RT_SUCCESS(rc))
    {
        pThis->pbBuf = (uint8_t *)RTMemDup(pvFile, pThis->cbBuf);
        RTFileReadAllFree(pvFile, pThis->cbBuf);
        if (pThis->pbBuf)
        {
            if (cVerbosity > 2)
                RTPrintf("PKCS#7 signature: %u bytes\n", pThis->cbBuf);

            /*
             * Decode it.
             */
            rc = SignToolPkcs7_Decode(pThis, true /*fCatalog*/);
            if (RT_SUCCESS(rc))
                return RTEXITCODE_SUCCESS;
        }
        else
            RTMsgError("Out of memory!");
    }
    else
        RTMsgError("Error reading '%s' into memory: %Rrc", pszFilename, rc);

    SignToolPkcs7_Delete(pThis);
    return RTEXITCODE_FAILURE;
}


/**
 * Encodes the signature into the SIGNTOOLPKCS7::pbNewBuf and
 * SIGNTOOLPKCS7::cbNewBuf members.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error message
 *          on failure.
 * @param   pThis               The signature to encode.
 * @param   cVerbosity          The verbosity.
 */
static RTEXITCODE SignToolPkcs7_Encode(PSIGNTOOLPKCS7 pThis, unsigned cVerbosity)
{
    RTERRINFOSTATIC StaticErrInfo;
    PRTASN1CORE pRoot = RTCrPkcs7ContentInfo_GetAsn1Core(&pThis->ContentInfo);
    uint32_t cbEncoded;
    int rc = RTAsn1EncodePrepare(pRoot, RTASN1ENCODE_F_DER, &cbEncoded, RTErrInfoInitStatic(&StaticErrInfo));
    if (RT_SUCCESS(rc))
    {
        if (cVerbosity >= 4)
            RTAsn1Dump(pRoot, 0, 0, RTStrmDumpPrintfV, g_pStdOut);

        RTMemFree(pThis->pbNewBuf);
        pThis->cbNewBuf = cbEncoded;
        pThis->pbNewBuf = (uint8_t *)RTMemAllocZ(cbEncoded);
        if (pThis->pbNewBuf)
        {
            rc = RTAsn1EncodeToBuffer(pRoot, RTASN1ENCODE_F_DER, pThis->pbNewBuf, pThis->cbNewBuf,
                                      RTErrInfoInitStatic(&StaticErrInfo));
            if (RT_SUCCESS(rc))
            {
                if (cVerbosity > 1)
                    RTMsgInfo("Encoded signature to %u bytes", cbEncoded);
                return RTEXITCODE_SUCCESS;
            }
            RTMsgError("RTAsn1EncodeToBuffer failed: %Rrc", rc);

            RTMemFree(pThis->pbNewBuf);
            pThis->pbNewBuf = NULL;
        }
        else
            RTMsgError("Failed to allocate %u bytes!", cbEncoded);
    }
    else
        RTMsgError("RTAsn1EncodePrepare failed: %Rrc - %s", rc, StaticErrInfo.szMsg);
    return RTEXITCODE_FAILURE;
}


/**
 * Helper that makes sure the UnauthenticatedAttributes are present in the given
 * SignerInfo structure.
 *
 * Call this before trying to modify the array.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error already
 *          displayed on failure.
 * @param   pSignerInfo         The SignerInfo structure in question.
 */
static RTEXITCODE SignToolPkcs7_EnsureUnauthenticatedAttributesPresent(PRTCRPKCS7SIGNERINFO pSignerInfo)
{
   if (pSignerInfo->UnauthenticatedAttributes.cItems == 0)
   {
       /* HACK ALERT! Invent ASN.1 setters/whatever for members to replace this mess. */

       if (pSignerInfo->AuthenticatedAttributes.cItems == 0)
           return RTMsgErrorExit(RTEXITCODE_FAILURE, "No authenticated or unauthenticated attributes! Sorry, no can do.");

       Assert(pSignerInfo->UnauthenticatedAttributes.SetCore.Asn1Core.uTag == 0);
       int rc = RTAsn1SetCore_Init(&pSignerInfo->UnauthenticatedAttributes.SetCore,
                                   pSignerInfo->AuthenticatedAttributes.SetCore.Asn1Core.pOps);
       if (RT_FAILURE(rc))
           return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTAsn1SetCore_Init failed: %Rrc", rc);
       pSignerInfo->UnauthenticatedAttributes.SetCore.Asn1Core.uTag   = 1;
       pSignerInfo->UnauthenticatedAttributes.SetCore.Asn1Core.fClass = ASN1_TAGCLASS_CONTEXT | ASN1_TAGFLAG_CONSTRUCTED;
       RTAsn1MemInitArrayAllocation(&pSignerInfo->UnauthenticatedAttributes.Allocation,
                                    pSignerInfo->AuthenticatedAttributes.Allocation.pAllocator,
                                    sizeof(**pSignerInfo->UnauthenticatedAttributes.papItems));
   }
   return RTEXITCODE_SUCCESS;
}


/**
 * Adds the @a pSrc signature as a nested signature.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error message
 *          on failure.
 * @param   pThis               The signature to modify.
 * @param   pSrc                The signature to add as nested.
 * @param   cVerbosity          The verbosity.
 * @param   fPrepend            Whether to prepend (true) or append (false) the
 *                              source signature to the nested attribute.
 */
static RTEXITCODE SignToolPkcs7_AddNestedSignature(PSIGNTOOLPKCS7 pThis, PSIGNTOOLPKCS7 pSrc,
                                                   unsigned cVerbosity, bool fPrepend)
{
    PRTCRPKCS7SIGNERINFO pSignerInfo = pThis->pSignedData->SignerInfos.papItems[0];

    /*
     * Deal with UnauthenticatedAttributes being absent before trying to append to the array.
     */
    RTEXITCODE rcExit = SignToolPkcs7_EnsureUnauthenticatedAttributesPresent(pSignerInfo);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Find or add an unauthenticated attribute for nested signatures.
     */
    int rc = VERR_NOT_FOUND;
    PRTCRPKCS7ATTRIBUTE pAttr = NULL;
    int32_t iPos = pSignerInfo->UnauthenticatedAttributes.cItems;
    while (iPos-- > 0)
        if (pSignerInfo->UnauthenticatedAttributes.papItems[iPos]->enmType == RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE)
        {
            pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iPos];
            rc = VINF_SUCCESS;
            break;
        }
    if (iPos < 0)
    {
        iPos = RTCrPkcs7Attributes_Append(&pSignerInfo->UnauthenticatedAttributes);
        if (iPos >= 0)
        {
            if (cVerbosity >= 3)
                RTMsgInfo("Adding UnauthenticatedAttribute #%u...", iPos);
            Assert((uint32_t)iPos < pSignerInfo->UnauthenticatedAttributes.cItems);

            pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iPos];
            rc = RTAsn1ObjId_InitFromString(&pAttr->Type, RTCR_PKCS9_ID_MS_NESTED_SIGNATURE, pAttr->Allocation.pAllocator);
            if (RT_SUCCESS(rc))
            {
                /** @todo Generalize the Type + enmType DYN stuff and generate setters. */
                Assert(pAttr->enmType == RTCRPKCS7ATTRIBUTETYPE_NOT_PRESENT);
                Assert(pAttr->uValues.pContentInfos == NULL);
                pAttr->enmType = RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE;
                rc = RTAsn1MemAllocZ(&pAttr->Allocation, (void **)&pAttr->uValues.pContentInfos,
                                     sizeof(*pAttr->uValues.pContentInfos));
                if (RT_SUCCESS(rc))
                {
                    rc = RTCrPkcs7SetOfContentInfos_Init(pAttr->uValues.pContentInfos, pAttr->Allocation.pAllocator);
                    if (!RT_SUCCESS(rc))
                        RTMsgError("RTCrPkcs7ContentInfos_Init failed: %Rrc", rc);
                }
                else
                    RTMsgError("RTAsn1MemAllocZ failed: %Rrc", rc);
            }
            else
                RTMsgError("RTAsn1ObjId_InitFromString failed: %Rrc", rc);
        }
        else
            RTMsgError("RTCrPkcs7Attributes_Append failed: %Rrc", iPos);
    }
    else if (cVerbosity >= 2)
        RTMsgInfo("Found UnauthenticatedAttribute #%u...", iPos);
    if (RT_SUCCESS(rc))
    {
        /*
         * Append/prepend the signature.
         */
        uint32_t iActualPos = UINT32_MAX;
        iPos = fPrepend ? 0 : pAttr->uValues.pContentInfos->cItems;
        rc = RTCrPkcs7SetOfContentInfos_InsertEx(pAttr->uValues.pContentInfos, iPos, &pSrc->ContentInfo,
                                                 pAttr->Allocation.pAllocator, &iActualPos);
        if (RT_SUCCESS(rc))
        {
            if (cVerbosity > 0)
                RTMsgInfo("Added nested signature (#%u)", iActualPos);
            if (cVerbosity >= 3)
            {
                RTMsgInfo("SingerInfo dump after change:");
                RTAsn1Dump(RTCrPkcs7SignerInfo_GetAsn1Core(pSignerInfo), 0, 2, RTStrmDumpPrintfV, g_pStdOut);
            }
            return RTEXITCODE_SUCCESS;
        }

        RTMsgError("RTCrPkcs7ContentInfos_InsertEx failed: %Rrc", rc);
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Writes the signature to the file.
 *
 * Caller must have called SignToolPkcs7_Encode() prior to this function.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error
 *          message on failure.
 * @param   pThis               The file which to write.
 * @param   cVerbosity          The verbosity.
 */
static RTEXITCODE SignToolPkcs7_WriteSignatureToFile(PSIGNTOOLPKCS7 pThis, const char *pszFilename, unsigned cVerbosity)
{
    AssertReturn(pThis->cbNewBuf && pThis->pbNewBuf, RTEXITCODE_FAILURE);

    /*
     * Open+truncate file, write new signature, close.  Simple.
     */
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_WRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_TRUNCATE | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        rc = RTFileWrite(hFile, pThis->pbNewBuf, pThis->cbNewBuf, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileClose(hFile);
            if (RT_SUCCESS(rc))
            {
                if (cVerbosity > 0)
                    RTMsgInfo("Wrote %u bytes to %s", pThis->cbNewBuf, pszFilename);
                return RTEXITCODE_SUCCESS;
            }

            RTMsgError("RTFileClose failed on %s: %Rrc", pszFilename, rc);
        }
        else
            RTMsgError("Write error on %s: %Rrc", pszFilename, rc);
    }
    else
        RTMsgError("Failed to open %s for writing: %Rrc", pszFilename, rc);
    return RTEXITCODE_FAILURE;
}



/**
 * Worker for recursively searching for MS nested signatures and signer infos.
 *
 * @returns Pointer to the signer info corresponding to @a iReqSignature.  NULL
 *          if not found.
 * @param   pSignedData     The signature to search.
 * @param   piNextSignature Pointer to the variable keeping track of the next
 *                          signature number.
 * @param   iReqSignature   The request signature number.
 * @param   ppSignedData    Where to return the signature data structure.
 *                          Optional.
 */
static PRTCRPKCS7SIGNERINFO SignToolPkcs7_FindNestedSignatureByIndexWorker(PRTCRPKCS7SIGNEDDATA pSignedData,
                                                                           uint32_t *piNextSignature,
                                                                           uint32_t iReqSignature,
                                                                           PRTCRPKCS7SIGNEDDATA *ppSignedData)
{
    for (uint32_t iSignerInfo = 0; iSignerInfo < pSignedData->SignerInfos.cItems; iSignerInfo++)
    {
        /* Match?*/
        PRTCRPKCS7SIGNERINFO pSignerInfo = pSignedData->SignerInfos.papItems[iSignerInfo];
        if (*piNextSignature == iReqSignature)
        {
            if (ppSignedData)
                *ppSignedData = pSignedData;
            return pSignerInfo;
        }
        *piNextSignature += 1;

        /* Look for nested signatures. */
        for (uint32_t iAttrib = 0; iAttrib < pSignerInfo->UnauthenticatedAttributes.cItems; iAttrib++)
            if (pSignerInfo->UnauthenticatedAttributes.papItems[iAttrib]->enmType == RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE)
            {
                PRTCRPKCS7SETOFCONTENTINFOS pCntInfos;
                pCntInfos = pSignerInfo->UnauthenticatedAttributes.papItems[iAttrib]->uValues.pContentInfos;
                for (uint32_t iCntInfo = 0; iCntInfo < pCntInfos->cItems; iCntInfo++)
                {
                    PRTCRPKCS7CONTENTINFO pCntInfo = pCntInfos->papItems[iCntInfo];
                    if (RTCrPkcs7ContentInfo_IsSignedData(pCntInfo))
                    {
                        PRTCRPKCS7SIGNERINFO pRet;
                        pRet = SignToolPkcs7_FindNestedSignatureByIndexWorker(pCntInfo->u.pSignedData, piNextSignature,
                                                                              iReqSignature, ppSignedData);
                        if (pRet)
                            return pRet;
                    }
                }
            }
    }
    return NULL;
}


/**
 * Locates the given nested signature.
 *
 * @returns Pointer to the signer info corresponding to @a iReqSignature.  NULL
 *          if not found.
 * @param   pThis           The PKCS\#7 structure to search.
 * @param   iReqSignature   The requested signature number.
 * @param   ppSignedData    Where to return the pointer to the signed data that
 *                          the returned signer info belongs to.
 *
 * @todo    Move into SPC or PKCS\#7.
 */
static PRTCRPKCS7SIGNERINFO SignToolPkcs7_FindNestedSignatureByIndex(PSIGNTOOLPKCS7 pThis, uint32_t iReqSignature,
                                                                     PRTCRPKCS7SIGNEDDATA *ppSignedData)
{
    uint32_t iNextSignature = 0;
    return SignToolPkcs7_FindNestedSignatureByIndexWorker(pThis->pSignedData, &iNextSignature, iReqSignature, ppSignedData);
}



/**
 * Reads and decodes PKCS\#7 signature from the given executable.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error message
 *          on failure.
 * @param   pThis               The structure to initialize.
 * @param   pszFilename         The executable filename.
 * @param   cVerbosity          The verbosity.
 * @param   enmLdrArch          For FAT binaries.
 */
static RTEXITCODE SignToolPkcs7Exe_InitFromFile(PSIGNTOOLPKCS7EXE pThis, const char *pszFilename,
                                                unsigned cVerbosity, RTLDRARCH enmLdrArch = RTLDRARCH_WHATEVER)
{
    /*
     * Init the return structure.
     */
    RT_ZERO(*pThis);
    pThis->hLdrMod = NIL_RTLDRMOD;
    pThis->pszFilename = pszFilename;

    /*
     * Open the image and check if it's signed.
     */
    int rc = RTLdrOpen(pszFilename, RTLDR_O_FOR_VALIDATION, enmLdrArch, &pThis->hLdrMod);
    if (RT_SUCCESS(rc))
    {
        bool fIsSigned = false;
        rc = RTLdrQueryProp(pThis->hLdrMod, RTLDRPROP_IS_SIGNED, &fIsSigned, sizeof(fIsSigned));
        if (RT_SUCCESS(rc) && fIsSigned)
        {
            /*
             * Query the PKCS#7 data (assuming M$ style signing) and hand it to a worker.
             */
            size_t cbActual = 0;
#ifdef DEBUG
            size_t cbBuf    = 64;
#else
            size_t cbBuf    = _512K;
#endif
            void  *pvBuf    = RTMemAllocZ(cbBuf);
            if (pvBuf)
            {
                rc = RTLdrQueryPropEx(pThis->hLdrMod, RTLDRPROP_PKCS7_SIGNED_DATA, NULL /*pvBits*/, pvBuf, cbBuf, &cbActual);
                if (rc == VERR_BUFFER_OVERFLOW)
                {
                    RTMemFree(pvBuf);
                    cbBuf = cbActual;
                    pvBuf = RTMemAllocZ(cbActual);
                    if (pvBuf)
                        rc = RTLdrQueryPropEx(pThis->hLdrMod, RTLDRPROP_PKCS7_SIGNED_DATA, NULL /*pvBits*/,
                                              pvBuf, cbBuf, &cbActual);
                    else
                        rc = VERR_NO_MEMORY;
                }
            }
            else
                rc = VERR_NO_MEMORY;

            pThis->pbBuf = (uint8_t *)pvBuf;
            pThis->cbBuf = cbActual;
            if (RT_SUCCESS(rc))
            {
                if (cVerbosity > 2)
                    RTPrintf("PKCS#7 signature: %u bytes\n", cbActual);
                if (cVerbosity > 3)
                    RTPrintf("%.*Rhxd\n", cbActual, pvBuf);

                /*
                 * Decode it.
                 */
                rc = SignToolPkcs7_Decode(pThis, false /*fCatalog*/);
                if (RT_SUCCESS(rc))
                    return RTEXITCODE_SUCCESS;
            }
            else
                RTMsgError("RTLdrQueryPropEx/RTLDRPROP_PKCS7_SIGNED_DATA failed on '%s': %Rrc\n", pszFilename, rc);
        }
        else if (RT_SUCCESS(rc))
            RTMsgInfo("'%s': not signed\n", pszFilename);
        else
            RTMsgError("RTLdrQueryProp/RTLDRPROP_IS_SIGNED failed on '%s': %Rrc\n", pszFilename, rc);
    }
    else
        RTMsgError("Error opening executable image '%s': %Rrc", pszFilename, rc);

    SignToolPkcs7Exe_Delete(pThis);
    return RTEXITCODE_FAILURE;
}


/**
 * Calculates the checksum of an executable.
 *
 * @returns Success indicator (errors are reported)
 * @param   pThis               The exe file to checksum.
 * @param   hFile               The file handle.
 * @param   puCheckSum          Where to return the checksum.
 */
static bool SignToolPkcs7Exe_CalcPeCheckSum(PSIGNTOOLPKCS7EXE pThis, RTFILE hFile, uint32_t *puCheckSum)
{
#ifdef RT_OS_WINDOWS
    /*
     * Try use IMAGEHLP!MapFileAndCheckSumW first.
     */
    PRTUTF16 pwszPath;
    int rc = RTStrToUtf16(pThis->pszFilename, &pwszPath);
    if (RT_SUCCESS(rc))
    {
        decltype(MapFileAndCheckSumW) *pfnMapFileAndCheckSumW;
        pfnMapFileAndCheckSumW = (decltype(MapFileAndCheckSumW) *)RTLdrGetSystemSymbol("IMAGEHLP.DLL", "MapFileAndCheckSumW");
        if (pfnMapFileAndCheckSumW)
        {
            DWORD uHeaderSum = UINT32_MAX;
            DWORD uCheckSum  = UINT32_MAX;
            DWORD dwRc = pfnMapFileAndCheckSumW(pwszPath, &uHeaderSum, &uCheckSum);
            if (dwRc == CHECKSUM_SUCCESS)
            {
                *puCheckSum = uCheckSum;
                return true;
            }
        }
    }
#endif

    RT_NOREF(pThis, hFile, puCheckSum);
    RTMsgError("Implement check sum calcuation fallback!");
    return false;
}


/**
 * Writes the signature to the file.
 *
 * This has the side-effect of closing the hLdrMod member.  So, it can only be
 * called once!
 *
 * Caller must have called SignToolPkcs7_Encode() prior to this function.
 *
 * @returns RTEXITCODE_SUCCESS on success, RTEXITCODE_FAILURE with error
 *          message on failure.
 * @param   pThis               The file which to write.
 * @param   cVerbosity          The verbosity.
 */
static RTEXITCODE SignToolPkcs7Exe_WriteSignatureToFile(PSIGNTOOLPKCS7EXE pThis, unsigned cVerbosity)
{
    AssertReturn(pThis->cbNewBuf && pThis->pbNewBuf, RTEXITCODE_FAILURE);

    /*
     * Get the file header offset and arch before closing the destination handle.
     */
    uint32_t offNtHdrs;
    int rc = RTLdrQueryProp(pThis->hLdrMod, RTLDRPROP_FILE_OFF_HEADER, &offNtHdrs, sizeof(offNtHdrs));
    if (RT_SUCCESS(rc))
    {
        RTLDRARCH enmLdrArch = RTLdrGetArch(pThis->hLdrMod);
        if (enmLdrArch != RTLDRARCH_INVALID)
        {
            RTLdrClose(pThis->hLdrMod);
            pThis->hLdrMod = NIL_RTLDRMOD;
            unsigned cbNtHdrs = 0;
            switch (enmLdrArch)
            {
                case RTLDRARCH_AMD64:
                    cbNtHdrs = sizeof(IMAGE_NT_HEADERS64);
                    break;
                case RTLDRARCH_X86_32:
                    cbNtHdrs = sizeof(IMAGE_NT_HEADERS32);
                    break;
                default:
                    RTMsgError("Unknown image arch: %d", enmLdrArch);
            }
            if (cbNtHdrs > 0)
            {
                if (cVerbosity > 0)
                    RTMsgInfo("offNtHdrs=%#x cbNtHdrs=%u\n", offNtHdrs, cbNtHdrs);

                /*
                 * Open the executable file for writing.
                 */
                RTFILE hFile;
                rc = RTFileOpen(&hFile, pThis->pszFilename, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                if (RT_SUCCESS(rc))
                {
                    /* Read the file header and locate the security directory entry. */
                    union
                    {
                        IMAGE_NT_HEADERS32 NtHdrs32;
                        IMAGE_NT_HEADERS64 NtHdrs64;
                    } uBuf;
                    PIMAGE_DATA_DIRECTORY pSecDir = cbNtHdrs == sizeof(IMAGE_NT_HEADERS64)
                                                  ? &uBuf.NtHdrs64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY]
                                                  : &uBuf.NtHdrs32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];

                    rc = RTFileReadAt(hFile, offNtHdrs, &uBuf, cbNtHdrs, NULL);
                    if (   RT_SUCCESS(rc)
                        && uBuf.NtHdrs32.Signature == IMAGE_NT_SIGNATURE)
                    {
                        /*
                         * Drop any old signature by truncating the file.
                         */
                        if (   pSecDir->Size > 8
                            && pSecDir->VirtualAddress > offNtHdrs + sizeof(IMAGE_NT_HEADERS32))
                        {
                            rc = RTFileSetSize(hFile, pSecDir->VirtualAddress);
                            if (RT_FAILURE(rc))
                                RTMsgError("Error truncating file to %#x bytes: %Rrc", pSecDir->VirtualAddress, rc);
                        }
                        else
                            rc = RTMsgErrorRc(VERR_BAD_EXE_FORMAT, "Bad security directory entry: VA=%#x Size=%#x",
                                              pSecDir->VirtualAddress, pSecDir->Size);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Sector align the signature portion.
                             */
                            uint32_t const  cbWinCert = RT_UOFFSETOF(WIN_CERTIFICATE, bCertificate);
                            uint64_t        offCur    = 0;
                            rc = RTFileQuerySize(hFile, &offCur);
                            if (   RT_SUCCESS(rc)
                                && offCur < _2G)
                            {
                                if (offCur & 0x1ff)
                                {
                                    uint32_t cbNeeded = 0x200 - ((uint32_t)offCur & 0x1ff);
                                    rc = RTFileWriteAt(hFile, offCur, g_abRTZero4K, cbNeeded, NULL);
                                    if (RT_SUCCESS(rc))
                                        offCur += cbNeeded;
                                }
                                if (RT_SUCCESS(rc))
                                {
                                    /*
                                     * Write the header followed by the signature data.
                                     */
                                    uint32_t const cbZeroPad = (uint32_t)(RT_ALIGN_Z(pThis->cbNewBuf, 8) - pThis->cbNewBuf);
                                    pSecDir->VirtualAddress  = (uint32_t)offCur;
                                    pSecDir->Size            = cbWinCert + (uint32_t)pThis->cbNewBuf + cbZeroPad;
                                    if (cVerbosity >= 2)
                                        RTMsgInfo("Writing %u (%#x) bytes of signature at %#x (%u).\n",
                                                  pSecDir->Size, pSecDir->Size, pSecDir->VirtualAddress, pSecDir->VirtualAddress);

                                    WIN_CERTIFICATE WinCert;
                                    WinCert.dwLength         = pSecDir->Size;
                                    WinCert.wRevision        = WIN_CERT_REVISION_2_0;
                                    WinCert.wCertificateType = WIN_CERT_TYPE_PKCS_SIGNED_DATA;

                                    rc = RTFileWriteAt(hFile, offCur, &WinCert, cbWinCert, NULL);
                                    if (RT_SUCCESS(rc))
                                    {
                                        offCur += cbWinCert;
                                        rc = RTFileWriteAt(hFile, offCur, pThis->pbNewBuf, pThis->cbNewBuf, NULL);
                                    }
                                    if (RT_SUCCESS(rc) && cbZeroPad)
                                    {
                                        offCur += pThis->cbNewBuf;
                                        rc = RTFileWriteAt(hFile, offCur, g_abRTZero4K, cbZeroPad, NULL);
                                    }
                                    if (RT_SUCCESS(rc))
                                    {
                                        /*
                                         * Reset the checksum (sec dir updated already) and rewrite the header.
                                         */
                                        uBuf.NtHdrs32.OptionalHeader.CheckSum = 0;
                                        offCur = offNtHdrs;
                                        rc = RTFileWriteAt(hFile, offNtHdrs, &uBuf, cbNtHdrs, NULL);
                                        if (RT_SUCCESS(rc))
                                            rc = RTFileFlush(hFile);
                                        if (RT_SUCCESS(rc))
                                        {
                                            /*
                                             * Calc checksum and write out the header again.
                                             */
                                            uint32_t uCheckSum = UINT32_MAX;
                                            if (SignToolPkcs7Exe_CalcPeCheckSum(pThis, hFile, &uCheckSum))
                                            {
                                                uBuf.NtHdrs32.OptionalHeader.CheckSum = uCheckSum;
                                                rc = RTFileWriteAt(hFile, offNtHdrs, &uBuf, cbNtHdrs, NULL);
                                                if (RT_SUCCESS(rc))
                                                    rc = RTFileFlush(hFile);
                                                if (RT_SUCCESS(rc))
                                                {
                                                    rc = RTFileClose(hFile);
                                                    if (RT_SUCCESS(rc))
                                                        return RTEXITCODE_SUCCESS;
                                                    RTMsgError("RTFileClose failed: %Rrc\n", rc);
                                                    return RTEXITCODE_FAILURE;
                                                }
                                            }
                                        }
                                    }
                                }
                                if (RT_FAILURE(rc))
                                    RTMsgError("Write error at %#RX64: %Rrc", offCur, rc);
                            }
                            else if (RT_SUCCESS(rc))
                                RTMsgError("File to big: %'RU64 bytes", offCur);
                            else
                                RTMsgError("RTFileQuerySize failed: %Rrc", rc);
                        }
                    }
                    else if (RT_SUCCESS(rc))
                        RTMsgError("Not NT executable header!");
                    else
                        RTMsgError("Error reading NT headers (%#x bytes) at %#x: %Rrc", cbNtHdrs, offNtHdrs, rc);
                    RTFileClose(hFile);
                }
                else
                    RTMsgError("Failed to open '%s' for writing: %Rrc", pThis->pszFilename, rc);
            }
        }
        else
            RTMsgError("RTLdrGetArch failed!");
    }
    else
        RTMsgError("RTLdrQueryProp/RTLDRPROP_FILE_OFF_HEADER failed: %Rrc", rc);
    return RTEXITCODE_FAILURE;
}

#ifndef IPRT_IN_BUILD_TOOL

static PRTCRPKCS7ATTRIBUTE SignToolPkcs7_AuthAttribAppend(PRTCRPKCS7ATTRIBUTES pAuthAttribs)
{
    int32_t iPos = RTCrPkcs7Attributes_Append(pAuthAttribs);
    if (iPos >= 0)
        return pAuthAttribs->papItems[iPos];
    RTMsgError("RTCrPkcs7Attributes_Append failed: %Rrc", iPos);
    return NULL;
}


static RTEXITCODE SignToolPkcs7_AuthAttribsAddSigningTime(PRTCRPKCS7ATTRIBUTES pAuthAttribs, RTTIMESPEC SigningTime)
{
    /*
     * Signing time.  For the old-style timestamps, Symantec used ASN.1 UTC TIME.
     *                              start -vv    vv=ASN1_TAG_UTC_TIME
     *  00000187d6a65fd0/23b0: 0d 01 09 05 31 0f 17 0d-31 36 31 30 30 35 30 37 ....1...16100507
     *  00000187d6a65fe0/23c0: 35 30 33 30 5a 30 23 06-09 2a 86 48 86 f7 0d 01 5030Z0#..*.H....
     *                                     ^^- end 2016-10-05T07:50:30.000000000Z (161005075030Z)
     */
    PRTCRPKCS7ATTRIBUTE pAttr = SignToolPkcs7_AuthAttribAppend(pAuthAttribs);
    if (!pAttr)
        return RTEXITCODE_FAILURE;

    int rc = RTCrPkcs7Attribute_SetSigningTime(pAttr, NULL, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attribute_SetSigningTime failed: %Rrc", rc);

    /* Create the timestamp. */
    int32_t iPos = RTAsn1SetOfTimes_Append(pAttr->uValues.pSigningTime);
    if (iPos < 0)
        return RTMsgErrorExitFailure("RTAsn1SetOfTimes_Append failed: %Rrc", iPos);

    PRTASN1TIME pTime = pAttr->uValues.pSigningTime->papItems[iPos];
    rc = RTAsn1Time_SetTimeSpec(pTime, pAttr->Allocation.pAllocator, &SigningTime);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1Time_SetTimeSpec failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AuthAttribsAddSpcOpusInfo(PRTCRPKCS7ATTRIBUTES pAuthAttribs, void *pvInfo)
{
    PRTCRPKCS7ATTRIBUTE pAttr = SignToolPkcs7_AuthAttribAppend(pAuthAttribs);
    if (!pAttr)
        return RTEXITCODE_FAILURE;

    int rc = RTCrPkcs7Attribute_SetMsStatementType(pAttr, NULL, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attribute_SetMsStatementType failed: %Rrc", rc);

    /* Override the ID. */
    rc = RTAsn1ObjId_SetFromString(&pAttr->Type, RTCR_PKCS9_ID_MS_SP_OPUS_INFO, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ObjId_SetFromString failed: %Rrc", rc);

    /* Add attribute value entry. */
    int32_t iPos = RTAsn1SetOfObjIdSeqs_Append(pAttr->uValues.pObjIdSeqs);
    if (iPos < 0)
        return RTMsgErrorExitFailure("RTAsn1SetOfObjIdSeqs_Append failed: %Rrc", iPos);

    RT_NOREF(pvInfo); Assert(!pvInfo);
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AuthAttribsAddMsStatementType(PRTCRPKCS7ATTRIBUTES pAuthAttribs, const char *pszTypeId)
{
    PRTCRPKCS7ATTRIBUTE pAttr = SignToolPkcs7_AuthAttribAppend(pAuthAttribs);
    if (!pAttr)
        return RTEXITCODE_FAILURE;

    int rc = RTCrPkcs7Attribute_SetMsStatementType(pAttr, NULL, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attribute_SetMsStatementType failed: %Rrc", rc);

    /* Add attribute value entry. */
    int32_t iPos = RTAsn1SetOfObjIdSeqs_Append(pAttr->uValues.pObjIdSeqs);
    if (iPos < 0)
        return RTMsgErrorExitFailure("RTAsn1SetOfObjIdSeqs_Append failed: %Rrc", iPos);
    PRTASN1SEQOFOBJIDS pSeqObjIds = pAttr->uValues.pObjIdSeqs->papItems[iPos];

    /* Add a object id to the value. */
    RTASN1OBJID ObjIdValue;
    rc = RTAsn1ObjId_InitFromString(&ObjIdValue, pszTypeId, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ObjId_InitFromString/%s failed: %Rrc", pszTypeId, rc);

    rc = RTAsn1SeqOfObjIds_InsertEx(pSeqObjIds, 0 /*iPos*/, &ObjIdValue, &g_RTAsn1DefaultAllocator, NULL);
    RTAsn1ObjId_Delete(&ObjIdValue);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1SeqOfObjIds_InsertEx failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE SignToolPkcs7_AuthAttribsAddContentType(PRTCRPKCS7ATTRIBUTES pAuthAttribs, const char *pszContentTypeId)
{
    PRTCRPKCS7ATTRIBUTE pAttr = SignToolPkcs7_AuthAttribAppend(pAuthAttribs);
    if (!pAttr)
        return RTEXITCODE_FAILURE;

    int rc = RTCrPkcs7Attribute_SetContentType(pAttr, NULL, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attribute_SetContentType failed: %Rrc", rc);

    /* Add a object id to the value. */
    RTASN1OBJID ObjIdValue;
    rc = RTAsn1ObjId_InitFromString(&ObjIdValue, pszContentTypeId, pAuthAttribs->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ObjId_InitFromString/%s failed: %Rrc", pszContentTypeId, rc);

    rc = RTAsn1SetOfObjIds_InsertEx(pAttr->uValues.pObjIds, 0 /*iPos*/, &ObjIdValue, pAuthAttribs->Allocation.pAllocator, NULL);
    RTAsn1ObjId_Delete(&ObjIdValue);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1SetOfObjIds_InsertEx failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AddAuthAttribsForTimestamp(PRTCRPKCS7ATTRIBUTES pAuthAttribs, bool fTimestampTypeOld,
                                                           RTTIMESPEC SigningTime,  PCRTCRX509CERTIFICATE pTimestampCert)
{
    /*
     * Add signing time.
     */
    RTEXITCODE rcExit = SignToolPkcs7_AuthAttribsAddSigningTime(pAuthAttribs, SigningTime);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * More later if we want to support fTimestampTypeOld = false perhaps?
     */
    Assert(fTimestampTypeOld);
    RT_NOREF(fTimestampTypeOld, pTimestampCert);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AddAuthAttribsForImageSignature(PRTCRPKCS7ATTRIBUTES pAuthAttribs, RTTIMESPEC SigningTime)
{
    /*
     * Add SpcOpusInfo.  No attribute values.
     *                      SEQ start -vv    vv- Type ObjId
     *   1c60: 0e 03 02 1a 05 00 a0 70-30 10 06 0a 2b 06 01 04 .......p0...+...
     *   1c70: 01 82 37 02 01 0c 31 02-30 00 30 19 06 09 2a 86 ..7...1.0.0...*.
     *                   Set Of -^^    ^^- Empty Sequence.
     */
    RTEXITCODE rcExit = SignToolPkcs7_AuthAttribsAddSpcOpusInfo(pAuthAttribs, NULL /*pvInfo - none*/);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Add ContentType = Ms-SpcIndirectDataContext?
     *                            SEQ start -vv    vv- Type ObjId
     *   1c70: 01 82 37 02 01 0c 31 02-30 00 30 19 06 09 2a 86 ..7...1.0.0...*.
     *   1c80: 48 86 f7 0d 01 09 03 31-0c 06 0a 2b 06 01 04 01 H......1...+....
     *   1c90: 82 37 02 01 04       ^^-   ^^- ObjId
     *                              ^- Set Of
     */
    rcExit = SignToolPkcs7_AuthAttribsAddContentType(pAuthAttribs, RTCRSPCINDIRECTDATACONTENT_OID);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Add Ms-SpcStatementType = Ms-SpcIndividualCodeSigning.
     *             SEQ start -vv    vv- Type ObjId
     *   1c90: 82 37 02 01 04 30 1c 06-0a 2b 06 01 04 01 82 37 .7...0...+.....7
     *   1ca0: 02 01 0b 31 0e 30 0c 06-0a 2b 06 01 04 01 82 37 ...1.0...+.....7
     *   1cb0: 02 01 15 ^^    ^^    ^^- ObjId
     *          Set Of -^^    ^^- Sequence Of
     */
    rcExit = SignToolPkcs7_AuthAttribsAddMsStatementType(pAuthAttribs, RTCRSPC_STMT_TYPE_INDIVIDUAL_CODE_SIGNING);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Add signing time. We add this, even if signtool.exe, since OpenSSL will always do it otherwise.
     */
    rcExit = SignToolPkcs7_AuthAttribsAddSigningTime(pAuthAttribs, SigningTime);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /** @todo more? Some certificate stuff?   */

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_PrependCounterSignature(PRTCRPKCS7SIGNERINFO pSignerInfo,
                                                        PCRTCRPKCS7SIGNERINFO pCounterSignerInfo, unsigned cVerbosity)
{
    /* Make sure the UnauthenticatedAttributes member is there. */
    RTEXITCODE rcExit = SignToolPkcs7_EnsureUnauthenticatedAttributesPresent(pSignerInfo);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /* Append an entry to UnauthenticatedAttributes. */
    uint32_t iPos;
    int rc = RTCrPkcs7Attributes_InsertEx(&pSignerInfo->UnauthenticatedAttributes, 0 /*iPosition*/, NULL /*pToClone*/,
                                          &g_RTAsn1DefaultAllocator, &iPos);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7Attributes_Append failed: %Rrc", rc);
    Assert(iPos < pSignerInfo->UnauthenticatedAttributes.cItems); Assert(iPos == 0);
    PRTCRPKCS7ATTRIBUTE pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iPos];

    if (cVerbosity >= 2)
        RTMsgInfo("Adding UnauthenticatedAttribute #%u...", iPos);

    /* Create the attrib and its sub-set of counter signatures. */
#if 1
    rc = RTCrPkcs7Attribute_SetCounterSignatures(pAttr, NULL, pAttr->Allocation.pAllocator);
#else
    rc = RTAsn1ObjId_InitFromString(&pAttr->Type, RTCR_PKCS9_ID_COUNTER_SIGNATURE_OID, pAttr->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ObjId_InitFromString failed: %Rrc", rc);

    /** @todo Generalize the Type + enmType DYN stuff and generate setters. */
    Assert(pAttr->enmType == RTCRPKCS7ATTRIBUTETYPE_NOT_PRESENT);
    Assert(pAttr->uValues.pContentInfos == NULL);
    pAttr->enmType = RTCRPKCS7ATTRIBUTETYPE_COUNTER_SIGNATURES;
    rc = RTAsn1MemAllocZ(&pAttr->Allocation, (void **)&pAttr->uValues.pCounterSignatures,
                         sizeof(*pAttr->uValues.pCounterSignatures));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1MemAllocZ failed: %Rrc", rc);

    rc = RTCrPkcs7SignerInfos_Init(pAttr->uValues.pCounterSignatures, pAttr->Allocation.pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7SignerInfos_Init failed: %Rrc", rc);
#endif

    /* Insert the counter signature. */
    rc = RTCrPkcs7SignerInfos_InsertEx(pAttr->uValues.pCounterSignatures, 0 /*iPosition*/, pCounterSignerInfo,
                                       pAttr->Allocation.pAllocator, NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7SignerInfos_InsertEx failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AppendCertificate(PRTCRPKCS7SIGNEDDATA pSignedData, PCRTCRX509CERTIFICATE pCertToAppend)
{
    if (pSignedData->Certificates.cItems == 0 && !RTCrPkcs7SetOfCerts_IsPresent(&pSignedData->Certificates))
        return RTMsgErrorExitFailure("PKCS#7 signature includes no certificates! Didn't expect that");

    /* Already there? */
    PCRTCRX509CERTIFICATE pExisting
        = RTCrPkcs7SetOfCerts_FindX509ByIssuerAndSerialNumber(&pSignedData->Certificates, &pCertToAppend->TbsCertificate.Issuer,
                                                              &pCertToAppend->TbsCertificate.SerialNumber);
    if (!pExisting || RTCrX509Certificate_Compare(pExisting, pCertToAppend) != 0)
    {
        /* Prepend a RTCRPKCS7CERT entry. */
        uint32_t iPos;
        int rc = RTCrPkcs7SetOfCerts_InsertEx(&pSignedData->Certificates, 0 /*iPosition*/, NULL /*pToClone*/,
                                              &g_RTAsn1DefaultAllocator, &iPos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrPkcs7SetOfCerts_Append failed: %Rrc", rc);
        PRTCRPKCS7CERT pCertEntry = pSignedData->Certificates.papItems[iPos];

        /* Set (clone) the certificate. */
        rc = RTCrPkcs7Cert_SetX509Cert(pCertEntry, pCertToAppend, pCertEntry->Allocation.pAllocator);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrPkcs7Cert_X509Cert failed: %Rrc", rc);
    }
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE Pkcs7SignStuff(const char *pszWhat, const void *pvToDataToSign, size_t cbToDataToSign,
                                 PCRTCRPKCS7ATTRIBUTES pAuthAttribs, RTCRSTORE hAdditionalCerts, uint32_t fExtraFlags,
                                 RTDIGESTTYPE enmDigestType, SIGNTOOLKEYPAIR *pCertKeyPair,
                                 unsigned cVerbosity, void **ppvSigned, size_t *pcbSigned,
                                 PRTCRPKCS7CONTENTINFO pContentInfo, PRTCRPKCS7SIGNEDDATA *ppSignedData)
{
    *ppvSigned = NULL;
    if (pcbSigned)
        *pcbSigned = 0;
    if (ppSignedData)
        *ppSignedData = NULL;

    /* Figure out how large the signature will be. */
    RTERRINFOSTATIC ErrInfo;
    size_t cbSigned = 1024;
    int rc = RTCrPkcs7SimpleSignSignedData(RTCRPKCS7SIGN_SD_F_NO_SMIME_CAP | fExtraFlags,
                                           pCertKeyPair->pCertificate, pCertKeyPair->hPrivateKey, pvToDataToSign, cbToDataToSign,
                                           enmDigestType, hAdditionalCerts, pAuthAttribs,
                                           NULL, &cbSigned, RTErrInfoInitStatic(&ErrInfo));
    if (rc != VERR_BUFFER_OVERFLOW)
        return RTMsgErrorExitFailure("RTCrPkcs7SimpleSignSignedData failed: %Rrc%#RTeim", rc, &ErrInfo.Core);

    /* Allocate memory for it and do the actual signing. */
    void *pvSigned = RTMemAllocZ(cbSigned);
    if (!pvSigned)
        return RTMsgErrorExitFailure("Failed to allocate %#zx bytes for %s signature", cbSigned, pszWhat);
    rc = RTCrPkcs7SimpleSignSignedData(RTCRPKCS7SIGN_SD_F_NO_SMIME_CAP | fExtraFlags,
                                       pCertKeyPair->pCertificate, pCertKeyPair->hPrivateKey, pvToDataToSign, cbToDataToSign,
                                       enmDigestType, hAdditionalCerts, pAuthAttribs,
                                       pvSigned, &cbSigned, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        if (cVerbosity > 2)
            RTMsgInfo("%s signature: %#zx bytes\n%.*Rhxd\n", pszWhat, cbSigned, cbSigned, pvSigned);

        /*
         * Decode the signature and check that it is SignedData.
         */
        RTASN1CURSORPRIMARY PrimaryCursor;
        RTAsn1CursorInitPrimary(&PrimaryCursor, pvSigned, (uint32_t)cbSigned, RTErrInfoInitStatic(&ErrInfo),
                                &g_RTAsn1DefaultAllocator, 0, pszWhat);
        rc = RTCrPkcs7ContentInfo_DecodeAsn1(&PrimaryCursor.Cursor, 0, pContentInfo, "CI");
        if (RT_SUCCESS(rc))
        {
            if (RTCrPkcs7ContentInfo_IsSignedData(pContentInfo))
            {
                *ppvSigned = pvSigned;
                if (pcbSigned)
                    *pcbSigned = cbSigned;
                if (ppSignedData)
                    *ppSignedData = pContentInfo->u.pSignedData;

                if (cVerbosity)
                {
                    SHOWEXEPKCS7 ShowExe;
                    RT_ZERO(ShowExe);
                    ShowExe.cVerbosity = cVerbosity;
                    HandleShowExeWorkerPkcs7Display(&ShowExe, pContentInfo->u.pSignedData, 0, pContentInfo);
                }
                return RTEXITCODE_SUCCESS;
            }

            RTMsgError("RTCrPkcs7SimpleSignSignedData did not create SignedData: %s", pContentInfo->ContentType.szObjId);
        }
        else
            RTMsgError("RTCrPkcs7ContentInfo_DecodeAsn1 failed: %Rrc%#RTeim", rc, &ErrInfo.Core);
        RTCrPkcs7ContentInfo_Delete(pContentInfo);
    }
    RTMemFree(pvSigned);
    return RTEXITCODE_FAILURE;
}


static RTEXITCODE SignToolPkcs7_AddTimestampSignatureEx(PRTCRPKCS7SIGNERINFO pSignerInfo, PRTCRPKCS7SIGNEDDATA pSignedData,
                                                        unsigned cVerbosity,  bool fReplaceExisting, bool fTimestampTypeOld,
                                                        RTTIMESPEC SigningTime, SIGNTOOLKEYPAIR *pTimestampPair)
{
    AssertReturn(fTimestampTypeOld, RTMsgErrorExitFailure("New style signatures not supported yet"));

    /*
     * Create a set of attributes we need to include in the AuthenticatedAttributes
     * of the timestamp signature.
     */
    RTCRPKCS7ATTRIBUTES AuthAttribs;
    int rc = RTCrPkcs7Attributes_Init(&AuthAttribs, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrPkcs7SetOfAttributes_Init failed: %Rrc", rc);

    RTEXITCODE rcExit = SignToolPkcs7_AddAuthAttribsForTimestamp(&AuthAttribs, fTimestampTypeOld, SigningTime,
                                                                 pTimestampPair->pCertificate);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Now create a PKCS#7 signature of the encrypted signature from the selected signer info.
         */
        void                *pvSigned      = NULL;
        PRTCRPKCS7SIGNEDDATA pTsSignedData = NULL;
        RTCRPKCS7CONTENTINFO TsContentInfo;
        rcExit = Pkcs7SignStuff("timestamp", pSignerInfo->EncryptedDigest.Asn1Core.uData.pv,
                                pSignerInfo->EncryptedDigest.Asn1Core.cb, &AuthAttribs, NIL_RTCRSTORE /*hAdditionalCerts*/,
                                RTCRPKCS7SIGN_SD_F_DEATCHED, RTDIGESTTYPE_SHA1, pTimestampPair, cVerbosity,
                                &pvSigned, NULL /*pcbSigned*/, &TsContentInfo, &pTsSignedData);
        if (rcExit == RTEXITCODE_SUCCESS)
        {

            /*
             * If we're replacing existing timestamp signatures, remove old ones now.
             */
            if (   fReplaceExisting
                && RTCrPkcs7Attributes_IsPresent(&pSignerInfo->UnauthenticatedAttributes))
            {
                uint32_t iItem = pSignerInfo->UnauthenticatedAttributes.cItems;
                while (iItem-- > 0)
                {
                    PRTCRPKCS7ATTRIBUTE pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[iItem];
                    if (pAttr->enmType == RTCRPKCS7ATTRIBUTETYPE_COUNTER_SIGNATURES) /* ASSUMES all counter sigs are timstamps */
                    {
                        if (cVerbosity > 1)
                            RTMsgInfo("Removing counter signature in attribute #%u\n", iItem);
                        rc = RTCrPkcs7Attributes_Erase(&pSignerInfo->UnauthenticatedAttributes, iItem);
                        if (RT_FAILURE(rc))
                            rcExit = RTMsgErrorExitFailure("RTCrPkcs7Attributes_Erase failed on #%u: %Rrc", iItem, rc);
                    }
                }
            }

            /*
             * Add the new one.
             */
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7_PrependCounterSignature(pSignerInfo, pTsSignedData->SignerInfos.papItems[0], cVerbosity);

            /*
             * Make sure the signing certificate is included.
             */
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7_AppendCertificate(pSignedData, pTimestampPair->pCertificate);

            /*
             * Clean up.
             */
            RTCrPkcs7ContentInfo_Delete(&TsContentInfo);
            RTMemFree(pvSigned);
        }
    }
    RTCrPkcs7Attributes_Delete(&AuthAttribs);
    return rcExit;
}


static RTEXITCODE SignToolPkcs7_AddTimestampSignature(SIGNTOOLPKCS7EXE *pThis, unsigned cVerbosity, unsigned iSignature,
                                                      bool fReplaceExisting, bool fTimestampTypeOld, RTTIMESPEC SigningTime,
                                                      SIGNTOOLKEYPAIR *pTimestampPair)
{
    AssertReturn(fTimestampTypeOld, RTMsgErrorExitFailure("New style signatures not supported yet"));

    /*
     * Locate the signature specified by iSignature and add a timestamp to it.
     */
    PRTCRPKCS7SIGNEDDATA pSignedData = NULL;
    PRTCRPKCS7SIGNERINFO pSignerInfo = SignToolPkcs7_FindNestedSignatureByIndex(pThis, iSignature, &pSignedData);
    if (!pSignerInfo)
        return RTMsgErrorExitFailure("No signature #%u in %s", iSignature, pThis->pszFilename);

    return SignToolPkcs7_AddTimestampSignatureEx(pSignerInfo, pSignedData, cVerbosity, fReplaceExisting, fTimestampTypeOld,
                                                 SigningTime, pTimestampPair);
}


static RTEXITCODE SignToolPkcs7_SignSpcIndData(SIGNTOOLPKCS7EXE *pThis, RTCRSPCINDIRECTDATACONTENT *pSpcIndData,
                                               unsigned cVerbosity,  RTDIGESTTYPE enmSigType, bool fReplaceExisting,
                                               SIGNTOOLKEYPAIR *pSigningCertKey, RTCRSTORE hAddCerts,
                                               bool fTimestampTypeOld, RTTIMESPEC SigningTime, SIGNTOOLKEYPAIR *pTimestampCertKey)
{
    /*
     * Encode it.
     */
    RTERRINFOSTATIC ErrInfo;
    PRTASN1CORE     pSpcRoot     = RTCrSpcIndirectDataContent_GetAsn1Core(pSpcIndData);
    uint32_t        cbSpcEncoded = 0;
    int rc = RTAsn1EncodePrepare(pSpcRoot, RTASN1ENCODE_F_DER, &cbSpcEncoded, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1EncodePrepare failed: %Rrc%RTeim", rc, &ErrInfo.Core);

    if (cVerbosity >= 4)
        RTAsn1Dump(pSpcRoot, 0, 0, RTStrmDumpPrintfV, g_pStdOut);

    void *pvSpcEncoded = (uint8_t *)RTMemTmpAllocZ(cbSpcEncoded);
    if (!pvSpcEncoded)
        return RTMsgErrorExitFailure("Failed to allocate %#z bytes for SpcIndirectData", cbSpcEncoded);

    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    rc = RTAsn1EncodeToBuffer(pSpcRoot, RTASN1ENCODE_F_DER, pvSpcEncoded, cbSpcEncoded, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        /*
         * Create additional authenticated attributes.
         */
        RTCRPKCS7ATTRIBUTES AuthAttribs;
        rc = RTCrPkcs7Attributes_Init(&AuthAttribs, &g_RTAsn1DefaultAllocator);
        if (RT_SUCCESS(rc))
        {
            rcExit = SignToolPkcs7_AddAuthAttribsForImageSignature(&AuthAttribs, SigningTime);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                /*
                 * Ditch the old signature if so desired.
                 */
                if (fReplaceExisting && pThis->pSignedData)
                {
                    RTCrPkcs7ContentInfo_Delete(&pThis->ContentInfo);
                    pThis->pSignedData = NULL;
                    RTMemFree(pThis->pbBuf);
                    pThis->pbBuf = NULL;
                    pThis->cbBuf = 0;
                }

                /*
                 * Do the actual signing.
                 */
                SIGNTOOLPKCS7  Src     = { NULL, 0, NULL };
                PSIGNTOOLPKCS7 pSigDst = !pThis->pSignedData ? pThis : &Src;
                rcExit = Pkcs7SignStuff("image", pvSpcEncoded, cbSpcEncoded, &AuthAttribs, hAddCerts, 0 /*fExtraFlags*/,
                                        enmSigType /** @todo ?? */, pSigningCertKey, cVerbosity,
                                        (void **)&pSigDst->pbBuf, &pSigDst->cbBuf, &pSigDst->ContentInfo, &pSigDst->pSignedData);
                if (rcExit == RTEXITCODE_SUCCESS)
                {
                    /*
                     * Add a timestamp signature if requested.
                     */
                    if (pTimestampCertKey->isComplete())
                        rcExit = SignToolPkcs7_AddTimestampSignatureEx(pSigDst->pSignedData->SignerInfos.papItems[0],
                                                                       pSigDst->pSignedData,
                                                                       cVerbosity, false /*fReplaceExisting*/,
                                                                       fTimestampTypeOld, SigningTime, pTimestampCertKey);

                    /*
                     * Append the signature to the existing one, if that's what we're doing.
                     */
                    if (rcExit == RTEXITCODE_SUCCESS && pSigDst == &Src)
                        rcExit = SignToolPkcs7_AddNestedSignature(pThis, &Src, cVerbosity, true /*fPrepend*/); /** @todo prepend/append option */

                    /* cleanup */
                    if (pSigDst == &Src)
                        SignToolPkcs7_Delete(&Src);
                }
            }
            RTCrPkcs7Attributes_Delete(&AuthAttribs);
        }
        else
            RTMsgError("RTCrPkcs7SetOfAttributes_Init failed: %Rrc", rc);
    }
    else
        RTMsgError("RTAsn1EncodeToBuffer failed: %Rrc", rc);
    RTMemTmpFree(pvSpcEncoded);
    return rcExit;
}


static RTEXITCODE SignToolPkcs7_SpcCompleteWithoutPageHashes(RTCRSPCINDIRECTDATACONTENT *pSpcIndData)
{
    PCRTASN1ALLOCATORVTABLE const pAllocator = &g_RTAsn1DefaultAllocator;
    PRTCRSPCPEIMAGEDATA const     pPeImage   = pSpcIndData->Data.uValue.pPeImage;
    Assert(pPeImage);

    /*
     * Set it to File with an empty name.
     *         RTCRSPCPEIMAGEDATA::Flags -vv
     * RTCRSPCPEIMAGEDATA::SeqCore -vv         T0 -vv    vv- pT2/CtxTag2
     *   0040: 04 01 82 37 02 01 0f 30-09 03 01 00 a0 04 a2 02 ...7...0........
     *   0050: 80 00 30 21 30 09 06 05-2b 0e 03 02 1a 05 00 04 ..0!0...+.......
     *         ^^- pUcs2 / empty string
     */

    /* Create an empty BMP string. */
    RTASN1STRING EmptyStr;
    int rc = RTAsn1BmpString_Init(&EmptyStr, pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1BmpString_Init/Ucs2 failed: %Rrc", rc);

    /* Create an SPC string and use the above empty string with the Ucs2 setter. */
    RTEXITCODE    rcExit = RTEXITCODE_FAILURE;
    RTCRSPCSTRING SpcString;
    rc = RTCrSpcString_Init(&SpcString, pAllocator);
    if (RT_SUCCESS(rc))
    {
        rc = RTCrSpcString_SetUcs2(&SpcString, &EmptyStr, pAllocator);
        if (RT_SUCCESS(rc))
        {
            /* Create a temporary SpcLink with the empty SpcString. */
            RTCRSPCLINK SpcLink;
            rc = RTCrSpcLink_Init(&SpcLink, pAllocator);
            if (RT_SUCCESS(rc))
            {
                /* Use the setter on the SpcLink object to copy the SpcString to it. */
                rc = RTCrSpcLink_SetFile(&SpcLink, &SpcString, pAllocator);
                if (RT_SUCCESS(rc))
                {
                    /* Use the setter to copy SpcLink to the PeImage structure. */
                    rc = RTCrSpcPeImageData_SetFile(pPeImage, &SpcLink, pAllocator);
                    if (RT_SUCCESS(rc))
                        rcExit = RTEXITCODE_SUCCESS;
                    else
                        RTMsgError("RTCrSpcPeImageData_SetFile failed: %Rrc", rc);
                }
                else
                    RTMsgError("RTCrSpcLink_SetFile failed: %Rrc", rc);
                RTCrSpcLink_Delete(&SpcLink);
            }
            else
                RTMsgError("RTCrSpcLink_Init failed: %Rrc", rc);
        }
        else
            RTMsgError("RTCrSpcString_SetUcs2 failed: %Rrc", rc);
        RTCrSpcString_Delete(&SpcString);
    }
    else
        RTMsgError("RTCrSpcString_Init failed: %Rrc", rc);
    RTAsn1BmpString_Delete(&EmptyStr);
    return rcExit;
}


static RTEXITCODE SignToolPkcs7_SpcAddImagePageHashes(SIGNTOOLPKCS7EXE *pThis, RTCRSPCINDIRECTDATACONTENT *pSpcIndData,
                                                      RTDIGESTTYPE enmSigType)
{
    RT_NOREF(pThis, pSpcIndData, enmSigType);
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_SpcAddImageHash(SIGNTOOLPKCS7EXE *pThis, RTCRSPCINDIRECTDATACONTENT *pSpcIndData,
                                                RTDIGESTTYPE enmSigType)
{
    uint32_t     const cbHash   = RTCrDigestTypeToHashSize(enmSigType);
    const char * const pszAlgId = RTCrDigestTypeToAlgorithmOid(enmSigType);

    /*
     * Ask the loader for the hash.
     */
    uint8_t abHash[RTSHA512_HASH_SIZE];
    int rc = RTLdrHashImage(pThis->hLdrMod, enmSigType, abHash, sizeof(abHash));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTLdrHashImage/%s failed: %Rrc", RTCrDigestTypeToName(enmSigType), rc);

    /*
     * Set it.
     */
    /** @todo no setter, this should be okay, though...   */
    rc = RTAsn1ObjId_InitFromString(&pSpcIndData->DigestInfo.DigestAlgorithm.Algorithm, pszAlgId, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ObjId_InitFromString/%s failed: %Rrc", pszAlgId, rc);

    rc = RTAsn1ContentDup(&pSpcIndData->DigestInfo.Digest.Asn1Core, abHash, cbHash, &g_RTAsn1DefaultAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTAsn1ContentDup/%#x failed: %Rrc", cbHash, rc);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE SignToolPkcs7_AddOrReplaceSignature(SIGNTOOLPKCS7EXE *pThis, unsigned cVerbosity, RTDIGESTTYPE enmSigType,
                                                      bool fReplaceExisting,  bool fHashPages, SIGNTOOLKEYPAIR *pSigningCertKey,
                                                      RTCRSTORE hAddCerts,  bool fTimestampTypeOld,
                                                      RTTIMESPEC SigningTime, SIGNTOOLKEYPAIR *pTimestampCertKey)
{
    AssertReturn(fTimestampTypeOld || pTimestampCertKey->isNull(),
                 RTMsgErrorExitFailure("New style signatures not supported yet"));

    /*
     * We must construct the data to be backed into the PKCS#7 signature
     * and signed.
     */
    PCRTASN1ALLOCATORVTABLE const   pAllocator = &g_RTAsn1DefaultAllocator;
    RTCRSPCINDIRECTDATACONTENT      SpcIndData;
    int rc = RTCrSpcIndirectDataContent_Init(&SpcIndData, pAllocator);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTCrSpcIndirectDataContent_Init failed: %Rrc", rc);

    /* Set the data to PE image. */
    /** @todo Generalize the Type + enmType DYN stuff and generate setters. */
    Assert(SpcIndData.Data.enmType == RTCRSPCAAOVTYPE_NOT_PRESENT);
    Assert(SpcIndData.Data.uValue.pPeImage == NULL);
    RTEXITCODE rcExit;
    rc = RTAsn1ObjId_SetFromString(&SpcIndData.Data.Type, RTCRSPCPEIMAGEDATA_OID, pAllocator);
    if (RT_SUCCESS(rc))
    {
        SpcIndData.Data.enmType = RTCRSPCAAOVTYPE_PE_IMAGE_DATA;
        rc = RTAsn1MemAllocZ(&SpcIndData.Data.Allocation, (void **)&SpcIndData.Data.uValue.pPeImage,
                             sizeof(*SpcIndData.Data.uValue.pPeImage));
        if (RT_SUCCESS(rc))
        {
            rc = RTCrSpcPeImageData_Init(SpcIndData.Data.uValue.pPeImage, pAllocator);
            if (RT_SUCCESS(rc))
            {
                /* Old (SHA1) signatures has a Flags member, it's zero bits, though. */
                if (enmSigType == RTDIGESTTYPE_SHA1)
                {
                    uint8_t         bFlags = 0;
                    RTASN1BITSTRING Flags;
                    rc = RTAsn1BitString_InitWithData(&Flags, &bFlags, 0, pAllocator);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTCrSpcPeImageData_SetFlags(SpcIndData.Data.uValue.pPeImage, &Flags, pAllocator);
                        RTAsn1BitString_Delete(&Flags);
                        if (RT_FAILURE(rc))
                            rcExit = RTMsgErrorExitFailure("RTCrSpcPeImageData_SetFlags failed: %Rrc", rc);
                    }
                    else
                        rcExit = RTMsgErrorExitFailure("RTAsn1BitString_InitWithData failed: %Rrc", rc);
                }

                /*
                 * Add the hashes.
                 */
                rcExit = SignToolPkcs7_SpcAddImageHash(pThis, &SpcIndData, enmSigType);
                if (rcExit == RTEXITCODE_SUCCESS)
                {
                    if (fHashPages)
                        rcExit = SignToolPkcs7_SpcAddImagePageHashes(pThis, &SpcIndData, enmSigType);
                    else
                        rcExit = SignToolPkcs7_SpcCompleteWithoutPageHashes(&SpcIndData);

                    /*
                     * Encode and sign the SPC data, timestamp it, and line it up for adding to the executable.
                     */
                    if (rcExit == RTEXITCODE_SUCCESS)
                        rcExit = SignToolPkcs7_SignSpcIndData(pThis, &SpcIndData, cVerbosity,
                                                              enmSigType, fReplaceExisting, pSigningCertKey, hAddCerts,
                                                              fTimestampTypeOld, SigningTime, pTimestampCertKey);
                }
            }
            else
                rcExit = RTMsgErrorExitFailure("RTCrPkcs7SignerInfos_Init failed: %Rrc", rc);
        }
        else
            rcExit = RTMsgErrorExitFailure("RTAsn1MemAllocZ failed for RTCRSPCPEIMAGEDATA: %Rrc", rc);
    }
    else
        rcExit = RTMsgErrorExitFailure("RTAsn1ObjId_SetWithString/SpcPeImageData failed: %Rrc", rc);

    RTCrSpcIndirectDataContent_Delete(&SpcIndData);
    return rcExit;
}

#endif /* !IPRT_IN_BUILD_TOOL */


/*********************************************************************************************************************************
*   The 'extract-exe-signer-cert' command.                                                                                       *
*********************************************************************************************************************************/

static RTEXITCODE HelpExtractExeSignerCert(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "extract-exe-signer-cert [--ber|--cer|--der] [--signature-index|-i <num>] [--exe|-e] <exe> [--output|-o] <outfile.cer>\n");
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleExtractExeSignerCert(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--ber",              'b', RTGETOPT_REQ_NOTHING },
        { "--cer",              'c', RTGETOPT_REQ_NOTHING },
        { "--der",              'd', RTGETOPT_REQ_NOTHING },
        { "--exe",              'e', RTGETOPT_REQ_STRING  },
        { "--output",           'o', RTGETOPT_REQ_STRING  },
        { "--signature-index",  'i', RTGETOPT_REQ_UINT32  },
    };

    const char *pszExe = NULL;
    const char *pszOut = NULL;
    RTLDRARCH   enmLdrArch   = RTLDRARCH_WHATEVER;
    unsigned    cVerbosity   = 0;
    uint32_t    fCursorFlags = RTASN1CURSOR_FLAGS_DER;
    uint32_t    iSignature   = 0;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'e':   pszExe = ValueUnion.psz; break;
            case 'o':   pszOut = ValueUnion.psz; break;
            case 'b':   fCursorFlags = 0; break;
            case 'c':   fCursorFlags = RTASN1CURSOR_FLAGS_CER; break;
            case 'd':   fCursorFlags = RTASN1CURSOR_FLAGS_DER; break;
            case 'i':   iSignature = ValueUnion.u32; break;
            case 'V':   return HandleVersion(cArgs, papszArgs);
            case 'h':   return HelpExtractExeSignerCert(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                if (!pszExe)
                    pszExe = ValueUnion.psz;
                else if (!pszOut)
                    pszOut = ValueUnion.psz;
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many file arguments: %s", ValueUnion.psz);
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszExe)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");
    if (!pszOut)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No output file given.");
    if (RTPathExists(pszOut))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The output file '%s' exists.", pszOut);

    /*
     * Do it.
     */
    /* Read & decode the PKCS#7 signature. */
    SIGNTOOLPKCS7EXE This;
    RTEXITCODE rcExit = SignToolPkcs7Exe_InitFromFile(&This, pszExe, cVerbosity, enmLdrArch);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /* Find the signing certificate (ASSUMING that the certificate used is shipped in the set of certificates). */
        PRTCRPKCS7SIGNEDDATA  pSignedData;
        PCRTCRPKCS7SIGNERINFO pSignerInfo = SignToolPkcs7_FindNestedSignatureByIndex(&This, iSignature, &pSignedData);
        rcExit = RTEXITCODE_FAILURE;
        if (pSignerInfo)
        {
            PCRTCRPKCS7ISSUERANDSERIALNUMBER pISN = &pSignedData->SignerInfos.papItems[0]->IssuerAndSerialNumber;
            PCRTCRX509CERTIFICATE pCert;
            pCert = RTCrPkcs7SetOfCerts_FindX509ByIssuerAndSerialNumber(&pSignedData->Certificates,
                                                                        &pISN->Name, &pISN->SerialNumber);
            if (pCert)
            {
                /*
                 * Write it out.
                 */
                RTFILE hFile;
                rc = RTFileOpen(&hFile, pszOut, RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE);
                if (RT_SUCCESS(rc))
                {
                    uint32_t cbCert = pCert->SeqCore.Asn1Core.cbHdr + pCert->SeqCore.Asn1Core.cb;
                    rc = RTFileWrite(hFile, pCert->SeqCore.Asn1Core.uData.pu8 - pCert->SeqCore.Asn1Core.cbHdr,
                                     cbCert, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTFileClose(hFile);
                        if (RT_SUCCESS(rc))
                        {
                            hFile  = NIL_RTFILE;
                            rcExit = RTEXITCODE_SUCCESS;
                            RTMsgInfo("Successfully wrote %u bytes to '%s'", cbCert, pszOut);
                        }
                        else
                            RTMsgError("RTFileClose failed: %Rrc", rc);
                    }
                    else
                        RTMsgError("RTFileWrite failed: %Rrc", rc);
                    RTFileClose(hFile);
                }
                else
                    RTMsgError("Error opening '%s' for writing: %Rrc", pszOut, rc);
            }
            else
                RTMsgError("Certificate not found.");
        }
        else
            RTMsgError("Could not locate signature #%u!", iSignature);

        /* Delete the signature data. */
        SignToolPkcs7Exe_Delete(&This);
    }
    return rcExit;
}


/*********************************************************************************************************************************
*   The 'add-nested-exe-signature' command.                                                                                      *
*********************************************************************************************************************************/

static RTEXITCODE HelpAddNestedExeSignature(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "add-nested-exe-signature [-v|--verbose] [-d|--debug] [-p|--prepend] <destination-exe> <source-exe>\n");
    if (enmLevel == RTSIGNTOOLHELP_FULL)
        RTStrmWrappedPrintf(pStrm, 0,
                            "\n"
                            "The --debug option allows the source-exe to be omitted in order to test the "
                            "encoding and PE file modification.\n"
                            "\n"
                            "The --prepend option puts the nested signature first rather than appending it "
                            "to the end of of the nested signature set.  Windows reads nested signatures in "
                            "reverse order, so --prepend will logically putting it last.\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleAddNestedExeSignature(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--prepend", 'p', RTGETOPT_REQ_NOTHING },
        { "--verbose", 'v', RTGETOPT_REQ_NOTHING },
        { "--debug",   'd', RTGETOPT_REQ_NOTHING },
    };

    const char *pszDst     = NULL;
    const char *pszSrc     = NULL;
    unsigned    cVerbosity = 0;
    bool        fDebug     = false;
    bool        fPrepend   = false;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'v':   cVerbosity++; break;
            case 'd':   fDebug = pszSrc == NULL; break;
            case 'p':   fPrepend = true; break;
            case 'V':   return HandleVersion(cArgs, papszArgs);
            case 'h':   return HelpAddNestedExeSignature(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                if (!pszDst)
                    pszDst = ValueUnion.psz;
                else if (!pszSrc)
                {
                    pszSrc = ValueUnion.psz;
                    fDebug = false;
                }
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many file arguments: %s", ValueUnion.psz);
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszDst)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No destination executable given.");
    if (!pszSrc && !fDebug)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No source executable file given.");

    /*
     * Do it.
     */
    /* Read & decode the source PKCS#7 signature. */
    SIGNTOOLPKCS7EXE Src;
    RTEXITCODE rcExit = pszSrc ? SignToolPkcs7Exe_InitFromFile(&Src, pszSrc, cVerbosity) : RTEXITCODE_SUCCESS;
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /* Ditto for the destination PKCS#7 signature. */
        SIGNTOOLPKCS7EXE Dst;
        rcExit = SignToolPkcs7Exe_InitFromFile(&Dst, pszDst, cVerbosity);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /* Do the signature manipulation. */
            if (pszSrc)
                rcExit = SignToolPkcs7_AddNestedSignature(&Dst, &Src, cVerbosity, fPrepend);
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7_Encode(&Dst, cVerbosity);

            /* Update the destination executable file. */
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7Exe_WriteSignatureToFile(&Dst, cVerbosity);

            SignToolPkcs7Exe_Delete(&Dst);
        }
        if (pszSrc)
            SignToolPkcs7Exe_Delete(&Src);
    }

    return rcExit;
}


/*********************************************************************************************************************************
*   The 'add-nested-cat-signature' command.                                                                                      *
*********************************************************************************************************************************/

static RTEXITCODE HelpAddNestedCatSignature(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "add-nested-cat-signature [-v|--verbose] [-d|--debug] [-p|--prepend] <destination-cat> <source-cat>\n");
    if (enmLevel == RTSIGNTOOLHELP_FULL)
        RTStrmWrappedPrintf(pStrm, 0,
                            "\n"
                            "The --debug option allows the source-cat to be omitted in order to test the "
                            "ASN.1 re-encoding of the destination catalog file.\n"
                            "\n"
                            "The --prepend option puts the nested signature first rather than appending it "
                            "to the end of of the nested signature set.  Windows reads nested signatures in "
                            "reverse order, so --prepend will logically putting it last.\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleAddNestedCatSignature(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--prepend", 'p', RTGETOPT_REQ_NOTHING },
        { "--verbose", 'v', RTGETOPT_REQ_NOTHING },
        { "--debug",   'd', RTGETOPT_REQ_NOTHING },
    };

    const char *pszDst     = NULL;
    const char *pszSrc     = NULL;
    unsigned    cVerbosity = 0;
    bool        fDebug     = false;
    bool        fPrepend   = false;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'v':   cVerbosity++; break;
            case 'd':   fDebug = pszSrc == NULL; break;
            case 'p':   fPrepend = true;  break;
            case 'V':   return HandleVersion(cArgs, papszArgs);
            case 'h':   return HelpAddNestedCatSignature(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                if (!pszDst)
                    pszDst = ValueUnion.psz;
                else if (!pszSrc)
                {
                    pszSrc = ValueUnion.psz;
                    fDebug = false;
                }
                else
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Too many file arguments: %s", ValueUnion.psz);
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszDst)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No destination catalog file given.");
    if (!pszSrc && !fDebug)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No source catalog file given.");

    /*
     * Do it.
     */
    /* Read & decode the source PKCS#7 signature. */
    SIGNTOOLPKCS7 Src;
    RTEXITCODE rcExit = pszSrc ? SignToolPkcs7_InitFromFile(&Src, pszSrc, cVerbosity) : RTEXITCODE_SUCCESS;
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /* Ditto for the destination PKCS#7 signature. */
        SIGNTOOLPKCS7EXE Dst;
        rcExit = SignToolPkcs7_InitFromFile(&Dst, pszDst, cVerbosity);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /* Do the signature manipulation. */
            if (pszSrc)
                rcExit = SignToolPkcs7_AddNestedSignature(&Dst, &Src, cVerbosity, fPrepend);
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7_Encode(&Dst, cVerbosity);

            /* Update the destination executable file. */
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = SignToolPkcs7_WriteSignatureToFile(&Dst, pszDst, cVerbosity);

            SignToolPkcs7_Delete(&Dst);
        }
        if (pszSrc)
            SignToolPkcs7_Delete(&Src);
    }

    return rcExit;
}


/*********************************************************************************************************************************
*   Option handlers shared by 'sign-exe', 'sign-cat', 'add-timestamp-exe-signature' and others.                                  *
*********************************************************************************************************************************/
#ifndef IPRT_IN_BUILD_TOOL

static RTEXITCODE HandleOptCertFile(SIGNTOOLKEYPAIR *pKeyPair, const char *pszFile)
{
    if (pKeyPair->pCertificate == &pKeyPair->Cert)
        RTCrX509Certificate_Delete(&pKeyPair->Cert);
    pKeyPair->pCertificate = NULL;

    RTERRINFOSTATIC ErrInfo;
    int rc = RTCrX509Certificate_ReadFromFile(&pKeyPair->Cert, pszFile, 0, &g_RTAsn1DefaultAllocator,
                                              RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Error reading certificate from '%s': %Rrc%#RTeim", pszFile, rc, &ErrInfo.Core);
    pKeyPair->pCertificate = &pKeyPair->Cert;
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleOptKeyFile(SIGNTOOLKEYPAIR *pKeyPair, const char *pszFile)
{
    RTCrKeyRelease(pKeyPair->hPrivateKey);

    RTERRINFOSTATIC ErrInfo;
    int rc = RTCrKeyCreateFromFile(&pKeyPair->hPrivateKey, 0 /*fFlags*/, pszFile,
                                   NULL /*pszPassword*/, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
        return RTEXITCODE_SUCCESS;

    pKeyPair->hPrivateKey = NIL_RTCRKEY;
    return RTMsgErrorExitFailure("Error reading private key from '%s': %Rrc%#RTeim", pszFile, rc, &ErrInfo.Core);
}

static RTEXITCODE HandleOptAddCert(PRTCRSTORE phStore, const char *pszFile)
{
    if (*phStore == NIL_RTCRSTORE)
    {
        int rc = RTCrStoreCreateInMem(phStore, 2);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTCrStoreCreateInMem(,2) failed: %Rrc", rc);
    }
    RTERRINFOSTATIC ErrInfo;
    int rc = RTCrStoreCertAddFromFile(*phStore, RTCRCERTCTX_F_ADD_IF_NOT_FOUND, pszFile, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Error reading certificate from '%s': %Rrc%#RTeim", pszFile, rc, &ErrInfo.Core);
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleOptSignatureType(RTDIGESTTYPE *penmSigType, const char *pszType)
{
    if (   RTStrICmpAscii(pszType, "sha1") == 0
        || RTStrICmpAscii(pszType, "sha-1") == 0)
        *penmSigType = RTDIGESTTYPE_SHA1;
    else if (   RTStrICmpAscii(pszType, "sha256") == 0
             || RTStrICmpAscii(pszType, "sha-256") == 0)
        *penmSigType = RTDIGESTTYPE_SHA256;
    else
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown signature type: %s (expected sha1 or sha256)", pszType);
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleOptTimestampType(bool *pfOldType, const char *pszType)
{
    if (strcmp(pszType, "old") == 0)
        *pfOldType = true;
    else if (strcmp(pszType, "new") == 0)
        *pfOldType = false;
    else
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown timestamp type: %s", pszType);
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleOptTimestampOverride(PRTTIMESPEC pSigningTime, const char *pszPartialTs)
{
    /*
     * First try use it as-is.
     */
    if (RTTimeSpecFromString(pSigningTime, pszPartialTs) != NULL)
        return RTEXITCODE_SUCCESS;

    /* Check the input against a pattern, making sure we've got something that
       makes sense before trying to merge. */
    size_t const cchPartialTs = strlen(pszPartialTs);
    static char s_szPattern[] = "0000-00-00T00:00:";
    if (cchPartialTs > sizeof(s_szPattern) - 1) /* It is not a partial timestamp if we've got the seconds component. */
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp: %s", pszPartialTs);

    for (size_t off = 0; off < cchPartialTs; off++)
        switch (s_szPattern[off])
        {
            case '0':
                if (!RT_C_IS_DIGIT(pszPartialTs[off]))
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp, expected digit at position %u: %s",
                                          off + 1, pszPartialTs);
                break;
            case '-':
            case ':':
                if (pszPartialTs[off] != s_szPattern[off])
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp, expected '%c' at position %u: %s",
                                          s_szPattern[off], off + 1, pszPartialTs);
                break;
            case 'T':
                if (   pszPartialTs[off] != 'T'
                    && pszPartialTs[off] != 't'
                    && pszPartialTs[off] != ' ')
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp, expected 'T' or space at position %u: %s",
                                          off + 1, pszPartialTs);
                break;
            default:
                return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Internal error");
        }

    if (RT_C_IS_DIGIT(s_szPattern[cchPartialTs]) && RT_C_IS_DIGIT(s_szPattern[cchPartialTs - 1]))
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Incomplete timstamp component: %s", pszPartialTs);

    /*
     * Take the current time and merge in the components from pszPartialTs.
     */
    char        szSigningTime[RTTIME_STR_LEN];
    RTTIMESPEC  Now;
    RTTimeSpecToString(RTTimeNow(&Now), szSigningTime, sizeof(szSigningTime));
    memcpy(szSigningTime, pszPartialTs, cchPartialTs);
    szSigningTime[4+1+2+1+2] = 'T';

    /* Fix 29th for non-leap override: */
    if (memcmp(&szSigningTime[5], RT_STR_TUPLE("02-29")) == 0)
    {
        if (!RTTimeIsLeapYear(RTStrToUInt32(szSigningTime)))
            szSigningTime[9] = '8';
    }
    if (RTTimeSpecFromString(pSigningTime, szSigningTime) == NULL)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid timestamp: %s (%s)", pszPartialTs, szSigningTime);

    return RTEXITCODE_SUCCESS;
}

#endif /* !IPRT_IN_BUILD_TOOL */


/*********************************************************************************************************************************
*   The 'add-timestamp-exe-signature' command.                                                                                   *
*********************************************************************************************************************************/
#ifndef IPRT_IN_BUILD_TOOL

static RTEXITCODE HelpAddTimestampExeSignature(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);

    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "add-timestamp-exe-signature [-v|--verbose] [--signature-index|-i <num>] "
                        "[--timestamp-cert-file <file>] "
                        "[--timestamp-key-file <file>] "
                        "[--timestamp-type old|new] "
                        "[--timestamp-date <fake-isots>] "
                        "[--timestamp-year <fake-year>] "
                        "[--replace-existing|-r] "
                        "<exe>\n");
    if (enmLevel == RTSIGNTOOLHELP_FULL)
        RTStrmWrappedPrintf(pStrm, 0,
                            "This is mainly to test timestamp code.\n"
                            "\n"
                            "The --timestamp-override option can take a partial or full ISO timestamp.  It is merged "
                            "with the current time if partial.\n"
                            "\n");
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleAddTimestampExeSignature(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--signature-index",      'i',                        RTGETOPT_REQ_UINT32 },
        { "--timestamp-cert-file",  OPT_TIMESTAMP_CERT_FILE,    RTGETOPT_REQ_STRING },
        { "--timestamp-key-file",   OPT_TIMESTAMP_KEY_FILE,     RTGETOPT_REQ_STRING },
        { "--timestamp-type",       OPT_TIMESTAMP_TYPE,         RTGETOPT_REQ_STRING },
        { "--timestamp-override",   OPT_TIMESTAMP_OVERRIDE,     RTGETOPT_REQ_STRING },
        { "--replace-existing",     'r',                        RTGETOPT_REQ_NOTHING },
        { "--verbose",              'v',                        RTGETOPT_REQ_NOTHING },
    };

    unsigned                cVerbosity              = 0;
    unsigned                iSignature              = 0;
    bool                    fReplaceExisting        = false;
    bool                    fTimestampTypeOld       = true;
    SIGNTOOLKEYPAIR         TimestampCertKey;
    RTTIMESPEC              SigningTime;
    RTTimeNow(&SigningTime);

    RTGETOPTSTATE   GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);

    RTEXITCODE      rcExit = RTEXITCODE_SUCCESS;
    RTGETOPTUNION   ValueUnion;
    int             ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        RTEXITCODE rcExit2 = RTEXITCODE_SUCCESS;
        switch (ch)
        {
            case 'i':                       iSignature = ValueUnion.u32; break;
            case OPT_TIMESTAMP_CERT_FILE:   rcExit2 = HandleOptCertFile(&TimestampCertKey, ValueUnion.psz); break;
            case OPT_TIMESTAMP_KEY_FILE:    rcExit2 = HandleOptKeyFile(&TimestampCertKey, ValueUnion.psz); break;
            case OPT_TIMESTAMP_TYPE:        rcExit2 = HandleOptTimestampType(&fTimestampTypeOld, ValueUnion.psz); break;
            case OPT_TIMESTAMP_OVERRIDE:    rcExit2 = HandleOptTimestampOverride(&SigningTime, ValueUnion.psz); break;
            case 'r':                       fReplaceExisting = true; break;
            case 'v':                       cVerbosity++; break;
            case 'V':                       return HandleVersion(cArgs, papszArgs);
            case 'h':                       return HelpAddTimestampExeSignature(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                /* check that we've got all the info we need: */
                if (TimestampCertKey.isComplete())
                {
                    /* Do the work: */
                    SIGNTOOLPKCS7EXE Exe;
                    rcExit2 = SignToolPkcs7Exe_InitFromFile(&Exe, ValueUnion.psz, cVerbosity);
                    if (rcExit2 == RTEXITCODE_SUCCESS)
                    {
                        rcExit2 = SignToolPkcs7_AddTimestampSignature(&Exe, cVerbosity, iSignature, fReplaceExisting,
                                                                      fTimestampTypeOld, SigningTime, &TimestampCertKey);
                        if (rcExit2 == RTEXITCODE_SUCCESS)
                            rcExit2 = SignToolPkcs7_Encode(&Exe, cVerbosity);
                        if (rcExit2 == RTEXITCODE_SUCCESS)
                            rcExit2 = SignToolPkcs7Exe_WriteSignatureToFile(&Exe, cVerbosity);
                        SignToolPkcs7Exe_Delete(&Exe);
                    }
                    if (rcExit2 != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                    rcExit2 = RTEXITCODE_SUCCESS;
                }
                else
                {
                    if (!TimestampCertKey.pCertificate)
                        RTMsgError("No timestamp certificate was specified");
                    if (TimestampCertKey.hPrivateKey == NIL_RTCRKEY)
                        RTMsgError("No timestamp private key was specified");
                    rcExit2 = RTEXITCODE_SYNTAX;
                }
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }

        if (rcExit2 != RTEXITCODE_SUCCESS)
        {
            rcExit = rcExit2;
            break;
        }
    }
    return rcExit;
}

#endif /*!IPRT_IN_BUILD_TOOL */


/*********************************************************************************************************************************
*   The 'sign-exe' command.                                                                                   *
*********************************************************************************************************************************/
#ifndef IPRT_IN_BUILD_TOOL

static RTEXITCODE HelpSignExe(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);

    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "sign-exe [-v|--verbose] "
                        "[--type sha1|sha256] "
                        "[--hash-pages] "
                        "[--no-hash-pages] "
                        "[--append] "
                        "[--cert-file <file>] "
                        "[--cert-key <file>] "
                        "[--add-cert <file>] "
                        "[--timestamp-cert-file <file>] "
                        "[--timestamp-key-file <file>] "
                        "[--timestamp-type old|new] "
                        "[--timestamp-date <fake-isots>] "
                        "[--timestamp-year <fake-year>] "
                        "[--replace-existing|-r] "
                        "<exe>\n");
    if (enmLevel == RTSIGNTOOLHELP_FULL)
        RTStrmWrappedPrintf(pStrm, 0,
                            "Create a new code signature for an executable.\n"
                            "\n"
                            "The --timestamp-override option can take a partial or full ISO timestamp.  It is merged "
                            "with the current time if partial.\n"
                            "\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleSignExe(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--append",               'a',                        RTGETOPT_REQ_NOTHING },
        { "/as",                    'a',                        RTGETOPT_REQ_NOTHING },
        { "--type",                 't',                        RTGETOPT_REQ_STRING },
        { "/fd",                    't',                        RTGETOPT_REQ_STRING },
        { "--hash-pages",           OPT_HASH_PAGES,             RTGETOPT_REQ_NOTHING },
        { "/ph",                    OPT_HASH_PAGES,             RTGETOPT_REQ_NOTHING },
        { "--no-hash-pages",        OPT_NO_HASH_PAGES,          RTGETOPT_REQ_NOTHING },
        { "/nph",                   OPT_NO_HASH_PAGES,          RTGETOPT_REQ_NOTHING },
        { "--add-cert",             OPT_ADD_CERT,               RTGETOPT_REQ_STRING },
        { "/ac",                    OPT_ADD_CERT,               RTGETOPT_REQ_STRING },
        { "--cert-file",            OPT_CERT_FILE,              RTGETOPT_REQ_STRING },
        { "--key-file",             OPT_KEY_FILE,               RTGETOPT_REQ_STRING },
        { "--timestamp-cert-file",  OPT_TIMESTAMP_CERT_FILE,    RTGETOPT_REQ_STRING },
        { "--timestamp-key-file",   OPT_TIMESTAMP_KEY_FILE,     RTGETOPT_REQ_STRING },
        { "--timestamp-type",       OPT_TIMESTAMP_TYPE,         RTGETOPT_REQ_STRING },
        { "--timestamp-override",   OPT_TIMESTAMP_OVERRIDE,     RTGETOPT_REQ_STRING },
        { "--verbose",              'v',                        RTGETOPT_REQ_NOTHING },
        { "/v",                     'v',                        RTGETOPT_REQ_NOTHING },
        { "/debug",                 'v',                        RTGETOPT_REQ_NOTHING },
    };

    unsigned                cVerbosity              = 0;
    RTDIGESTTYPE            enmSigType              = RTDIGESTTYPE_SHA1;
    bool                    fReplaceExisting        = true;
    bool                    fHashPages              = false;
    SIGNTOOLKEYPAIR         SigningCertKey;
    RTCRSTORE               hAddCerts               = NIL_RTCRSTORE; /* leaked if returning directly (--help, --version) */
    bool                    fTimestampTypeOld       = true;
    SIGNTOOLKEYPAIR         TimestampCertKey;
    RTTIMESPEC              SigningTime;
    RTTimeNow(&SigningTime);

    RTGETOPTSTATE   GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);

    RTEXITCODE      rcExit = RTEXITCODE_SUCCESS;
    RTGETOPTUNION   ValueUnion;
    int             ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        RTEXITCODE rcExit2 = RTEXITCODE_SUCCESS;
        switch (ch)
        {
            case 't':                       rcExit2 = HandleOptSignatureType(&enmSigType, ValueUnion.psz); break;
            case 'a':                       fReplaceExisting = false; break;
            case OPT_HASH_PAGES:            fHashPages = true; break;
            case OPT_NO_HASH_PAGES:         fHashPages = false; break;
            case OPT_CERT_FILE:             rcExit2 = HandleOptCertFile(&SigningCertKey, ValueUnion.psz); break;
            case OPT_KEY_FILE:              rcExit2 = HandleOptKeyFile(&SigningCertKey, ValueUnion.psz); break;
            case OPT_ADD_CERT:              rcExit2 = HandleOptAddCert(&hAddCerts, ValueUnion.psz); break;
            case OPT_TIMESTAMP_CERT_FILE:   rcExit2 = HandleOptCertFile(&TimestampCertKey, ValueUnion.psz); break;
            case OPT_TIMESTAMP_KEY_FILE:    rcExit2 = HandleOptKeyFile(&TimestampCertKey, ValueUnion.psz); break;
            case OPT_TIMESTAMP_TYPE:        rcExit2 = HandleOptTimestampType(&fTimestampTypeOld, ValueUnion.psz); break;
            case OPT_TIMESTAMP_OVERRIDE:    rcExit2 = HandleOptTimestampOverride(&SigningTime, ValueUnion.psz); break;
            case 'v':                       cVerbosity++; break;
            case 'V':                       return HandleVersion(cArgs, papszArgs);
            case 'h':                       return HelpSignExe(g_pStdOut, RTSIGNTOOLHELP_FULL);

            case VINF_GETOPT_NOT_OPTION:
                /* check that we've got all the info we need: */
                if (   SigningCertKey.isComplete()
                    && (TimestampCertKey.isNull() || TimestampCertKey.isComplete()))
                {
                    /* Do the work: */
                    SIGNTOOLPKCS7EXE Exe;
                    /** @todo will fail if not already signed. */
                    rcExit2 = SignToolPkcs7Exe_InitFromFile(&Exe, ValueUnion.psz, cVerbosity);
                    if (rcExit2 == RTEXITCODE_SUCCESS)
                    {
                        rcExit2 = SignToolPkcs7_AddOrReplaceSignature(&Exe, cVerbosity, enmSigType, fReplaceExisting, fHashPages,
                                                                      &SigningCertKey, hAddCerts,
                                                                      fTimestampTypeOld, SigningTime, &TimestampCertKey);
                        if (rcExit2 == RTEXITCODE_SUCCESS)
                            rcExit2 = SignToolPkcs7_Encode(&Exe, cVerbosity);
                        if (rcExit2 == RTEXITCODE_SUCCESS)
                            rcExit2 = SignToolPkcs7Exe_WriteSignatureToFile(&Exe, cVerbosity);
                        SignToolPkcs7Exe_Delete(&Exe);
                    }
                    if (rcExit2 != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                    rcExit2 = RTEXITCODE_SUCCESS;
                }
                else
                {
                    if (!TimestampCertKey.pCertificate)
                        RTMsgError("No signing certificate was specified");
                    if (TimestampCertKey.hPrivateKey == NIL_RTCRKEY)
                        RTMsgError("No signing private key was specified");

                    if (!TimestampCertKey.pCertificate && !TimestampCertKey.isNull())
                        RTMsgError("No timestamp certificate was specified");
                    if (TimestampCertKey.hPrivateKey == NIL_RTCRKEY && !TimestampCertKey.isNull())
                        RTMsgError("No timestamp private key was specified");
                    rcExit2 = RTEXITCODE_SYNTAX;
                }
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
        if (rcExit2 != RTEXITCODE_SUCCESS)
        {
            rcExit = rcExit2;
            break;
        }
    }

    if (hAddCerts != NIL_RTCRSTORE)
        RTCrStoreRelease(hAddCerts);
    return rcExit;
}

#endif /*!IPRT_IN_BUILD_TOOL */


/*********************************************************************************************************************************
*   The 'verify-exe' command.                                                                                                    *
*********************************************************************************************************************************/
#ifndef IPRT_IN_BUILD_TOOL

static RTEXITCODE HelpVerifyExe(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "verify-exe [--verbose|--quiet] [--kernel] [--root <root-cert.der>] [--additional <supp-cert.der>] "
                        "[--type <win|osx>] <exe1> [exe2 [..]]\n");
    return RTEXITCODE_SUCCESS;
}

typedef struct VERIFYEXESTATE
{
    RTCRSTORE   hRootStore;
    RTCRSTORE   hKernelRootStore;
    RTCRSTORE   hAdditionalStore;
    bool        fKernel;
    int         cVerbose;
    enum { kSignType_Windows, kSignType_OSX } enmSignType;
    RTLDRARCH   enmLdrArch;
    uint32_t    cBad;
    uint32_t    cOkay;
    const char *pszFilename;
} VERIFYEXESTATE;

# ifdef VBOX
/** Certificate store load set.
 * Declared outside HandleVerifyExe because of braindead gcc visibility crap. */
struct STSTORESET
{
    RTCRSTORE       hStore;
    PCSUPTAENTRY    paTAs;
    unsigned        cTAs;
};
# endif

/**
 * @callback_method_impl{FNRTCRPKCS7VERIFYCERTCALLBACK,
 * Standard code signing.  Use this for Microsoft SPC.}
 */
static DECLCALLBACK(int) VerifyExecCertVerifyCallback(PCRTCRX509CERTIFICATE pCert, RTCRX509CERTPATHS hCertPaths, uint32_t fFlags,
                                                      void *pvUser, PRTERRINFO pErrInfo)
{
    VERIFYEXESTATE *pState = (VERIFYEXESTATE *)pvUser;
    uint32_t        cPaths = RTCrX509CertPathsGetPathCount(hCertPaths);

    /*
     * Dump all the paths.
     */
    if (pState->cVerbose > 0)
    {
        for (uint32_t iPath = 0; iPath < cPaths; iPath++)
        {
            RTPrintf("---\n");
            RTCrX509CertPathsDumpOne(hCertPaths, iPath, pState->cVerbose, RTStrmDumpPrintfV, g_pStdOut);
            *pErrInfo->pszMsg = '\0';
        }
        RTPrintf("---\n");
    }

    /*
     * Test signing certificates normally doesn't have all the necessary
     * features required below.  So, treat them as special cases.
     */
    if (   hCertPaths == NIL_RTCRX509CERTPATHS
        && RTCrX509Name_Compare(&pCert->TbsCertificate.Issuer, &pCert->TbsCertificate.Subject) == 0)
    {
        RTMsgInfo("Test signed.\n");
        return VINF_SUCCESS;
    }

    if (hCertPaths == NIL_RTCRX509CERTPATHS)
        RTMsgInfo("Signed by trusted certificate.\n");

    /*
     * Standard code signing capabilites required.
     */
    int rc = RTCrPkcs7VerifyCertCallbackCodeSigning(pCert, hCertPaths, fFlags, NULL, pErrInfo);
    if (   RT_SUCCESS(rc)
        && (fFlags & RTCRPKCS7VCC_F_SIGNED_DATA))
    {
        /*
         * If windows kernel signing, a valid certificate path must be anchored
         * by the microsoft kernel signing root certificate.  The only
         * alternative is test signing.
         */
        if (   pState->fKernel
            && hCertPaths != NIL_RTCRX509CERTPATHS
            && pState->enmSignType == VERIFYEXESTATE::kSignType_Windows)
        {
            uint32_t cFound = 0;
            uint32_t cValid = 0;
            for (uint32_t iPath = 0; iPath < cPaths; iPath++)
            {
                bool                            fTrusted;
                PCRTCRX509NAME                  pSubject;
                PCRTCRX509SUBJECTPUBLICKEYINFO  pPublicKeyInfo;
                int                             rcVerify;
                rc = RTCrX509CertPathsQueryPathInfo(hCertPaths, iPath, &fTrusted, NULL /*pcNodes*/, &pSubject, &pPublicKeyInfo,
                                                    NULL, NULL /*pCertCtx*/, &rcVerify);
                AssertRCBreak(rc);

                if (RT_SUCCESS(rcVerify))
                {
                    Assert(fTrusted);
                    cValid++;

                    /* Search the kernel signing root store for a matching anchor. */
                    RTCRSTORECERTSEARCH Search;
                    rc = RTCrStoreCertFindBySubjectOrAltSubjectByRfc5280(pState->hKernelRootStore, pSubject, &Search);
                    AssertRCBreak(rc);
                    PCRTCRCERTCTX pCertCtx;
                    while ((pCertCtx = RTCrStoreCertSearchNext(pState->hKernelRootStore, &Search)) != NULL)
                    {
                        PCRTCRX509SUBJECTPUBLICKEYINFO pPubKeyInfo;
                        if (pCertCtx->pCert)
                            pPubKeyInfo = &pCertCtx->pCert->TbsCertificate.SubjectPublicKeyInfo;
                        else if (pCertCtx->pTaInfo)
                            pPubKeyInfo = &pCertCtx->pTaInfo->PubKey;
                        else
                            pPubKeyInfo = NULL;
                        if (RTCrX509SubjectPublicKeyInfo_Compare(pPubKeyInfo, pPublicKeyInfo) == 0)
                            cFound++;
                        RTCrCertCtxRelease(pCertCtx);
                    }

                    int rc2 = RTCrStoreCertSearchDestroy(pState->hKernelRootStore, &Search); AssertRC(rc2);
                }
            }
            if (RT_SUCCESS(rc) && cFound == 0)
                rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE, "Not valid kernel code signature.");
            if (RT_SUCCESS(rc) && cValid != 2)
                RTMsgWarning("%u valid paths, expected 2", cValid);
        }
        /*
         * For Mac OS X signing, check for special developer ID attributes.
         */
        else if (pState->enmSignType == VERIFYEXESTATE::kSignType_OSX)
        {
            uint32_t cDevIdApp    = 0;
            uint32_t cDevIdKext   = 0;
            uint32_t cDevIdMacDev = 0;
            for (uint32_t i = 0; i < pCert->TbsCertificate.T3.Extensions.cItems; i++)
            {
                PCRTCRX509EXTENSION pExt = pCert->TbsCertificate.T3.Extensions.papItems[i];
                if (RTAsn1ObjId_CompareWithString(&pExt->ExtnId, RTCR_APPLE_CS_DEVID_APPLICATION_OID) == 0)
                {
                    cDevIdApp++;
                    if (!pExt->Critical.fValue)
                        rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                           "Dev ID Application certificate extension is not flagged critical");
                }
                else if (RTAsn1ObjId_CompareWithString(&pExt->ExtnId, RTCR_APPLE_CS_DEVID_KEXT_OID) == 0)
                {
                    cDevIdKext++;
                    if (!pExt->Critical.fValue)
                        rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                           "Dev ID kext certificate extension is not flagged critical");
                }
                else if (RTAsn1ObjId_CompareWithString(&pExt->ExtnId, RTCR_APPLE_CS_DEVID_MAC_SW_DEV_OID) == 0)
                {
                    cDevIdMacDev++;
                    if (!pExt->Critical.fValue)
                        rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                           "Dev ID Mac SW dev certificate extension is not flagged critical");
                }
            }
            if (cDevIdApp == 0)
            {
                if (cDevIdMacDev == 0)
                    rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                       "Certificate is missing the 'Dev ID Application' extension");
                else
                    RTMsgWarning("Mac SW dev certificate used to sign code.");
            }
            if (cDevIdKext == 0 && pState->fKernel)
            {
                if (cDevIdMacDev == 0)
                    rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                       "Certificate is missing the 'Dev ID kext' extension");
                else
                    RTMsgWarning("Mac SW dev certificate used to sign kernel code.");
            }
        }
    }

    return rc;
}

/** @callback_method_impl{FNRTLDRVALIDATESIGNEDDATA}  */
static DECLCALLBACK(int) VerifyExeCallback(RTLDRMOD hLdrMod, PCRTLDRSIGNATUREINFO pInfo, PRTERRINFO pErrInfo, void *pvUser)
{
    VERIFYEXESTATE *pState = (VERIFYEXESTATE *)pvUser;
    RT_NOREF_PV(hLdrMod);

    switch (pInfo->enmType)
    {
        case RTLDRSIGNATURETYPE_PKCS7_SIGNED_DATA:
        {
            PCRTCRPKCS7CONTENTINFO pContentInfo = (PCRTCRPKCS7CONTENTINFO)pInfo->pvSignature;

            /*
             * Dump the signed data if so requested and it's the first one, assuming that
             * additional signatures in contained wihtin the same ContentInfo structure.
             */
            if (pState->cVerbose && pInfo->iSignature == 0)
                RTAsn1Dump(&pContentInfo->SeqCore.Asn1Core, 0, 0, RTStrmDumpPrintfV, g_pStdOut);

            /*
             * We'll try different alternative timestamps here.
             */
            struct { RTTIMESPEC TimeSpec; const char *pszDesc; } aTimes[2];
            unsigned cTimes = 0;

            /* Linking timestamp: */
            uint64_t uLinkingTime = 0;
            int rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_TIMESTAMP_SECONDS, &uLinkingTime, sizeof(uLinkingTime));
            if (RT_SUCCESS(rc))
            {
                RTTimeSpecSetSeconds(&aTimes[0].TimeSpec, uLinkingTime);
                aTimes[0].pszDesc = "at link time";
                cTimes++;
            }
            else if (rc != VERR_NOT_FOUND)
                RTMsgError("RTLdrQueryProp/RTLDRPROP_TIMESTAMP_SECONDS failed on '%s': %Rrc\n", pState->pszFilename, rc);

            /* Now: */
            RTTimeNow(&aTimes[cTimes].TimeSpec);
            aTimes[cTimes].pszDesc = "now";
            cTimes++;

            /*
             * Do the actual verification.
             */
            for (unsigned iTime = 0; iTime < cTimes; iTime++)
            {
                if (pInfo->pvExternalData)
                    rc = RTCrPkcs7VerifySignedDataWithExternalData(pContentInfo,
                                                                   RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                                   | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_SIGNING_TIME_IF_PRESENT
                                                                   | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_MS_TIMESTAMP_IF_PRESENT
                                                                   | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS,
                                                                   pState->hAdditionalStore, pState->hRootStore,
                                                                   &aTimes[iTime].TimeSpec,
                                                                   VerifyExecCertVerifyCallback, pState,
                                                                   pInfo->pvExternalData, pInfo->cbExternalData, pErrInfo);
                else
                    rc = RTCrPkcs7VerifySignedData(pContentInfo,
                                                   RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                   | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_SIGNING_TIME_IF_PRESENT
                                                   | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_MS_TIMESTAMP_IF_PRESENT
                                                   | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS,
                                                   pState->hAdditionalStore, pState->hRootStore,
                                                   &aTimes[iTime].TimeSpec,
                                                   VerifyExecCertVerifyCallback, pState, pErrInfo);
                if (RT_SUCCESS(rc))
                {
                    Assert(rc == VINF_SUCCESS || rc == VINF_CR_DIGEST_DEPRECATED);
                    const char *pszNote = rc == VINF_CR_DIGEST_DEPRECATED ? " (deprecated digest)" : "";
                    if (pInfo->cSignatures == 1)
                        RTMsgInfo("'%s' is valid %s%s.\n", pState->pszFilename, aTimes[iTime].pszDesc, pszNote);
                    else
                        RTMsgInfo("'%s' signature #%u is valid %s%s.\n",
                                  pState->pszFilename, pInfo->iSignature + 1, aTimes[iTime].pszDesc, pszNote);
                    pState->cOkay++;
                    return VINF_SUCCESS;
                }
                if (rc != VERR_CR_X509_CPV_NOT_VALID_AT_TIME)
                {
                    if (pInfo->cSignatures == 1)
                        RTMsgError("%s: Failed to verify signature: %Rrc%#RTeim\n", pState->pszFilename, rc, pErrInfo);
                    else
                        RTMsgError("%s: Failed to verify signature #%u: %Rrc%#RTeim\n",
                                   pState->pszFilename, pInfo->iSignature + 1, rc, pErrInfo);
                    pState->cBad++;
                    return VINF_SUCCESS;
                }
            }

            if (pInfo->cSignatures == 1)
                RTMsgError("%s: Signature is not valid at present or link time.\n", pState->pszFilename);
            else
                RTMsgError("%s: Signature #%u is not valid at present or link time.\n",
                           pState->pszFilename, pInfo->iSignature + 1);
            pState->cBad++;
            return VINF_SUCCESS;
        }

        default:
            return RTErrInfoSetF(pErrInfo, VERR_NOT_SUPPORTED, "Unsupported signature type: %d", pInfo->enmType);
    }
}

/**
 * Worker for HandleVerifyExe.
 */
static RTEXITCODE HandleVerifyExeWorker(VERIFYEXESTATE *pState, const char *pszFilename, PRTERRINFOSTATIC pStaticErrInfo)
{
    /*
     * Open the executable image and verify it.
     */
    RTLDRMOD hLdrMod;
    int rc = RTLdrOpen(pszFilename, RTLDR_O_FOR_VALIDATION, pState->enmLdrArch, &hLdrMod);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error opening executable image '%s': %Rrc", pszFilename, rc);

    /* Reset the state. */
    pState->cBad        = 0;
    pState->cOkay       = 0;
    pState->pszFilename = pszFilename;

    rc = RTLdrVerifySignature(hLdrMod, VerifyExeCallback, pState, RTErrInfoInitStatic(pStaticErrInfo));
    if (RT_FAILURE(rc))
        RTMsgError("RTLdrVerifySignature failed on '%s': %Rrc - %s\n", pszFilename, rc, pStaticErrInfo->szMsg);

    int rc2 = RTLdrClose(hLdrMod);
    if (RT_FAILURE(rc2))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTLdrClose failed: %Rrc\n", rc2);
    if (RT_FAILURE(rc))
        return rc != VERR_LDRVI_NOT_SIGNED ? RTEXITCODE_FAILURE : RTEXITCODE_SKIPPED;

    return pState->cOkay > 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE HandleVerifyExe(int cArgs, char **papszArgs)
{
    RTERRINFOSTATIC StaticErrInfo;

    /* Note! This code does not try to clean up the crypto stores on failure.
             This is intentional as the code is only expected to be used in a
             one-command-per-process environment where we do exit() upon
             returning from this function. */

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--kernel",       'k', RTGETOPT_REQ_NOTHING },
        { "--root",         'r', RTGETOPT_REQ_STRING },
        { "--additional",   'a', RTGETOPT_REQ_STRING },
        { "--add",          'a', RTGETOPT_REQ_STRING },
        { "--type",         't', RTGETOPT_REQ_STRING },
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q', RTGETOPT_REQ_NOTHING },
    };

    VERIFYEXESTATE State =
    {
        NIL_RTCRSTORE, NIL_RTCRSTORE, NIL_RTCRSTORE, false, 0,
        VERIFYEXESTATE::kSignType_Windows, RTLDRARCH_WHATEVER,
        0, 0, NULL
    };
    int rc = RTCrStoreCreateInMem(&State.hRootStore, 0);
    if (RT_SUCCESS(rc))
        rc = RTCrStoreCreateInMem(&State.hKernelRootStore, 0);
    if (RT_SUCCESS(rc))
        rc = RTCrStoreCreateInMem(&State.hAdditionalStore, 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error creating in-memory certificate store: %Rrc", rc);

    RTGETOPTSTATE GetState;
    rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) && ch != VINF_GETOPT_NOT_OPTION)
    {
        switch (ch)
        {
            case 'r': case 'a':
                rc = RTCrStoreCertAddFromFile(ch == 'r' ? State.hRootStore : State.hAdditionalStore,
                                              RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR,
                                              ValueUnion.psz, RTErrInfoInitStatic(&StaticErrInfo));
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error loading certificate '%s': %Rrc - %s",
                                          ValueUnion.psz, rc, StaticErrInfo.szMsg);
                if (RTErrInfoIsSet(&StaticErrInfo.Core))
                    RTMsgWarning("Warnings loading certificate '%s': %s", ValueUnion.psz, StaticErrInfo.szMsg);
                break;

            case 't':
                if (!strcmp(ValueUnion.psz, "win") || !strcmp(ValueUnion.psz, "windows"))
                    State.enmSignType = VERIFYEXESTATE::kSignType_Windows;
                else if (!strcmp(ValueUnion.psz, "osx") || !strcmp(ValueUnion.psz, "apple"))
                    State.enmSignType = VERIFYEXESTATE::kSignType_OSX;
                else
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown signing type: '%s'", ValueUnion.psz);
                break;

            case 'k': State.fKernel = true; break;
            case 'v': State.cVerbose++; break;
            case 'q': State.cVerbose = 0; break;
            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpVerifyExe(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (ch != VINF_GETOPT_NOT_OPTION)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");

    /*
     * Populate the certificate stores according to the signing type.
     */
# ifdef VBOX
    unsigned          cSets = 0;
    struct STSTORESET aSets[6];
    switch (State.enmSignType)
    {
        case VERIFYEXESTATE::kSignType_Windows:
            aSets[cSets].hStore  = State.hRootStore;
            aSets[cSets].paTAs   = g_aSUPTimestampTAs;
            aSets[cSets].cTAs    = g_cSUPTimestampTAs;
            cSets++;
            aSets[cSets].hStore  = State.hRootStore;
            aSets[cSets].paTAs   = g_aSUPSpcRootTAs;
            aSets[cSets].cTAs    = g_cSUPSpcRootTAs;
            cSets++;
            aSets[cSets].hStore  = State.hRootStore;
            aSets[cSets].paTAs   = g_aSUPNtKernelRootTAs;
            aSets[cSets].cTAs    = g_cSUPNtKernelRootTAs;
            cSets++;
            aSets[cSets].hStore  = State.hKernelRootStore;
            aSets[cSets].paTAs   = g_aSUPNtKernelRootTAs;
            aSets[cSets].cTAs    = g_cSUPNtKernelRootTAs;
            cSets++;
            break;

        case VERIFYEXESTATE::kSignType_OSX:
            aSets[cSets].hStore  = State.hRootStore;
            aSets[cSets].paTAs   = g_aSUPAppleRootTAs;
            aSets[cSets].cTAs    = g_cSUPAppleRootTAs;
            cSets++;
            break;
    }
    for (unsigned i = 0; i < cSets; i++)
        for (unsigned j = 0; j < aSets[i].cTAs; j++)
        {
            rc = RTCrStoreCertAddEncoded(aSets[i].hStore, RTCRCERTCTX_F_ENC_TAF_DER, aSets[i].paTAs[j].pch,
                                         aSets[i].paTAs[j].cb, RTErrInfoInitStatic(&StaticErrInfo));
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTCrStoreCertAddEncoded failed (%u/%u): %s",
                                      i, j, StaticErrInfo.szMsg);
        }
# endif /* VBOX */

    /*
     * Do it.
     */
    RTEXITCODE rcExit;
    for (;;)
    {
        rcExit = HandleVerifyExeWorker(&State, ValueUnion.psz, &StaticErrInfo);
        if (rcExit != RTEXITCODE_SUCCESS)
            break;

        /*
         * Next file
         */
        ch = RTGetOpt(&GetState, &ValueUnion);
        if (ch == 0)
            break;
        if (ch != VINF_GETOPT_NOT_OPTION)
        {
            rcExit = RTGetOptPrintError(ch, &ValueUnion);
            break;
        }
    }

    /*
     * Clean up.
     */
    uint32_t cRefs;
    cRefs = RTCrStoreRelease(State.hRootStore);       Assert(cRefs == 0);
    cRefs = RTCrStoreRelease(State.hKernelRootStore); Assert(cRefs == 0);
    cRefs = RTCrStoreRelease(State.hAdditionalStore); Assert(cRefs == 0);

    return rcExit;
}

#endif /* !IPRT_IN_BUILD_TOOL */

/*
 * common code for show-exe and show-cat:
 */

/**
 * Display an object ID.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   pObjId              The object ID to display.
 * @param   pszLabel            The field label (prefixed by szPrefix).
 * @param   pszPost             What to print after the ID (typically newline).
 */
static void HandleShowExeWorkerDisplayObjId(PSHOWEXEPKCS7 pThis, PCRTASN1OBJID pObjId, const char *pszLabel, const char *pszPost)
{
    int rc = RTAsn1QueryObjIdName(pObjId, pThis->szTmp, sizeof(pThis->szTmp));
    if (RT_SUCCESS(rc))
    {
        if (pThis->cVerbosity > 1)
            RTPrintf("%s%s%s (%s)%s", pThis->szPrefix, pszLabel, pThis->szTmp, pObjId->szObjId, pszPost);
        else
            RTPrintf("%s%s%s%s", pThis->szPrefix, pszLabel, pThis->szTmp, pszPost);
    }
    else
        RTPrintf("%s%s%s%s", pThis->szPrefix, pszLabel, pObjId->szObjId, pszPost);
}


/**
 * Display an object ID, without prefix and label
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   pObjId              The object ID to display.
 * @param   pszPost             What to print after the ID (typically newline).
 */
static void HandleShowExeWorkerDisplayObjIdSimple(PSHOWEXEPKCS7 pThis, PCRTASN1OBJID pObjId, const char *pszPost)
{
    int rc = RTAsn1QueryObjIdName(pObjId, pThis->szTmp, sizeof(pThis->szTmp));
    if (RT_SUCCESS(rc))
    {
        if (pThis->cVerbosity > 1)
            RTPrintf("%s (%s)%s", pThis->szTmp, pObjId->szObjId, pszPost);
        else
            RTPrintf("%s%s", pThis->szTmp, pszPost);
    }
    else
        RTPrintf("%s%s", pObjId->szObjId, pszPost);
}


/**
 * Display a signer info attribute.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   offPrefix           The current prefix offset.
 * @param   pAttr               The attribute to display.
 */
static int HandleShowExeWorkerPkcs7DisplayAttrib(PSHOWEXEPKCS7 pThis, size_t offPrefix, PCRTCRPKCS7ATTRIBUTE pAttr)
{
    HandleShowExeWorkerDisplayObjId(pThis, &pAttr->Type, "", ":\n");
    if (pThis->cVerbosity > 4 && pAttr->SeqCore.Asn1Core.uData.pu8)
        RTPrintf("%s uData.pu8=%p cb=%#x\n", pThis->szPrefix, pAttr->SeqCore.Asn1Core.uData.pu8, pAttr->SeqCore.Asn1Core.cb);

    int rc = VINF_SUCCESS;
    switch (pAttr->enmType)
    {
        case RTCRPKCS7ATTRIBUTETYPE_UNKNOWN:
            if (pAttr->uValues.pCores->cItems <= 1)
                RTPrintf("%s %u bytes\n", pThis->szPrefix,pAttr->uValues.pCores->SetCore.Asn1Core.cb);
            else
                RTPrintf("%s %u bytes divided by %u items\n", pThis->szPrefix, pAttr->uValues.pCores->SetCore.Asn1Core.cb, pAttr->uValues.pCores->cItems);
            break;

        /* Object IDs, use pObjIds. */
        case RTCRPKCS7ATTRIBUTETYPE_OBJ_IDS:
            if (pAttr->uValues.pObjIds->cItems != 1)
                RTPrintf("%s%u object IDs:", pThis->szPrefix, pAttr->uValues.pObjIds->cItems);
            for (unsigned i = 0; i < pAttr->uValues.pObjIds->cItems; i++)
            {
                if (pAttr->uValues.pObjIds->cItems == 1)
                    RTPrintf("%s ", pThis->szPrefix);
                else
                    RTPrintf("%s ObjId[%u]: ", pThis->szPrefix, i);
                HandleShowExeWorkerDisplayObjIdSimple(pThis, pAttr->uValues.pObjIds->papItems[i], "\n");
            }
            break;

        /* Sequence of object IDs, use pObjIdSeqs. */
        case RTCRPKCS7ATTRIBUTETYPE_MS_STATEMENT_TYPE:
            if (pAttr->uValues.pObjIdSeqs->cItems != 1)
                RTPrintf("%s%u object IDs:", pThis->szPrefix, pAttr->uValues.pObjIdSeqs->cItems);
            for (unsigned i = 0; i < pAttr->uValues.pObjIdSeqs->cItems; i++)
            {
                uint32_t const cObjIds = pAttr->uValues.pObjIdSeqs->papItems[i]->cItems;
                for (unsigned j = 0; j < cObjIds; j++)
                {
                    if (pAttr->uValues.pObjIdSeqs->cItems == 1)
                        RTPrintf("%s ", pThis->szPrefix);
                    else
                        RTPrintf("%s ObjIdSeq[%u]: ", pThis->szPrefix, i);
                    if (cObjIds != 1)
                        RTPrintf(" ObjId[%u]: ", j);
                    HandleShowExeWorkerDisplayObjIdSimple(pThis, pAttr->uValues.pObjIdSeqs->papItems[i]->papItems[i], "\n");
                }
            }
            break;

        /* Octet strings, use pOctetStrings. */
        case RTCRPKCS7ATTRIBUTETYPE_OCTET_STRINGS:
            if (pAttr->uValues.pOctetStrings->cItems != 1)
                RTPrintf("%s%u octet strings:", pThis->szPrefix, pAttr->uValues.pOctetStrings->cItems);
            for (unsigned i = 0; i < pAttr->uValues.pOctetStrings->cItems; i++)
            {
                PCRTASN1OCTETSTRING pOctetString = pAttr->uValues.pOctetStrings->papItems[i];
                uint32_t cbContent = pOctetString->Asn1Core.cb;
                if (cbContent > 0 && (cbContent <= 128 || pThis->cVerbosity >= 2))
                {
                    uint8_t const *pbContent = pOctetString->Asn1Core.uData.pu8;
                    uint32_t       off       = 0;
                    while (off < cbContent)
                    {
                        uint32_t cbNow = RT_MIN(cbContent - off, 16);
                        if (pAttr->uValues.pOctetStrings->cItems == 1)
                            RTPrintf("%s %#06x: %.*Rhxs\n", pThis->szPrefix, off, cbNow, &pbContent[off]);
                        else
                            RTPrintf("%s OctetString[%u]: %#06x: %.*Rhxs\n", pThis->szPrefix, i, off, cbNow, &pbContent[off]);
                        off += cbNow;
                    }
                }
                else
                    RTPrintf("%s: OctetString[%u]: %u bytes\n", pThis->szPrefix, i, pOctetString->Asn1Core.cb);
            }
            break;

        /* Counter signatures (PKCS \#9), use pCounterSignatures. */
        case RTCRPKCS7ATTRIBUTETYPE_COUNTER_SIGNATURES:
            RTPrintf("%s%u counter signatures, %u bytes in total\n", pThis->szPrefix,
                     pAttr->uValues.pCounterSignatures->cItems, pAttr->uValues.pCounterSignatures->SetCore.Asn1Core.cb);
            for (uint32_t i = 0; i < pAttr->uValues.pCounterSignatures->cItems; i++)
            {
                size_t offPrefix2 = offPrefix;
                if (pAttr->uValues.pContentInfos->cItems > 1)
                    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "CounterSig[%u]: ", i);
                else
                    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "  ");

                int rc2 = HandleShowExeWorkerPkcs7DisplaySignerInfo(pThis, offPrefix2,
                                                                    pAttr->uValues.pCounterSignatures->papItems[i]);
                if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                    rc = rc2;
            }
            break;

        /* Signing time (PKCS \#9), use pSigningTime. */
        case RTCRPKCS7ATTRIBUTETYPE_SIGNING_TIME:
            for (uint32_t i = 0; i < pAttr->uValues.pSigningTime->cItems; i++)
            {
                PCRTASN1TIME pTime = pAttr->uValues.pSigningTime->papItems[i];
                char szTS[RTTIME_STR_LEN];
                RTTimeToString(&pTime->Time, szTS, sizeof(szTS));
                if (pAttr->uValues.pSigningTime->cItems == 1)
                    RTPrintf("%s %s (%.*s)\n", pThis->szPrefix, szTS, pTime->Asn1Core.cb, pTime->Asn1Core.uData.pch);
                else
                    RTPrintf("%s #%u: %s (%.*s)\n", pThis->szPrefix, i, szTS, pTime->Asn1Core.cb, pTime->Asn1Core.uData.pch);
            }
            break;

        /* Microsoft timestamp info (RFC-3161) signed data, use pContentInfo. */
        case RTCRPKCS7ATTRIBUTETYPE_MS_TIMESTAMP:
        case RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE:
            if (pAttr->uValues.pContentInfos->cItems > 1)
                RTPrintf("%s%u nested signatures, %u bytes in total\n", pThis->szPrefix,
                         pAttr->uValues.pContentInfos->cItems, pAttr->uValues.pContentInfos->SetCore.Asn1Core.cb);
            for (unsigned i = 0; i < pAttr->uValues.pContentInfos->cItems; i++)
            {
                size_t offPrefix2 = offPrefix;
                if (pAttr->uValues.pContentInfos->cItems > 1)
                    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "NestedSig[%u]: ", i);
                else
                    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "  ");
                //    offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "NestedSig: ", i);
                PCRTCRPKCS7CONTENTINFO pContentInfo = pAttr->uValues.pContentInfos->papItems[i];
                int rc2;
                if (RTCrPkcs7ContentInfo_IsSignedData(pContentInfo))
                    rc2 = HandleShowExeWorkerPkcs7Display(pThis, pContentInfo->u.pSignedData, offPrefix2, pContentInfo);
                else
                    rc2 = RTMsgErrorRc(VERR_ASN1_UNEXPECTED_OBJ_ID, "%sPKCS#7 content in nested signature is not 'signedData': %s",
                                       pThis->szPrefix, pContentInfo->ContentType.szObjId);
                if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                    rc = rc2;
            }
            break;

        case RTCRPKCS7ATTRIBUTETYPE_APPLE_MULTI_CD_PLIST:
            if (pAttr->uValues.pContentInfos->cItems != 1)
                RTPrintf("%s%u plists, expected only 1.\n", pThis->szPrefix, pAttr->uValues.pOctetStrings->cItems);
            for (unsigned i = 0; i < pAttr->uValues.pOctetStrings->cItems; i++)
            {
                PCRTASN1OCTETSTRING pOctetString = pAttr->uValues.pOctetStrings->papItems[i];
                size_t              cbContent    = pOctetString->Asn1Core.cb;
                char  const        *pchContent   = pOctetString->Asn1Core.uData.pch;
                rc = RTStrValidateEncodingEx(pchContent, cbContent, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
                if (RT_SUCCESS(rc))
                {
                    while (cbContent > 0)
                    {
                        const char *pchNewLine = (const char *)memchr(pchContent, '\n', cbContent);
                        size_t      cchToWrite = pchNewLine ? pchNewLine - pchContent : cbContent;
                        if (pAttr->uValues.pOctetStrings->cItems == 1)
                            RTPrintf("%s %.*s\n", pThis->szPrefix, cchToWrite, pchContent);
                        else
                            RTPrintf("%s plist[%u]: %.*s\n", pThis->szPrefix, i, cchToWrite, pchContent);
                        if (!pchNewLine)
                            break;
                        pchContent = pchNewLine + 1;
                        cbContent -= cchToWrite + 1;
                    }
                }
                else
                {
                    if (pAttr->uValues.pContentInfos->cItems != 1)
                        RTPrintf("%s: plist[%u]: Invalid UTF-8: %Rrc\n", pThis->szPrefix, i, rc);
                    else
                        RTPrintf("%s: Invalid UTF-8: %Rrc\n", pThis->szPrefix, rc);
                    for (uint32_t off = 0; off < cbContent; off += 16)
                    {
                        size_t cbNow = RT_MIN(cbContent - off, 16);
                        if (pAttr->uValues.pOctetStrings->cItems == 1)
                            RTPrintf("%s %#06x: %.*Rhxs\n", pThis->szPrefix, off, cbNow, &pchContent[off]);
                        else
                            RTPrintf("%s plist[%u]: %#06x: %.*Rhxs\n", pThis->szPrefix, i, off, cbNow, &pchContent[off]);
                    }
                }
            }
            break;

        case RTCRPKCS7ATTRIBUTETYPE_INVALID:
            RTPrintf("%sINVALID!\n", pThis->szPrefix);
            break;
        case RTCRPKCS7ATTRIBUTETYPE_NOT_PRESENT:
            RTPrintf("%sNOT PRESENT!\n", pThis->szPrefix);
            break;
        default:
            RTPrintf("%senmType=%d!\n", pThis->szPrefix, pAttr->enmType);
            break;
    }
    return rc;
}


/**
 * Displays a SignerInfo structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   offPrefix           The current prefix offset.
 * @param   pSignerInfo         The structure to display.
 */
static int HandleShowExeWorkerPkcs7DisplaySignerInfo(PSHOWEXEPKCS7 pThis, size_t offPrefix, PCRTCRPKCS7SIGNERINFO pSignerInfo)
{
    int rc = RTAsn1Integer_ToString(&pSignerInfo->IssuerAndSerialNumber.SerialNumber,
                                    pThis->szTmp, sizeof(pThis->szTmp), 0 /*fFlags*/, NULL);
    if (RT_FAILURE(rc))
        RTStrPrintf(pThis->szTmp, sizeof(pThis->szTmp), "%Rrc", rc);
    RTPrintf("%s                  Serial No: %s\n", pThis->szPrefix, pThis->szTmp);

    rc = RTCrX509Name_FormatAsString(&pSignerInfo->IssuerAndSerialNumber.Name, pThis->szTmp, sizeof(pThis->szTmp), NULL);
    if (RT_FAILURE(rc))
        RTStrPrintf(pThis->szTmp, sizeof(pThis->szTmp), "%Rrc", rc);
    RTPrintf("%s                     Issuer: %s\n", pThis->szPrefix, pThis->szTmp);

    const char *pszType = RTCrDigestTypeToName(RTCrX509AlgorithmIdentifier_QueryDigestType(&pSignerInfo->DigestAlgorithm));
    if (!pszType)
        pszType = pSignerInfo->DigestAlgorithm.Algorithm.szObjId;
    RTPrintf("%s           Digest Algorithm: %s", pThis->szPrefix, pszType);
    if (pThis->cVerbosity > 1)
        RTPrintf(" (%s)\n", pSignerInfo->DigestAlgorithm.Algorithm.szObjId);
    else
        RTPrintf("\n");

    HandleShowExeWorkerDisplayObjId(pThis, &pSignerInfo->DigestEncryptionAlgorithm.Algorithm,
                                    "Digest Encryption Algorithm: ", "\n");

    if (pSignerInfo->AuthenticatedAttributes.cItems == 0)
        RTPrintf("%s   Authenticated Attributes: none\n", pThis->szPrefix);
    else
    {
        RTPrintf("%s   Authenticated Attributes: %u item%s\n", pThis->szPrefix,
                 pSignerInfo->AuthenticatedAttributes.cItems, pSignerInfo->AuthenticatedAttributes.cItems > 1 ? "s" : "");
        for (unsigned j = 0; j < pSignerInfo->AuthenticatedAttributes.cItems; j++)
        {
            PRTCRPKCS7ATTRIBUTE pAttr = pSignerInfo->AuthenticatedAttributes.papItems[j];
            size_t offPrefix3 = offPrefix+ RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix,
                                                         "              AuthAttrib[%u]: ", j);
            HandleShowExeWorkerPkcs7DisplayAttrib(pThis, offPrefix3, pAttr);
        }
        pThis->szPrefix[offPrefix] = '\0';
    }

    if (pSignerInfo->UnauthenticatedAttributes.cItems == 0)
        RTPrintf("%s Unauthenticated Attributes: none\n", pThis->szPrefix);
    else
    {
        RTPrintf("%s Unauthenticated Attributes: %u item%s\n", pThis->szPrefix,
                 pSignerInfo->UnauthenticatedAttributes.cItems, pSignerInfo->UnauthenticatedAttributes.cItems > 1 ? "s" : "");
        for (unsigned j = 0; j < pSignerInfo->UnauthenticatedAttributes.cItems; j++)
        {
            PRTCRPKCS7ATTRIBUTE pAttr = pSignerInfo->UnauthenticatedAttributes.papItems[j];
            size_t offPrefix3 = offPrefix + RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix,
                                                        "            UnauthAttrib[%u]: ", j);
            HandleShowExeWorkerPkcs7DisplayAttrib(pThis, offPrefix3, pAttr);
        }
        pThis->szPrefix[offPrefix] = '\0';
    }

    /** @todo show the encrypted stuff (EncryptedDigest)?   */
    return rc;
}


/**
 * Displays a Microsoft SPC indirect data structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   offPrefix           The current prefix offset.
 * @param   pIndData            The indirect data to display.
 */
static int HandleShowExeWorkerPkcs7DisplaySpcIdirectDataContent(PSHOWEXEPKCS7 pThis, size_t offPrefix,
                                                                PCRTCRSPCINDIRECTDATACONTENT pIndData)
{
    /*
     * The image hash.
     */
    RTDIGESTTYPE const enmDigestType = RTCrX509AlgorithmIdentifier_QueryDigestType(&pIndData->DigestInfo.DigestAlgorithm);
    const char        *pszDigestType = RTCrDigestTypeToName(enmDigestType);
    RTPrintf("%s Digest Type: %s", pThis->szPrefix, pszDigestType);
    if (pThis->cVerbosity > 1)
        RTPrintf(" (%s)\n", pIndData->DigestInfo.DigestAlgorithm.Algorithm.szObjId);
    else
        RTPrintf("\n");
    RTPrintf("%s      Digest: %.*Rhxs\n",
             pThis->szPrefix, pIndData->DigestInfo.Digest.Asn1Core.cb, pIndData->DigestInfo.Digest.Asn1Core.uData.pu8);

    /*
     * The data/file/url.
     */
    switch (pIndData->Data.enmType)
    {
        case RTCRSPCAAOVTYPE_PE_IMAGE_DATA:
        {
            RTPrintf("%s   Data Type: PE Image Data\n", pThis->szPrefix);
            PRTCRSPCPEIMAGEDATA pPeImage = pIndData->Data.uValue.pPeImage;
            /** @todo display "Flags". */

            switch (pPeImage->T0.File.enmChoice)
            {
                case RTCRSPCLINKCHOICE_MONIKER:
                {
                    PRTCRSPCSERIALIZEDOBJECT pMoniker = pPeImage->T0.File.u.pMoniker;
                    if (RTCrSpcSerializedObject_IsPresent(pMoniker))
                    {
                        if (RTUuidCompareStr(pMoniker->Uuid.Asn1Core.uData.pUuid, RTCRSPCSERIALIZEDOBJECT_UUID_STR) == 0)
                        {
                            RTPrintf("%s     Moniker: SpcSerializedObject (%RTuuid)\n",
                                     pThis->szPrefix, pMoniker->Uuid.Asn1Core.uData.pUuid);

                            PCRTCRSPCSERIALIZEDOBJECTATTRIBUTES pData = pMoniker->u.pData;
                            if (pData)
                                for (uint32_t i = 0; i < pData->cItems; i++)
                                {
                                    RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix,
                                                "MonikerAttrib[%u]: ", i);

                                    switch (pData->papItems[i]->enmType)
                                    {
                                        case RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V2:
                                        case RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V1:
                                        {
                                            PCRTCRSPCSERIALIZEDPAGEHASHES pPgHashes = pData->papItems[i]->u.pPageHashes;
                                            uint32_t const cbHash =    pData->papItems[i]->enmType
                                                                    == RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V1
                                                                  ? 160/8 /*SHA-1*/ : 256/8 /*SHA-256*/;
                                            uint32_t const cPages = pPgHashes->RawData.Asn1Core.cb / (cbHash + sizeof(uint32_t));

                                            RTPrintf("%sPage Hashes version %u - %u pages (%u bytes total)\n", pThis->szPrefix,
                                                        pData->papItems[i]->enmType
                                                     == RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V1 ? 1 : 2,
                                                     cPages, pPgHashes->RawData.Asn1Core.cb);
                                            if (pThis->cVerbosity > 0)
                                            {
                                                PCRTCRSPCPEIMAGEPAGEHASHES pPg = pPgHashes->pData;
                                                for (unsigned iPg = 0; iPg < cPages; iPg++)
                                                {
                                                    uint32_t offHash = 0;
                                                    do
                                                    {
                                                        if (offHash == 0)
                                                            RTPrintf("%.*s  Page#%04u/%#08x: ",
                                                                     offPrefix, pThis->szPrefix, iPg, pPg->Generic.offFile);
                                                        else
                                                            RTPrintf("%.*s                      ", offPrefix, pThis->szPrefix);
                                                        uint32_t cbLeft = cbHash - offHash;
                                                        if (cbLeft > 24)
                                                            cbLeft = 16;
                                                        RTPrintf("%.*Rhxs\n", cbLeft, &pPg->Generic.abHash[offHash]);
                                                        offHash += cbLeft;
                                                    } while (offHash < cbHash);
                                                    pPg = (PCRTCRSPCPEIMAGEPAGEHASHES)&pPg->Generic.abHash[cbHash];
                                                }

                                                if (pThis->cVerbosity > 3)
                                                    RTPrintf("%.*Rhxd\n",
                                                             pPgHashes->RawData.Asn1Core.cb,
                                                             pPgHashes->RawData.Asn1Core.uData.pu8);
                                            }
                                            break;
                                        }

                                        case RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_UNKNOWN:
                                            HandleShowExeWorkerDisplayObjIdSimple(pThis, &pData->papItems[i]->Type, "\n");
                                            break;
                                        case RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_NOT_PRESENT:
                                            RTPrintf("%sNot present!\n", pThis->szPrefix);
                                            break;
                                        default:
                                            RTPrintf("%senmType=%d!\n", pThis->szPrefix, pData->papItems[i]->enmType);
                                            break;
                                    }
                                    pThis->szPrefix[offPrefix] = '\0';
                                }
                            else
                                RTPrintf("%s              pData is NULL!\n", pThis->szPrefix);
                        }
                        else
                            RTPrintf("%s     Moniker: Unknown UUID: %RTuuid\n",
                                     pThis->szPrefix, pMoniker->Uuid.Asn1Core.uData.pUuid);
                    }
                    else
                        RTPrintf("%s     Moniker: not present\n", pThis->szPrefix);
                    break;
                }

                case RTCRSPCLINKCHOICE_URL:
                {
                    const char *pszUrl = NULL;
                    int rc = pPeImage->T0.File.u.pUrl
                           ? RTAsn1String_QueryUtf8(pPeImage->T0.File.u.pUrl, &pszUrl, NULL)
                           : VERR_NOT_FOUND;
                    if (RT_SUCCESS(rc))
                        RTPrintf("%s         URL: '%s'\n", pThis->szPrefix, pszUrl);
                    else
                        RTPrintf("%s         URL: rc=%Rrc\n", pThis->szPrefix, rc);
                    break;
                }

                case RTCRSPCLINKCHOICE_FILE:
                {
                    const char *pszFile = NULL;
                    int rc = pPeImage->T0.File.u.pT2 && pPeImage->T0.File.u.pT2->File.u.pAscii
                           ? RTAsn1String_QueryUtf8(pPeImage->T0.File.u.pT2->File.u.pAscii, &pszFile, NULL)
                           : VERR_NOT_FOUND;
                    if (RT_SUCCESS(rc))
                        RTPrintf("%s        File: '%s'\n", pThis->szPrefix, pszFile);
                    else
                        RTPrintf("%s        File: rc=%Rrc\n", pThis->szPrefix, rc);
                    if (pThis->cVerbosity > 4 && pPeImage->T0.File.u.pT2 == NULL)
                        RTPrintf("%s        pT2=NULL\n", pThis->szPrefix);
                    else if (pThis->cVerbosity > 4)
                    {
                        PCRTASN1STRING pStr = pPeImage->T0.File.u.pT2->File.u.pAscii;
                        RTPrintf("%s        pT2=%p/%p LB %#x fFlags=%#x pOps=%p (%s)\n"
                                 "%s        enmChoice=%d pStr=%p/%p LB %#x fFlags=%#x\n",
                                 pThis->szPrefix,
                                 pPeImage->T0.File.u.pT2,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.uData.pu8,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.cb,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.fFlags,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.pOps,
                                 pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.pOps
                                 ? pPeImage->T0.File.u.pT2->CtxTag2.Asn1Core.pOps->pszName : "",
                                 pThis->szPrefix,
                                 pPeImage->T0.File.u.pT2->File.enmChoice,
                                 pStr,
                                 pStr ? pStr->Asn1Core.uData.pu8 : NULL,
                                 pStr ? pStr->Asn1Core.cb : 0,
                                 pStr ? pStr->Asn1Core.fFlags : 0);
                    }
                    break;
                }

                case RTCRSPCLINKCHOICE_NOT_PRESENT:
                    RTPrintf("%s              File not present!\n", pThis->szPrefix);
                    break;
                default:
                    RTPrintf("%s              enmChoice=%d!\n", pThis->szPrefix, pPeImage->T0.File.enmChoice);
                    break;
            }
            break;
        }

        case RTCRSPCAAOVTYPE_UNKNOWN:
            HandleShowExeWorkerDisplayObjId(pThis, &pIndData->Data.Type, "   Data Type: ", "\n");
            break;
        case RTCRSPCAAOVTYPE_NOT_PRESENT:
            RTPrintf("%s   Data Type: Not present!\n", pThis->szPrefix);
            break;
        default:
            RTPrintf("%s   Data Type: enmType=%d!\n", pThis->szPrefix, pIndData->Data.enmType);
            break;
    }

    return VINF_SUCCESS;
}


/**
 * Display an PKCS#7 signed data instance.
 *
 * @returns IPRT status code.
 * @param   pThis               The show exe instance data.
 * @param   pSignedData         The signed data to display.
 * @param   offPrefix           The current prefix offset.
 * @param   pContentInfo        The content info structure (for the size).
 */
static int HandleShowExeWorkerPkcs7Display(PSHOWEXEPKCS7 pThis, PRTCRPKCS7SIGNEDDATA pSignedData, size_t offPrefix,
                                           PCRTCRPKCS7CONTENTINFO pContentInfo)
{
    pThis->szPrefix[offPrefix] = '\0';
    RTPrintf("%sPKCS#7 signature: %u (%#x) bytes\n", pThis->szPrefix,
             RTASN1CORE_GET_RAW_ASN1_SIZE(&pContentInfo->SeqCore.Asn1Core),
             RTASN1CORE_GET_RAW_ASN1_SIZE(&pContentInfo->SeqCore.Asn1Core));

    /*
     * Display list of signing algorithms.
     */
    RTPrintf("%sDigestAlgorithms: ", pThis->szPrefix);
    if (pSignedData->DigestAlgorithms.cItems == 0)
        RTPrintf("none");
    for (unsigned i = 0; i < pSignedData->DigestAlgorithms.cItems; i++)
    {
        PCRTCRX509ALGORITHMIDENTIFIER pAlgoId = pSignedData->DigestAlgorithms.papItems[i];
        const char *pszDigestType = RTCrDigestTypeToName(RTCrX509AlgorithmIdentifier_QueryDigestType(pAlgoId));
        if (!pszDigestType)
            pszDigestType = pAlgoId->Algorithm.szObjId;
        RTPrintf(i == 0 ? "%s" : ", %s", pszDigestType);
        if (pThis->cVerbosity > 1)
            RTPrintf(" (%s)", pAlgoId->Algorithm.szObjId);
    }
    RTPrintf("\n");

    /*
     * Display the signed data content.
     */
    if (RTAsn1ObjId_CompareWithString(&pSignedData->ContentInfo.ContentType, RTCRSPCINDIRECTDATACONTENT_OID) == 0)
    {
        RTPrintf("%s     ContentType: SpcIndirectDataContent (" RTCRSPCINDIRECTDATACONTENT_OID ")\n", pThis->szPrefix);
        size_t offPrefix2 = RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "    SPC Ind Data: ");
        HandleShowExeWorkerPkcs7DisplaySpcIdirectDataContent(pThis, offPrefix2 + offPrefix,
                                                             pSignedData->ContentInfo.u.pIndirectDataContent);
        pThis->szPrefix[offPrefix] = '\0';
    }
    else
        HandleShowExeWorkerDisplayObjId(pThis, &pSignedData->ContentInfo.ContentType, "     ContentType: ", " - not implemented.\n");

    /*
     * Display certificates (Certificates).
     */
    if (pSignedData->Certificates.cItems > 0)
    {
        RTPrintf("%s    Certificates: %u\n", pThis->szPrefix, pSignedData->Certificates.cItems);
        for (uint32_t i = 0; i < pSignedData->Certificates.cItems; i++)
        {
            PCRTCRPKCS7CERT pCert = pSignedData->Certificates.papItems[i];
            if (i != 0 && pThis->cVerbosity >= 2)
                RTPrintf("\n");
            switch (pCert->enmChoice)
            {
                case RTCRPKCS7CERTCHOICE_X509:
                {
                    PCRTCRX509CERTIFICATE pX509Cert = pCert->u.pX509Cert;
                    int rc2 = RTAsn1QueryObjIdName(&pX509Cert->SignatureAlgorithm.Algorithm, pThis->szTmp, sizeof(pThis->szTmp));
                    RTPrintf("%s      Certificate #%u: %s\n", pThis->szPrefix, i,
                             RT_SUCCESS(rc2) ? pThis->szTmp : pX509Cert->SignatureAlgorithm.Algorithm.szObjId);

                    rc2 = RTCrX509Name_FormatAsString(&pX509Cert->TbsCertificate.Subject,
                                                      pThis->szTmp, sizeof(pThis->szTmp), NULL);
                    if (RT_FAILURE(rc2))
                        RTStrPrintf(pThis->szTmp, sizeof(pThis->szTmp), "%Rrc", rc2);
                    RTPrintf("%s        Subject: %s\n", pThis->szPrefix, pThis->szTmp);

                    rc2 = RTCrX509Name_FormatAsString(&pX509Cert->TbsCertificate.Issuer,
                                                      pThis->szTmp, sizeof(pThis->szTmp), NULL);
                    if (RT_FAILURE(rc2))
                        RTStrPrintf(pThis->szTmp, sizeof(pThis->szTmp), "%Rrc", rc2);
                    RTPrintf("%s         Issuer: %s\n", pThis->szPrefix, pThis->szTmp);


                    char szNotAfter[RTTIME_STR_LEN];
                    RTPrintf("%s          Valid: %s thru %s\n", pThis->szPrefix,
                             RTTimeToString(&pX509Cert->TbsCertificate.Validity.NotBefore.Time,
                                            pThis->szTmp, sizeof(pThis->szTmp)),
                             RTTimeToString(&pX509Cert->TbsCertificate.Validity.NotAfter.Time,
                                            szNotAfter, sizeof(szNotAfter)));
                    break;
                }

                default:
                    RTPrintf("%s      Certificate #%u: Unsupported type\n", pThis->szPrefix, i);
                    break;
            }


            if (pThis->cVerbosity >= 2)
                RTAsn1Dump(RTCrPkcs7Cert_GetAsn1Core(pSignedData->Certificates.papItems[i]), 0,
                           ((uint32_t)offPrefix + 9) / 2, RTStrmDumpPrintfV, g_pStdOut);
        }

        /** @todo display certificates properly. */
    }

    if (pSignedData->Crls.cb > 0)
        RTPrintf("%s            CRLs: %u bytes\n", pThis->szPrefix, pSignedData->Crls.cb);

    /*
     * Show signatures (SignerInfos).
     */
    unsigned const cSigInfos = pSignedData->SignerInfos.cItems;
    if (cSigInfos != 1)
        RTPrintf("%s     SignerInfos: %u signers\n", pThis->szPrefix, cSigInfos);
    else
        RTPrintf("%s     SignerInfos:\n", pThis->szPrefix);
    int rc = VINF_SUCCESS;
    for (unsigned i = 0; i < cSigInfos; i++)
    {
        size_t offPrefix2 = offPrefix;
        if (cSigInfos != 1)
            offPrefix2 += RTStrPrintf(&pThis->szPrefix[offPrefix], sizeof(pThis->szPrefix) - offPrefix, "SignerInfo[%u]: ", i);

        int rc2 = HandleShowExeWorkerPkcs7DisplaySignerInfo(pThis, offPrefix2, pSignedData->SignerInfos.papItems[i]);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }
    pThis->szPrefix[offPrefix] = '\0';

    return rc;
}


/*
 * The 'show-exe' command.
 */
static RTEXITCODE HelpShowExe(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT, "show-exe [--verbose|-v] [--quiet|-q] <exe1> [exe2 [..]]\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleShowExe(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q', RTGETOPT_REQ_NOTHING },
    };

    unsigned  cVerbosity = 0;
    RTLDRARCH enmLdrArch = RTLDRARCH_WHATEVER;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) && ch != VINF_GETOPT_NOT_OPTION)
    {
        switch (ch)
        {
            case 'v': cVerbosity++; break;
            case 'q': cVerbosity = 0; break;
            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpShowExe(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (ch != VINF_GETOPT_NOT_OPTION)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");

    /*
     * Do it.
     */
    unsigned   iFile  = 0;
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    do
    {
        RTPrintf(iFile == 0 ? "%s:\n" : "\n%s:\n", ValueUnion.psz);

        SHOWEXEPKCS7 This;
        RT_ZERO(This);
        This.cVerbosity = cVerbosity;

        RTEXITCODE rcExitThis = SignToolPkcs7Exe_InitFromFile(&This, ValueUnion.psz, cVerbosity, enmLdrArch);
        if (rcExitThis == RTEXITCODE_SUCCESS)
        {
            rc = HandleShowExeWorkerPkcs7Display(&This, This.pSignedData, 0, &This.ContentInfo);
            if (RT_FAILURE(rc))
                rcExit = RTEXITCODE_FAILURE;
            SignToolPkcs7Exe_Delete(&This);
        }
        if (rcExitThis != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
            rcExit = rcExitThis;

        iFile++;
    } while ((ch = RTGetOpt(&GetState, &ValueUnion)) == VINF_GETOPT_NOT_OPTION);
    if (ch != 0)
        return RTGetOptPrintError(ch, &ValueUnion);

    return rcExit;
}


/*
 * The 'show-cat' command.
 */
static RTEXITCODE HelpShowCat(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT, "show-cat [--verbose|-v] [--quiet|-q] <cat1> [cat2 [..]]\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE HandleShowCat(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q', RTGETOPT_REQ_NOTHING },
    };

    unsigned  cVerbosity = 0;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) && ch != VINF_GETOPT_NOT_OPTION)
    {
        switch (ch)
        {
            case 'v': cVerbosity++; break;
            case 'q': cVerbosity = 0; break;
            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpShowCat(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (ch != VINF_GETOPT_NOT_OPTION)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No executable given.");

    /*
     * Do it.
     */
    unsigned   iFile  = 0;
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    do
    {
        RTPrintf(iFile == 0 ? "%s:\n" : "\n%s:\n", ValueUnion.psz);

        SHOWEXEPKCS7 This;
        RT_ZERO(This);
        This.cVerbosity = cVerbosity;

        RTEXITCODE rcExitThis = SignToolPkcs7_InitFromFile(&This, ValueUnion.psz, cVerbosity);
        if (rcExitThis == RTEXITCODE_SUCCESS)
        {
            This.hLdrMod = NIL_RTLDRMOD;

            rc = HandleShowExeWorkerPkcs7Display(&This, This.pSignedData, 0, &This.ContentInfo);
            if (RT_FAILURE(rc))
                rcExit = RTEXITCODE_FAILURE;
            SignToolPkcs7Exe_Delete(&This);
        }
        if (rcExitThis != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
            rcExit = rcExitThis;

        iFile++;
    } while ((ch = RTGetOpt(&GetState, &ValueUnion)) == VINF_GETOPT_NOT_OPTION);
    if (ch != 0)
        return RTGetOptPrintError(ch, &ValueUnion);

    return rcExit;
}


/*
 * The 'make-tainfo' command.
 */
static RTEXITCODE HelpMakeTaInfo(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmWrappedPrintf(pStrm, RTSTRMWRAPPED_F_HANGING_INDENT,
                        "make-tainfo [--verbose|--quiet] [--cert <cert.der>]  [-o|--output] <tainfo.der>\n");
    return RTEXITCODE_SUCCESS;
}


typedef struct MAKETAINFOSTATE
{
    int         cVerbose;
    const char *pszCert;
    const char *pszOutput;
} MAKETAINFOSTATE;


/** @callback_method_impl{FNRTASN1ENCODEWRITER}  */
static DECLCALLBACK(int) handleMakeTaInfoWriter(const void *pvBuf, size_t cbToWrite, void *pvUser, PRTERRINFO pErrInfo)
{
    RT_NOREF_PV(pErrInfo);
    return RTStrmWrite((PRTSTREAM)pvUser, pvBuf, cbToWrite);
}


static RTEXITCODE HandleMakeTaInfo(int cArgs, char **papszArgs)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--cert",         'c', RTGETOPT_REQ_STRING },
        { "--output",       'o', RTGETOPT_REQ_STRING },
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q', RTGETOPT_REQ_NOTHING },
    };

    MAKETAINFOSTATE State = { 0, NULL, NULL };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    int ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'c':
                if (State.pszCert)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "The --cert option can only be used once.");
                State.pszCert = ValueUnion.psz;
                break;

            case 'o':
            case VINF_GETOPT_NOT_OPTION:
                if (State.pszOutput)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Multiple output files specified.");
                State.pszOutput = ValueUnion.psz;
                break;

            case 'v': State.cVerbose++; break;
            case 'q': State.cVerbose = 0; break;
            case 'V': return HandleVersion(cArgs, papszArgs);
            case 'h': return HelpMakeTaInfo(g_pStdOut, RTSIGNTOOLHELP_FULL);
            default:  return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!State.pszCert)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No input certificate was specified.");
    if (!State.pszOutput)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No output file was specified.");

    /*
     * Read the certificate.
     */
    RTERRINFOSTATIC         StaticErrInfo;
    RTCRX509CERTIFICATE     Certificate;
    rc = RTCrX509Certificate_ReadFromFile(&Certificate, State.pszCert, 0, &g_RTAsn1DefaultAllocator,
                                          RTErrInfoInitStatic(&StaticErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error reading certificate from %s: %Rrc - %s",
                              State.pszCert, rc, StaticErrInfo.szMsg);
    /*
     * Construct the trust anchor information.
     */
    RTCRTAFTRUSTANCHORINFO TrustAnchor;
    rc = RTCrTafTrustAnchorInfo_Init(&TrustAnchor, &g_RTAsn1DefaultAllocator);
    if (RT_SUCCESS(rc))
    {
        /* Public key. */
        Assert(RTCrX509SubjectPublicKeyInfo_IsPresent(&TrustAnchor.PubKey));
        RTCrX509SubjectPublicKeyInfo_Delete(&TrustAnchor.PubKey);
        rc = RTCrX509SubjectPublicKeyInfo_Clone(&TrustAnchor.PubKey, &Certificate.TbsCertificate.SubjectPublicKeyInfo,
                                                &g_RTAsn1DefaultAllocator);
        if (RT_FAILURE(rc))
            RTMsgError("RTCrX509SubjectPublicKeyInfo_Clone failed: %Rrc", rc);
        RTAsn1Core_ResetImplict(RTCrX509SubjectPublicKeyInfo_GetAsn1Core(&TrustAnchor.PubKey)); /* temporary hack. */

        /* Key Identifier. */
        PCRTASN1OCTETSTRING pKeyIdentifier = NULL;
        if (Certificate.TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_SUBJECT_KEY_IDENTIFIER)
            pKeyIdentifier = Certificate.TbsCertificate.T3.pSubjectKeyIdentifier;
        else if (   (Certificate.TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_AUTHORITY_KEY_IDENTIFIER)
                 && RTCrX509Certificate_IsSelfSigned(&Certificate)
                 && RTAsn1OctetString_IsPresent(&Certificate.TbsCertificate.T3.pAuthorityKeyIdentifier->KeyIdentifier) )
            pKeyIdentifier = &Certificate.TbsCertificate.T3.pAuthorityKeyIdentifier->KeyIdentifier;
        else if (   (Certificate.TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_OLD_AUTHORITY_KEY_IDENTIFIER)
                 && RTCrX509Certificate_IsSelfSigned(&Certificate)
                 && RTAsn1OctetString_IsPresent(&Certificate.TbsCertificate.T3.pOldAuthorityKeyIdentifier->KeyIdentifier) )
            pKeyIdentifier = &Certificate.TbsCertificate.T3.pOldAuthorityKeyIdentifier->KeyIdentifier;
        if (pKeyIdentifier && pKeyIdentifier->Asn1Core.cb > 0)
        {
            Assert(RTAsn1OctetString_IsPresent(&TrustAnchor.KeyIdentifier));
            RTAsn1OctetString_Delete(&TrustAnchor.KeyIdentifier);
            rc = RTAsn1OctetString_Clone(&TrustAnchor.KeyIdentifier, pKeyIdentifier, &g_RTAsn1DefaultAllocator);
            if (RT_FAILURE(rc))
                RTMsgError("RTAsn1OctetString_Clone failed: %Rrc", rc);
            RTAsn1Core_ResetImplict(RTAsn1OctetString_GetAsn1Core(&TrustAnchor.KeyIdentifier)); /* temporary hack. */
        }
        else
            RTMsgWarning("No key identifier found or has zero length.");

        /* Subject */
        if (RT_SUCCESS(rc))
        {
            Assert(!RTCrTafCertPathControls_IsPresent(&TrustAnchor.CertPath));
            rc = RTCrTafCertPathControls_Init(&TrustAnchor.CertPath, &g_RTAsn1DefaultAllocator);
            if (RT_SUCCESS(rc))
            {
                Assert(RTCrX509Name_IsPresent(&TrustAnchor.CertPath.TaName));
                RTCrX509Name_Delete(&TrustAnchor.CertPath.TaName);
                rc = RTCrX509Name_Clone(&TrustAnchor.CertPath.TaName, &Certificate.TbsCertificate.Subject,
                                        &g_RTAsn1DefaultAllocator);
                if (RT_SUCCESS(rc))
                {
                    RTAsn1Core_ResetImplict(RTCrX509Name_GetAsn1Core(&TrustAnchor.CertPath.TaName)); /* temporary hack. */
                    rc = RTCrX509Name_RecodeAsUtf8(&TrustAnchor.CertPath.TaName, &g_RTAsn1DefaultAllocator);
                    if (RT_FAILURE(rc))
                        RTMsgError("RTCrX509Name_RecodeAsUtf8 failed: %Rrc", rc);
                }
                else
                    RTMsgError("RTCrX509Name_Clone failed: %Rrc", rc);
            }
            else
                RTMsgError("RTCrTafCertPathControls_Init failed: %Rrc", rc);
        }

        /* Check that what we've constructed makes some sense. */
        if (RT_SUCCESS(rc))
        {
            rc = RTCrTafTrustAnchorInfo_CheckSanity(&TrustAnchor, 0, RTErrInfoInitStatic(&StaticErrInfo), "TAI");
            if (RT_FAILURE(rc))
                RTMsgError("RTCrTafTrustAnchorInfo_CheckSanity failed: %Rrc - %s", rc, StaticErrInfo.szMsg);
        }

        if (RT_SUCCESS(rc))
        {
            /*
             * Encode it and write it to the output file.
             */
            uint32_t cbEncoded;
            rc = RTAsn1EncodePrepare(RTCrTafTrustAnchorInfo_GetAsn1Core(&TrustAnchor), RTASN1ENCODE_F_DER, &cbEncoded,
                                     RTErrInfoInitStatic(&StaticErrInfo));
            if (RT_SUCCESS(rc))
            {
                if (State.cVerbose >= 1)
                    RTAsn1Dump(RTCrTafTrustAnchorInfo_GetAsn1Core(&TrustAnchor), 0, 0, RTStrmDumpPrintfV, g_pStdOut);

                PRTSTREAM pStrm;
                rc = RTStrmOpen(State.pszOutput, "wb", &pStrm);
                if (RT_SUCCESS(rc))
                {
                    rc = RTAsn1EncodeWrite(RTCrTafTrustAnchorInfo_GetAsn1Core(&TrustAnchor), RTASN1ENCODE_F_DER,
                                           handleMakeTaInfoWriter, pStrm, RTErrInfoInitStatic(&StaticErrInfo));
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTStrmClose(pStrm);
                        if (RT_SUCCESS(rc))
                            RTMsgInfo("Successfully wrote TrustedAnchorInfo to '%s'.", State.pszOutput);
                        else
                            RTMsgError("RTStrmClose failed: %Rrc", rc);
                    }
                    else
                    {
                        RTMsgError("RTAsn1EncodeWrite failed: %Rrc - %s", rc, StaticErrInfo.szMsg);
                        RTStrmClose(pStrm);
                    }
                }
                else
                    RTMsgError("Error opening '%s' for writing: %Rrcs", State.pszOutput, rc);
            }
            else
                RTMsgError("RTAsn1EncodePrepare failed: %Rrc - %s", rc, StaticErrInfo.szMsg);
        }

        RTCrTafTrustAnchorInfo_Delete(&TrustAnchor);
    }
    else
        RTMsgError("RTCrTafTrustAnchorInfo_Init failed: %Rrc", rc);

    RTCrX509Certificate_Delete(&Certificate);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}



/*
 * The 'version' command.
 */
static RTEXITCODE HelpVersion(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmPrintf(pStrm, "version\n");
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleVersion(int cArgs, char **papszArgs)
{
    RT_NOREF_PV(cArgs); RT_NOREF_PV(papszArgs);
#ifndef IN_BLD_PROG  /* RTBldCfgVersion or RTBldCfgRevision in build time IPRT lib. */
    RTPrintf("%s\n", RTBldCfgVersion());
    return RTEXITCODE_SUCCESS;
#else
    return RTEXITCODE_FAILURE;
#endif
}



/**
 * Command mapping.
 */
static struct
{
    /** The command. */
    const char *pszCmd;
    /**
     * Handle the command.
     * @returns Program exit code.
     * @param   cArgs       Number of arguments.
     * @param   papszArgs   The argument vector, starting with the command name.
     */
    RTEXITCODE (*pfnHandler)(int cArgs, char **papszArgs);
    /**
     * Produce help.
     * @returns RTEXITCODE_SUCCESS to simplify handling '--help' in the handler.
     * @param   pStrm       Where to send help text.
     * @param   enmLevel    The level of the help information.
     */
    RTEXITCODE (*pfnHelp)(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel);
}
/** Mapping commands to handler and helper functions. */
const g_aCommands[] =
{
    { "extract-exe-signer-cert",        HandleExtractExeSignerCert,         HelpExtractExeSignerCert },
    { "add-nested-exe-signature",       HandleAddNestedExeSignature,        HelpAddNestedExeSignature },
    { "add-nested-cat-signature",       HandleAddNestedCatSignature,        HelpAddNestedCatSignature },
#ifndef IPRT_IN_BUILD_TOOL
    { "add-timestamp-exe-signature",    HandleAddTimestampExeSignature,     HelpAddTimestampExeSignature },
    { "sign-exe",                       HandleSignExe,                      HelpSignExe  },
#endif
#ifndef IPRT_IN_BUILD_TOOL
    { "verify-exe",                     HandleVerifyExe,                    HelpVerifyExe },
#endif
    { "show-exe",                       HandleShowExe,                      HelpShowExe },
    { "show-cat",                       HandleShowCat,                      HelpShowCat },
    { "make-tainfo",                    HandleMakeTaInfo,                   HelpMakeTaInfo },
    { "help",                           HandleHelp,                         HelpHelp },
    { "--help",                         HandleHelp,                         NULL },
    { "-h",                             HandleHelp,                         NULL },
    { "version",                        HandleVersion,                      HelpVersion },
    { "--version",                      HandleVersion,                      NULL },
    { "-V",                             HandleVersion,                      NULL },
};


/*
 * The 'help' command.
 */
static RTEXITCODE HelpHelp(PRTSTREAM pStrm, RTSIGNTOOLHELP enmLevel)
{
    RT_NOREF_PV(enmLevel);
    RTStrmPrintf(pStrm, "help [cmd-patterns]\n");
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE HandleHelp(int cArgs, char **papszArgs)
{
    RTSIGNTOOLHELP  enmLevel = cArgs <= 1 ? RTSIGNTOOLHELP_USAGE : RTSIGNTOOLHELP_FULL;
    uint32_t        cShowed  = 0;
    uint32_t        cchWidth;
    if (RT_FAILURE(RTStrmQueryTerminalWidth(g_pStdOut, &cchWidth)))
        cchWidth = 80;
    for (uint32_t iCmd = 0; iCmd < RT_ELEMENTS(g_aCommands); iCmd++)
    {
        if (g_aCommands[iCmd].pfnHelp)
        {
            bool fShow = false;
            if (cArgs <= 1)
                fShow = true;
            else
                for (int iArg = 1; iArg < cArgs; iArg++)
                    if (RTStrSimplePatternMultiMatch(papszArgs[iArg], RTSTR_MAX, g_aCommands[iCmd].pszCmd, RTSTR_MAX, NULL))
                    {
                        fShow = true;
                        break;
                    }
            if (fShow)
            {
                if (cShowed && enmLevel == RTSIGNTOOLHELP_FULL)
                    RTPrintf("%.*s\n", RT_MIN(cchWidth, 100),
                             "- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - ");
                g_aCommands[iCmd].pfnHelp(g_pStdOut, enmLevel);
                cShowed++;
            }
        }
    }
    return cShowed ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}



int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse global arguments.
     */
    int iArg = 1;
    /* none presently. */

    /*
     * Command dispatcher.
     */
    if (iArg < argc)
    {
        const char *pszCmd = argv[iArg];
        uint32_t i = RT_ELEMENTS(g_aCommands);
        while (i-- > 0)
            if (!strcmp(g_aCommands[i].pszCmd, pszCmd))
                return g_aCommands[i].pfnHandler(argc - iArg, &argv[iArg]);
        RTMsgError("Unknown command '%s'.", pszCmd);
    }
    else
        RTMsgError("No command given. (try --help)");

    return RTEXITCODE_SYNTAX;
}


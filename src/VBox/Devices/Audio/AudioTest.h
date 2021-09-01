/* $Id$ */
/** @file
 * Audio testing routines.
 * Common code which is being used by the ValidationKit audio test (VKAT)
 * and the debug / ValdikationKit audio driver(s).
 */

/*
 * Copyright (C) 2021 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef VBOX_INCLUDED_SRC_Audio_AudioTest_h
#define VBOX_INCLUDED_SRC_Audio_AudioTest_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @todo Some stuff here can be private-only to the implementation. */

/** Maximum length in characters an audio test tag can have. */
#define AUDIOTEST_TAG_MAX               64
/** Maximum length in characters a single audio test error description can have. */
#define AUDIOTEST_ERROR_DESC_MAX        128
/** Prefix for audio test (set) directories. */
#define AUDIOTEST_PATH_PREFIX_STR       "vkat"

/**
 * Enumeration for an audio test tone (wave) type.
 */
typedef enum AUDIOTESTTONETYPE
{
    /** Invalid type. */
    AUDIOTESTTONETYPE_INVALID = 0,
    /** Sine wave. */
    AUDIOTESTTONETYPE_SINE,
    /** Square wave. Not implemented yet. */
    AUDIOTESTTONETYPE_SQUARE,
    /** Triangluar wave. Not implemented yet. */
    AUDIOTESTTONETYPE_TRIANGLE,
    /** Sawtooth wave. Not implemented yet. */
    AUDIOTESTTONETYPE_SAWTOOTH,
    /** The usual 32-bit hack. */
    AUDIOTESTTONETYPE_32BIT_HACK = 0x7fffffff
} AUDIOTESTTONETYPE;

/**
 * Structure for handling an audio (sine wave) test tone.
 */
typedef struct AUDIOTESTTONE
{
    /** The tone's wave type. */
    AUDIOTESTTONETYPE   enmType;
    /** The PCM properties. */
    PDMAUDIOPCMPROPS    Props;
    /** Current sample index for generate the sine wave. */
    uint64_t            uSample;
    /** The fixed portion of the sin() input. */
    double              rdFixed;
    /** Frequency (in Hz) of the sine wave to generate. */
    double              rdFreqHz;
} AUDIOTESTTONE;
/** Pointer to an audio test tone. */
typedef AUDIOTESTTONE *PAUDIOTESTTONE;

/**
 * Structure for handling audio test tone parameters.
 */
typedef struct AUDIOTESTTONEPARMS
{
    /** The PCM properties. */
    PDMAUDIOPCMPROPS Props;
    /** Tone frequency (in Hz) to use.
     *  Will be later converted to a double value. */
    double           dbFreqHz;
    /** Prequel (in ms) to play silence. Optional and can be set to 0. */
    RTMSINTERVAL     msPrequel;
    /** Duration (in ms) to play the test tone. */
    RTMSINTERVAL     msDuration;
    /** Sequel (in ms) to play silence. Optional and can be set to 0. */
    RTMSINTERVAL     msSequel;
    /** Volume (in percent, 0-100) to use.
     *  If set to 0, the tone is muted (i.e. silent). */
    uint8_t          uVolumePercent;
} AUDIOTESTTONEPARMS;
/** Pointer to audio test tone parameters. */
typedef AUDIOTESTTONEPARMS *PAUDIOTESTTONEPARMS;

/**
 * Enumeration for the test set mode.
 */
typedef enum AUDIOTESTSETMODE
{
    /** Invalid test set mode. */
    AUDIOTESTSETMODE_INVALID = 0,
    /** Test set is being created (testing in progress). */
    AUDIOTESTSETMODE_TEST,
    /** Existing test set is being verified. */
    AUDIOTESTSETMODE_VERIFY,
    /** The usual 32-bit hack. */
    AUDIOTESTSETMODE_32BIT_HACK = 0x7fffffff
} AUDIOTESTSETMODE;

/**
 * Enumeration to specify an audio test type.
 */
typedef enum AUDIOTESTTYPE
{
    /** Invalid test type, do not use. */
    AUDIOTESTTYPE_INVALID = 0,
    /** Play a test tone. */
    AUDIOTESTTYPE_TESTTONE_PLAY,
    /** Record a test tone. */
    AUDIOTESTTYPE_TESTTONE_RECORD,
    /** The usual 32-bit hack. */
    AUDIOTESTTYPE_32BIT_HACK = 0x7fffffff
} AUDIOTESTTYPE;

/**
 * Audio test request data.
 */
typedef struct AUDIOTESTPARMS
{
    /** Specifies the current test iteration. */
    uint32_t                idxCurrent;
    /** How many iterations the test should be executed. */
    uint32_t                cIterations;
    /** PCM audio stream properties to use. */
    PDMAUDIOPCMPROPS        Props;
    /** Audio device to use. */
    PDMAUDIOHOSTDEV         Dev;
    /** How much to delay (wait, in ms) the test being executed. */
    RTMSINTERVAL            msDelay;
    /** The test direction. */
    PDMAUDIODIR             enmDir;
    /** The test type. */
    AUDIOTESTTYPE           enmType;
    /** Union for test type-specific data. */
    union
    {
        AUDIOTESTTONEPARMS  TestTone;
    };
} AUDIOTESTPARMS;
/** Pointer to a test parameter structure. */
typedef AUDIOTESTPARMS *PAUDIOTESTPARMS;

/** Test object handle. */
typedef R3R0PTRTYPE(struct AUDIOTESTOBJINT RT_FAR *)      AUDIOTESTOBJ;
/** Pointer to test object handle. */
typedef AUDIOTESTOBJ                              RT_FAR *PAUDIOTESTOBJ;
/** Nil test object handle. */
#define NIL_AUDIOTESTOBJ                                  ((AUDIOTESTOBJ)~(RTHCINTPTR)0)

struct AUDIOTESTSET;

/**
 * Structure specifying a single audio test entry of a test set.
 *
 * A test set can contain zero or more test entry (tests).
 */
typedef struct AUDIOTESTENTRY
{
    /** List node. */
    RTLISTNODE           Node;
    /** Pointer to test set parent. */
    AUDIOTESTSET        *pParent;
    /** Friendly description of the test. */
    char                 szDesc[64];
    /** Audio test parameters this test needs to perform the actual test. */
    AUDIOTESTPARMS       Parms;
    /** Number of test objects bound to this test. */
    uint32_t             cObj;
    /** Absolute offset (in bytes) where to write the "obj_count" value later. */
    uint64_t             offObjCount;
    /** Overall test result. */
    int                  rc;
} AUDIOTESTENTRY;
/** Pointer to an audio test entry. */
typedef AUDIOTESTENTRY *PAUDIOTESTENTRY;

/**
 * Structure specifying an audio test set.
 */
typedef struct AUDIOTESTSET
{
    /** The set's tag. */
    char             szTag[AUDIOTEST_TAG_MAX];
    /** Absolute path where to store the test audio data. */
    char             szPathAbs[RTPATH_MAX];
    /** Current mode the test set is in. */
    AUDIOTESTSETMODE enmMode;
    union
    {
        /** @todo r=bird: RTSTREAM not RTFILE.  That means you don't have to check
         *        every write status code and it's buffered and thus faster.  Also,
         *        you don't have to re-invent fprintf-style RTFileWrite wrappers. */
        RTFILE       hFile;
        RTINIFILE    hIniFile;
    } f;
    /** Number of test objects in lstObj. */
    uint32_t         cObj;
    /** Absolute offset (in bytes) where to write the "obj_count" value later. */
    uint64_t         offObjCount;
    /** List containing PAUDIOTESTOBJ test object entries. */
    RTLISTANCHOR     lstObj;
    /** Number of performed tests.
     *  Not necessarily bound to the test object entries above. */
    uint32_t         cTests;
    /** Absolute offset (in bytes) where to write the "test_count" value later. */
    uint64_t         offTestCount;
    /** List containing PAUDIOTESTENTRY test entries. */
    RTLISTANCHOR     lstTest;
    /** Current test running. Can be NULL if no test is running. */
    PAUDIOTESTENTRY  pTestCur;
    /** Number of tests currently running.
     *  Currently we only allow one concurrent test running at a given time. */
    uint32_t         cTestsRunning;
    /** Number of total (test) failures. */
    uint32_t         cTotalFailures;
} AUDIOTESTSET;
/** Pointer to an audio test set. */
typedef AUDIOTESTSET *PAUDIOTESTSET;

/**
 * Structure for holding a single audio test error entry.
 */
typedef struct AUDIOTESTERRORENTRY
{
    /** The entrie's list node. */
    RTLISTNODE       Node;
    /** Additional rc. */
    int              rc;
    /** Actual error description. */
    char             szDesc[AUDIOTEST_ERROR_DESC_MAX];
} AUDIOTESTERRORENTRY;
/** Pointer to an audio test error description. */
typedef AUDIOTESTERRORENTRY *PAUDIOTESTERRORENTRY;

/**
 * Structure for holding an audio test error description.
 * This can contain multiple errors (FIFO list).
 */
typedef struct AUDIOTESTERRORDESC
{
    /** List entries containing the (FIFO-style) errors of type AUDIOTESTERRORENTRY. */
    RTLISTANCHOR     List;
    /** Number of errors in the list. */
    uint32_t         cErrors;
} AUDIOTESTERRORDESC;
/** Pointer to an audio test error description. */
typedef AUDIOTESTERRORDESC *PAUDIOTESTERRORDESC;
/** Const pointer to an audio test error description. */
typedef AUDIOTESTERRORDESC const *PCAUDIOTESTERRORDESC;

double AudioTestToneInit(PAUDIOTESTTONE pTone, PPDMAUDIOPCMPROPS pProps, double dbFreq);
double AudioTestToneInitRandom(PAUDIOTESTTONE pTone, PPDMAUDIOPCMPROPS pProps);
double AudioTestToneGetRandomFreq(void);
int    AudioTestToneGenerate(PAUDIOTESTTONE pTone, void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten);

int    AudioTestGenTag(char *pszTag, size_t cbTag);

int    AudioTestPathGetTemp(char *pszPath, size_t cbPath);
int    AudioTestPathCreateTemp(char *pszPath, size_t cbPath, const char *pszUUID);
int    AudioTestPathCreate(char *pszPath, size_t cbPath, const char *pszUUID);

int    AudioTestSetObjCreateAndRegister(PAUDIOTESTSET pSet, const char *pszName, PAUDIOTESTOBJ pObj);

int    AudioTestObjWrite(AUDIOTESTOBJ Obj, const void *pvBuf, size_t cbBuf);
int    AudioTestObjAddMetadataStr(AUDIOTESTOBJ Obj, const char *pszFormat, ...);
int    AudioTestObjClose(AUDIOTESTOBJ Obj);

int    AudioTestSetTestBegin(PAUDIOTESTSET pSet, const char *pszDesc, PAUDIOTESTPARMS pParms, PAUDIOTESTENTRY *ppEntry);
int    AudioTestSetTestFailed(PAUDIOTESTENTRY pEntry, int rc, const char *pszErr);
int    AudioTestSetTestDone(PAUDIOTESTENTRY pEntry);
bool   AudioTestSetTestIsRunning(PAUDIOTESTENTRY pEntry);

int    AudioTestSetCreate(PAUDIOTESTSET pSet, const char *pszPath, const char *pszTag);
int    AudioTestSetDestroy(PAUDIOTESTSET pSet);
int    AudioTestSetOpen(PAUDIOTESTSET pSet, const char *pszPath);
int    AudioTestSetClose(PAUDIOTESTSET pSet);
int    AudioTestSetWipe(PAUDIOTESTSET pSet);
const char *AudioTestSetGetTag(PAUDIOTESTSET pSet);
uint32_t AudioTestSetGetTestsTotal(PAUDIOTESTSET pSet);
uint32_t AudioTestSetGetTestsRunning(PAUDIOTESTSET pSet);
uint32_t AudioTestSetGetTotalFailures(PAUDIOTESTSET pSet);
bool   AudioTestSetIsPacked(const char *pszPath);
bool   AudioTestSetIsRunning(PAUDIOTESTSET pSet);
int    AudioTestSetPack(PAUDIOTESTSET pSet, const char *pszOutDir, char *pszFileName, size_t cbFileName);
int    AudioTestSetUnpack(const char *pszFile, const char *pszOutDir);
int    AudioTestSetVerify(PAUDIOTESTSET pSetA, PAUDIOTESTSET pSetB, PAUDIOTESTERRORDESC pErrDesc);

uint32_t AudioTestErrorDescCount(PCAUDIOTESTERRORDESC pErr);
bool   AudioTestErrorDescFailed(PCAUDIOTESTERRORDESC pErr);

void   AudioTestErrorDescDestroy(PAUDIOTESTERRORDESC pErr);

/** @name Wave File Accessors
 * @{ */
/**
 * An open wave (.WAV) file.
 */
typedef struct AUDIOTESTWAVEFILE
{
    /** Magic value (AUDIOTESTWAVEFILE_MAGIC). */
    uint32_t            u32Magic;
    /** Set if we're in read-mode, clear if in write mode. */
    bool                fReadMode;
    /** The file handle. */
    RTFILE              hFile;
    /** The absolute file offset of the first sample */
    uint32_t            offSamples;
    /** Number of bytes of samples. */
    uint32_t            cbSamples;
    /** The current read position relative to @a offSamples.  */
    uint32_t            offCur;
    /** The PCM properties for the file format.  */
    PDMAUDIOPCMPROPS    Props;
} AUDIOTESTWAVEFILE;
/** Pointer to an open wave file. */
typedef AUDIOTESTWAVEFILE *PAUDIOTESTWAVEFILE;

/** Magic value for AUDIOTESTWAVEFILE::u32Magic (Miles Dewey Davis III). */
#define AUDIOTESTWAVEFILE_MAGIC         UINT32_C(0x19260526)
/** Magic value for AUDIOTESTWAVEFILE::u32Magic after closing. */
#define AUDIOTESTWAVEFILE_MAGIC_DEAD    UINT32_C(0x19910928)

int    AudioTestWaveFileOpen(const char *pszFile, PAUDIOTESTWAVEFILE pWaveFile, PRTERRINFO pErrInfo);
int    AudioTestWaveFileCreate(const char *pszFile, PCPDMAUDIOPCMPROPS pProps, PAUDIOTESTWAVEFILE pWaveFile, PRTERRINFO pErrInfo);
int    AudioTestWaveFileRead(PAUDIOTESTWAVEFILE pWaveFile, void *pvBuf, size_t cbBuf, size_t *pcbRead);
int    AudioTestWaveFileWrite(PAUDIOTESTWAVEFILE pWaveFile, const void *pvBuf, size_t cbBuf);
int    AudioTestWaveFileClose(PAUDIOTESTWAVEFILE pWaveFile);

/** @} */

#endif /* !VBOX_INCLUDED_SRC_Audio_AudioTest_h */


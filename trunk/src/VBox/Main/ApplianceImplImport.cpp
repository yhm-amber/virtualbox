/* $Id$ */
/** @file
 *
 * IAppliance and IVirtualSystem COM class implementations.
 */

/*
 * Copyright (C) 2008-2010 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#include <iprt/stream.h>
#include <iprt/path.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/s3.h>
#include <iprt/sha.h>
#include <iprt/manifest.h>

#include <VBox/param.h>
#include <VBox/version.h>

#include "ApplianceImpl.h"
#include "VFSExplorerImpl.h"
#include "VirtualBoxImpl.h"
#include "GuestOSTypeImpl.h"
#include "ProgressImpl.h"
#include "MachineImpl.h"
#include "MediumImpl.h"

#include "HostNetworkInterfaceImpl.h"

#include "AutoCaller.h"
#include "Logging.h"

#include "ApplianceImplPrivate.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
//
// IAppliance public methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Public method implementation.
 * @return
 */
STDMETHODIMP Appliance::Interpret()
{
    // @todo:
    //  - don't use COM methods but the methods directly (faster, but needs appropriate locking of that objects itself (s. HardDisk))
    //  - Appropriate handle errors like not supported file formats
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!isApplianceIdle())
        return E_ACCESSDENIED;

    HRESULT rc = S_OK;

    /* Clear any previous virtual system descriptions */
    m->virtualSystemDescriptions.clear();

    /* We need the default path for storing disk images */
    ComPtr<ISystemProperties> systemProps;
    rc = mVirtualBox->COMGETTER(SystemProperties)(systemProps.asOutParam());
    if (FAILED(rc)) return rc;
    Bstr bstrDefaultHardDiskLocation;
    rc = systemProps->COMGETTER(DefaultHardDiskFolder)(bstrDefaultHardDiskLocation.asOutParam());
    if (FAILED(rc)) return rc;

    if (!m->pReader)
        return setError(E_FAIL,
                        tr("Cannot interpret appliance without reading it first (call read() before interpret())"));

    // Change the appliance state so we can safely leave the lock while doing time-consuming
    // disk imports; also the below method calls do all kinds of locking which conflicts with
    // the appliance object lock
    m->state = Data::ApplianceImporting;
    alock.release();

    /* Try/catch so we can clean up on error */
    try
    {
        list<VirtualSystem>::const_iterator it;
        /* Iterate through all virtual systems */
        for (it = m->pReader->m_llVirtualSystems.begin();
             it != m->pReader->m_llVirtualSystems.end();
             ++it)
        {
            const VirtualSystem &vsysThis = *it;

            ComObjPtr<VirtualSystemDescription> pNewDesc;
            rc = pNewDesc.createObject();
            if (FAILED(rc)) throw rc;
            rc = pNewDesc->init();
            if (FAILED(rc)) throw rc;

            /* Guest OS type */
            Utf8Str strOsTypeVBox,
                    strCIMOSType = Utf8StrFmt("%RI32", (uint32_t)vsysThis.cimos);
            convertCIMOSType2VBoxOSType(strOsTypeVBox, vsysThis.cimos, vsysThis.strCimosDesc);
            pNewDesc->addEntry(VirtualSystemDescriptionType_OS,
                               "",
                               strCIMOSType,
                               strOsTypeVBox);

            /* VM name */
            /* If the there isn't any name specified create a default one out of
             * the OS type */
            Utf8Str nameVBox = vsysThis.strName;
            if (nameVBox.isEmpty())
                nameVBox = strOsTypeVBox;
            searchUniqueVMName(nameVBox);
            pNewDesc->addEntry(VirtualSystemDescriptionType_Name,
                               "",
                               vsysThis.strName,
                               nameVBox);

            /* VM Product */
            if (!vsysThis.strProduct.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_Product,
                                    "",
                                    vsysThis.strProduct,
                                    vsysThis.strProduct);

            /* VM Vendor */
            if (!vsysThis.strVendor.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_Vendor,
                                    "",
                                    vsysThis.strVendor,
                                    vsysThis.strVendor);

            /* VM Version */
            if (!vsysThis.strVersion.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_Version,
                                    "",
                                    vsysThis.strVersion,
                                    vsysThis.strVersion);

            /* VM ProductUrl */
            if (!vsysThis.strProductUrl.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_ProductUrl,
                                    "",
                                    vsysThis.strProductUrl,
                                    vsysThis.strProductUrl);

            /* VM VendorUrl */
            if (!vsysThis.strVendorUrl.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_VendorUrl,
                                    "",
                                    vsysThis.strVendorUrl,
                                    vsysThis.strVendorUrl);

            /* VM description */
            if (!vsysThis.strDescription.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_Description,
                                    "",
                                    vsysThis.strDescription,
                                    vsysThis.strDescription);

            /* VM license */
            if (!vsysThis.strLicenseText.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_License,
                                    "",
                                    vsysThis.strLicenseText,
                                    vsysThis.strLicenseText);

            /* Now that we know the OS type, get our internal defaults based on that. */
            ComPtr<IGuestOSType> pGuestOSType;
            rc = mVirtualBox->GetGuestOSType(Bstr(strOsTypeVBox), pGuestOSType.asOutParam());
            if (FAILED(rc)) throw rc;

            /* CPU count */
            ULONG cpuCountVBox = vsysThis.cCPUs;
            /* Check for the constrains */
            if (cpuCountVBox > SchemaDefs::MaxCPUCount)
            {
                addWarning(tr("The virtual system \"%s\" claims support for %u CPU's, but VirtualBox has support for max %u CPU's only."),
                           vsysThis.strName.c_str(), cpuCountVBox, SchemaDefs::MaxCPUCount);
                cpuCountVBox = SchemaDefs::MaxCPUCount;
            }
            if (vsysThis.cCPUs == 0)
                cpuCountVBox = 1;
            pNewDesc->addEntry(VirtualSystemDescriptionType_CPU,
                               "",
                               Utf8StrFmt("%RI32", (uint32_t)vsysThis.cCPUs),
                               Utf8StrFmt("%RI32", (uint32_t)cpuCountVBox));

            /* RAM */
            uint64_t ullMemSizeVBox = vsysThis.ullMemorySize / _1M;
            /* Check for the constrains */
            if (ullMemSizeVBox != 0 &&
                (ullMemSizeVBox < MM_RAM_MIN_IN_MB ||
                 ullMemSizeVBox > MM_RAM_MAX_IN_MB))
            {
                addWarning(tr("The virtual system \"%s\" claims support for %llu MB RAM size, but VirtualBox has support for min %u & max %u MB RAM size only."),
                              vsysThis.strName.c_str(), ullMemSizeVBox, MM_RAM_MIN_IN_MB, MM_RAM_MAX_IN_MB);
                ullMemSizeVBox = RT_MIN(RT_MAX(ullMemSizeVBox, MM_RAM_MIN_IN_MB), MM_RAM_MAX_IN_MB);
            }
            if (vsysThis.ullMemorySize == 0)
            {
                /* If the RAM of the OVF is zero, use our predefined values */
                ULONG memSizeVBox2;
                rc = pGuestOSType->COMGETTER(RecommendedRAM)(&memSizeVBox2);
                if (FAILED(rc)) throw rc;
                /* VBox stores that in MByte */
                ullMemSizeVBox = (uint64_t)memSizeVBox2;
            }
            pNewDesc->addEntry(VirtualSystemDescriptionType_Memory,
                               "",
                               Utf8StrFmt("%RI64", (uint64_t)vsysThis.ullMemorySize),
                               Utf8StrFmt("%RI64", (uint64_t)ullMemSizeVBox));

            /* Audio */
            if (!vsysThis.strSoundCardType.isEmpty())
                /* Currently we set the AC97 always.
                   @todo: figure out the hardware which could be possible */
                pNewDesc->addEntry(VirtualSystemDescriptionType_SoundCard,
                                   "",
                                   vsysThis.strSoundCardType,
                                   Utf8StrFmt("%RI32", (uint32_t)AudioControllerType_AC97));

#ifdef VBOX_WITH_USB
            /* USB Controller */
            if (vsysThis.fHasUsbController)
                pNewDesc->addEntry(VirtualSystemDescriptionType_USBController, "", "", "");
#endif /* VBOX_WITH_USB */

            /* Network Controller */
            size_t cEthernetAdapters = vsysThis.llEthernetAdapters.size();
            if (cEthernetAdapters > 0)
            {
                /* Check for the constrains */
                if (cEthernetAdapters > SchemaDefs::NetworkAdapterCount)
                    addWarning(tr("The virtual system \"%s\" claims support for %zu network adapters, but VirtualBox has support for max %u network adapter only."),
                                  vsysThis.strName.c_str(), cEthernetAdapters, SchemaDefs::NetworkAdapterCount);

                /* Get the default network adapter type for the selected guest OS */
                NetworkAdapterType_T defaultAdapterVBox = NetworkAdapterType_Am79C970A;
                rc = pGuestOSType->COMGETTER(AdapterType)(&defaultAdapterVBox);
                if (FAILED(rc)) throw rc;

                EthernetAdaptersList::const_iterator itEA;
                /* Iterate through all abstract networks. We support 8 network
                 * adapters at the maximum, so the first 8 will be added only. */
                size_t a = 0;
                for (itEA = vsysThis.llEthernetAdapters.begin();
                     itEA != vsysThis.llEthernetAdapters.end() && a < SchemaDefs::NetworkAdapterCount;
                     ++itEA, ++a)
                {
                    const EthernetAdapter &ea = *itEA; // logical network to connect to
                    Utf8Str strNetwork = ea.strNetworkName;
                    // make sure it's one of these two
                    if (    (strNetwork.compare("Null", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("NAT", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("Bridged", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("Internal", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("HostOnly", Utf8Str::CaseInsensitive))
                       )
                        strNetwork = "Bridged";     // VMware assumes this is the default apparently

                    /* Figure out the hardware type */
                    NetworkAdapterType_T nwAdapterVBox = defaultAdapterVBox;
                    if (!ea.strAdapterType.compare("PCNet32", Utf8Str::CaseInsensitive))
                    {
                        /* If the default adapter is already one of the two
                         * PCNet adapters use the default one. If not use the
                         * Am79C970A as fallback. */
                        if (!(defaultAdapterVBox == NetworkAdapterType_Am79C970A ||
                              defaultAdapterVBox == NetworkAdapterType_Am79C973))
                            nwAdapterVBox = NetworkAdapterType_Am79C970A;
                    }
#ifdef VBOX_WITH_E1000
                    /* VMWare accidentally write this with VirtualCenter 3.5,
                       so make sure in this case always to use the VMWare one */
                    else if (!ea.strAdapterType.compare("E10000", Utf8Str::CaseInsensitive))
                        nwAdapterVBox = NetworkAdapterType_I82545EM;
                    else if (!ea.strAdapterType.compare("E1000", Utf8Str::CaseInsensitive))
                    {
                        /* Check if this OVF was written by VirtualBox */
                        if (Utf8Str(vsysThis.strVirtualSystemType).contains("virtualbox", Utf8Str::CaseInsensitive))
                        {
                            /* If the default adapter is already one of the three
                             * E1000 adapters use the default one. If not use the
                             * I82545EM as fallback. */
                            if (!(defaultAdapterVBox == NetworkAdapterType_I82540EM ||
                                  defaultAdapterVBox == NetworkAdapterType_I82543GC ||
                                  defaultAdapterVBox == NetworkAdapterType_I82545EM))
                            nwAdapterVBox = NetworkAdapterType_I82540EM;
                        }
                        else
                            /* Always use this one since it's what VMware uses */
                            nwAdapterVBox = NetworkAdapterType_I82545EM;
                    }
#endif /* VBOX_WITH_E1000 */

                    pNewDesc->addEntry(VirtualSystemDescriptionType_NetworkAdapter,
                                       "",      // ref
                                       ea.strNetworkName,      // orig
                                       Utf8StrFmt("%RI32", (uint32_t)nwAdapterVBox),   // conf
                                       0,
                                       Utf8StrFmt("type=%s", strNetwork.c_str()));       // extra conf
                }
            }

            /* Floppy Drive */
            if (vsysThis.fHasFloppyDrive)
                pNewDesc->addEntry(VirtualSystemDescriptionType_Floppy, "", "", "");

            /* CD Drive */
            if (vsysThis.fHasCdromDrive)
                pNewDesc->addEntry(VirtualSystemDescriptionType_CDROM, "", "", "");

            /* Hard disk Controller */
            uint16_t cIDEused = 0;
            uint16_t cSATAused = 0; NOREF(cSATAused);
            uint16_t cSCSIused = 0; NOREF(cSCSIused);
            ControllersMap::const_iterator hdcIt;
            /* Iterate through all hard disk controllers */
            for (hdcIt = vsysThis.mapControllers.begin();
                 hdcIt != vsysThis.mapControllers.end();
                 ++hdcIt)
            {
                const HardDiskController &hdc = hdcIt->second;
                Utf8Str strControllerID = Utf8StrFmt("%RI32", (uint32_t)hdc.idController);

                switch (hdc.system)
                {
                    case HardDiskController::IDE:
                        {
                            /* Check for the constrains */
                            /* @todo: I'm very confused! Are these bits *one* controller or
                               is every port/bus declared as an extra controller. */
                            if (cIDEused < 4)
                            {
                                // @todo: figure out the IDE types
                                /* Use PIIX4 as default */
                                Utf8Str strType = "PIIX4";
                                if (!hdc.strControllerType.compare("PIIX3", Utf8Str::CaseInsensitive))
                                    strType = "PIIX3";
                                else if (!hdc.strControllerType.compare("ICH6", Utf8Str::CaseInsensitive))
                                    strType = "ICH6";
                                pNewDesc->addEntry(VirtualSystemDescriptionType_HardDiskControllerIDE,
                                                   strControllerID,
                                                   hdc.strControllerType,
                                                   strType);
                            }
                            else
                            {
                                /* Warn only once */
                                if (cIDEused == 1)
                                    addWarning(tr("The virtual \"%s\" system requests support for more than one IDE controller, but VirtualBox has support for only one."),
                                               vsysThis.strName.c_str());

                            }
                            ++cIDEused;
                            break;
                        }

                    case HardDiskController::SATA:
                        {
#ifdef VBOX_WITH_AHCI
                            /* Check for the constrains */
                            if (cSATAused < 1)
                            {
                                // @todo: figure out the SATA types
                                /* We only support a plain AHCI controller, so use them always */
                                pNewDesc->addEntry(VirtualSystemDescriptionType_HardDiskControllerSATA,
                                                   strControllerID,
                                                   hdc.strControllerType,
                                                   "AHCI");
                            }
                            else
                            {
                                /* Warn only once */
                                if (cSATAused == 1)
                                    addWarning(tr("The virtual system \"%s\" requests support for more than one SATA controller, but VirtualBox has support for only one"),
                                               vsysThis.strName.c_str());

                            }
                            ++cSATAused;
                            break;
#else /* !VBOX_WITH_AHCI */
                            addWarning(tr("The virtual system \"%s\" requests at least one SATA controller but this version of VirtualBox does not provide a SATA controller emulation"),
                                      vsysThis.strName.c_str());
#endif /* !VBOX_WITH_AHCI */
                        }

                    case HardDiskController::SCSI:
                        {
#ifdef VBOX_WITH_LSILOGIC
                            /* Check for the constrains */
                            if (cSCSIused < 1)
                            {
                                Utf8Str hdcController = "LsiLogic";
                                if (!hdc.strControllerType.compare("BusLogic", Utf8Str::CaseInsensitive))
                                    hdcController = "BusLogic";
                                pNewDesc->addEntry(VirtualSystemDescriptionType_HardDiskControllerSCSI,
                                                   strControllerID,
                                                   hdc.strControllerType,
                                                   hdcController);
                            }
                            else
                                addWarning(tr("The virtual system \"%s\" requests support for an additional SCSI controller of type \"%s\" with ID %s, but VirtualBox presently supports only one SCSI controller."),
                                           vsysThis.strName.c_str(),
                                           hdc.strControllerType.c_str(),
                                           strControllerID.c_str());
                            ++cSCSIused;
                            break;
#else /* !VBOX_WITH_LSILOGIC */
                            addWarning(tr("The virtual system \"%s\" requests at least one SATA controller but this version of VirtualBox does not provide a SCSI controller emulation"),
                                       vsysThis.strName.c_str());
#endif /* !VBOX_WITH_LSILOGIC */
                        }
                }
            }

            /* Hard disks */
            if (vsysThis.mapVirtualDisks.size() > 0)
            {
                VirtualDisksMap::const_iterator itVD;
                /* Iterate through all hard disks ()*/
                for (itVD = vsysThis.mapVirtualDisks.begin();
                     itVD != vsysThis.mapVirtualDisks.end();
                     ++itVD)
                {
                    const VirtualDisk &hd = itVD->second;
                    /* Get the associated disk image */
                    const DiskImage &di = m->pReader->m_mapDisks[hd.strDiskId];

                    // @todo:
                    //  - figure out all possible vmdk formats we also support
                    //  - figure out if there is a url specifier for vhd already
                    //  - we need a url specifier for the vdi format
                    if (   di.strFormat.compare("http://www.vmware.com/specifications/vmdk.html#sparse", Utf8Str::CaseInsensitive)
                        || di.strFormat.compare("http://www.vmware.com/specifications/vmdk.html#compressed", Utf8Str::CaseInsensitive))
                    {
                        /* If the href is empty use the VM name as filename */
                        Utf8Str strFilename = di.strHref;
                        if (!strFilename.length())
                            strFilename = Utf8StrFmt("%s.vmdk", nameVBox.c_str());
                        /* Construct a unique target path */
                        Utf8StrFmt strPath("%ls%c%s",
                                           bstrDefaultHardDiskLocation.raw(),
                                           RTPATH_DELIMITER,
                                           strFilename.c_str());
                        searchUniqueDiskImageFilePath(strPath);

                        /* find the description for the hard disk controller
                         * that has the same ID as hd.idController */
                        const VirtualSystemDescriptionEntry *pController;
                        if (!(pController = pNewDesc->findControllerFromID(hd.idController)))
                            throw setError(E_FAIL,
                                           tr("Cannot find hard disk controller with OVF instance ID %RI32 to which disk \"%s\" should be attached"),
                                           hd.idController,
                                           di.strHref.c_str());

                        /* controller to attach to, and the bus within that controller */
                        Utf8StrFmt strExtraConfig("controller=%RI16;channel=%RI16",
                                                  pController->ulIndex,
                                                  hd.ulAddressOnParent);
                        ULONG ulSize = 0;
                        if (di.iCapacity != -1)
                            ulSize = (ULONG)(di.iCapacity / _1M);
                        else if (di.iPopulatedSize != -1)
                            ulSize = (ULONG)(di.iPopulatedSize / _1M);
                        else if (di.iSize != -1)
                            ulSize = (ULONG)(di.iSize / _1M);
                        if (ulSize == 0)
                            ulSize = 10000;         // assume 10 GB, this is for the progress bar only anyway
                        pNewDesc->addEntry(VirtualSystemDescriptionType_HardDiskImage,
                                           hd.strDiskId,
                                           di.strHref,
                                           strPath,
                                           ulSize,
                                           strExtraConfig);
                    }
                    else
                        throw setError(VBOX_E_FILE_ERROR,
                                       tr("Unsupported format for virtual disk image in OVF: \"%s\"", di.strFormat.c_str()));
                }
            }

            m->virtualSystemDescriptions.push_back(pNewDesc);
        }
    }
    catch (HRESULT aRC)
    {
        /* On error we clear the list & return */
        m->virtualSystemDescriptions.clear();
        rc = aRC;
    }

    // reset the appliance state
    alock.acquire();
    m->state = Data::ApplianceIdle;

    return rc;
}

/**
 * Public method implementation.
 * @param aProgress
 * @return
 */
STDMETHODIMP Appliance::ImportMachines(IProgress **aProgress)
{
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    // do not allow entering this method if the appliance is busy reading or writing
    if (!isApplianceIdle())
        return E_ACCESSDENIED;

    if (!m->pReader)
        return setError(E_FAIL,
                        tr("Cannot import machines without reading it first (call read() before importMachines())"));

    ComObjPtr<Progress> progress;
    HRESULT rc = S_OK;
    try
    {
        rc = importImpl(m->locInfo, progress);
    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

    if (SUCCEEDED(rc))
        /* Return progress to the caller */
        progress.queryInterfaceTo(aProgress);

    return rc;
}

////////////////////////////////////////////////////////////////////////////////
//
// Appliance private methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 *
 * @param aLocInfo
 * @param aProgress
 * @return
 */
HRESULT Appliance::readImpl(const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress)
{
    /* Initialize our worker task */
    std::auto_ptr<TaskImportOVF> task(new TaskImportOVF(this));
    /* What should the task do */
    task->taskType = TaskImportOVF::Read;
    /* Copy the current location info to the task */
    task->locInfo = aLocInfo;

    BstrFmt bstrDesc = BstrFmt(tr("Read appliance '%s'"),
                               aLocInfo.strPath.c_str());
    HRESULT rc;
    /* Create the progress object */
    aProgress.createObject();
    if (task->locInfo.storageType == VFSType_File)
    {
        /* 1 operation only */
        rc = aProgress->init(mVirtualBox, static_cast<IAppliance*>(this),
                             bstrDesc,
                             TRUE /* aCancelable */);
    }
    else
    {
        /* 4/5 is downloading, 1/5 is reading */
        rc = aProgress->init(mVirtualBox, static_cast<IAppliance*>(this),
                             bstrDesc,
                             TRUE /* aCancelable */,
                             2, // ULONG cOperations,
                             5, // ULONG ulTotalOperationsWeight,
                             BstrFmt(tr("Download appliance '%s'"),
                                     aLocInfo.strPath.c_str()), // CBSTR bstrFirstOperationDescription,
                             4); // ULONG ulFirstOperationWeight,
    }
    if (FAILED(rc)) throw rc;

    task->progress = aProgress;

    rc = task->startThread();
    if (FAILED(rc)) throw rc;

    /* Don't destruct on success */
    task.release();

    return rc;
}

/**
 *
 * @param pTask
 * @return
 */
int Appliance::readFS(TaskImportOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;

    try
    {
        /* Read & parse the XML structure of the OVF file */
        m->pReader = new OVFReader(pTask->locInfo.strPath);
        /* Create the SHA1 sum of the OVF file for later validation */
        char *pszDigest;
        int vrc = RTSha1Digest(pTask->locInfo.strPath.c_str(), &pszDigest);
        if (RT_FAILURE(vrc))
            throw setError(VBOX_E_FILE_ERROR,
                           tr("Couldn't calculate SHA1 digest for file '%s' (%Rrc)"),
                           RTPathFilename(pTask->locInfo.strPath.c_str()), vrc);
        m->strOVFSHA1Digest = pszDigest;
        RTStrFree(pszDigest);
    }
    catch(xml::Error &x)
    {
        rc = setError(VBOX_E_FILE_ERROR,
                      x.what());
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }

    pTask->rc = rc;

    if (!pTask->progress.isNull())
        pTask->progress->notifyComplete(rc);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}

/**
 *
 * @param pTask
 * @return
 */
int Appliance::readS3(TaskImportOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;
    int vrc = VINF_SUCCESS;
    RTS3 hS3 = NIL_RTS3;
    char szOSTmpDir[RTPATH_MAX];
    RTPathTemp(szOSTmpDir, sizeof(szOSTmpDir));
    /* The template for the temporary directory created below */
    char *pszTmpDir;
    RTStrAPrintf(&pszTmpDir, "%s"RTPATH_SLASH_STR"vbox-ovf-XXXXXX", szOSTmpDir);
    list< pair<Utf8Str, ULONG> > filesList;
    Utf8Str strTmpOvf;

    try
    {
        /* Extract the bucket */
        Utf8Str tmpPath = pTask->locInfo.strPath;
        Utf8Str bucket;
        parseBucket(tmpPath, bucket);

        /* We need a temporary directory which we can put the OVF file & all
         * disk images in */
        vrc = RTDirCreateTemp(pszTmpDir);
        if (RT_FAILURE(vrc))
            throw setError(VBOX_E_FILE_ERROR,
                           tr("Cannot create temporary directory '%s'"), pszTmpDir);

        /* The temporary name of the target OVF file */
        strTmpOvf = Utf8StrFmt("%s/%s", pszTmpDir, RTPathFilename(tmpPath.c_str()));

        /* Next we have to download the OVF */
        vrc = RTS3Create(&hS3, pTask->locInfo.strUsername.c_str(), pTask->locInfo.strPassword.c_str(), pTask->locInfo.strHostname.c_str(), "virtualbox-agent/"VBOX_VERSION_STRING);
        if(RT_FAILURE(vrc))
            throw setError(VBOX_E_IPRT_ERROR,
                           tr("Cannot create S3 service handler"));
        RTS3SetProgressCallback(hS3, pTask->updateProgress, &pTask);

        /* Get it */
        char *pszFilename = RTPathFilename(strTmpOvf.c_str());
        vrc = RTS3GetKey(hS3, bucket.c_str(), pszFilename, strTmpOvf.c_str());
        if (RT_FAILURE(vrc))
        {
            if(vrc == VERR_S3_CANCELED)
                throw S_OK; /* todo: !!!!!!!!!!!!! */
            else if(vrc == VERR_S3_ACCESS_DENIED)
                throw setError(E_ACCESSDENIED,
                               tr("Cannot download file '%s' from S3 storage server (Access denied). Make sure that your credentials are right. Also check that your host clock is properly synced"), pszFilename);
            else if(vrc == VERR_S3_NOT_FOUND)
                throw setError(VBOX_E_FILE_ERROR,
                               tr("Cannot download file '%s' from S3 storage server (File not found)"), pszFilename);
            else
                throw setError(VBOX_E_IPRT_ERROR,
                               tr("Cannot download file '%s' from S3 storage server (%Rrc)"), pszFilename, vrc);
        }

        /* Close the connection early */
        RTS3Destroy(hS3);
        hS3 = NIL_RTS3;

        if (!pTask->progress.isNull())
            pTask->progress->SetNextOperation(Bstr(tr("Reading")), 1);

        /* Prepare the temporary reading of the OVF */
        ComObjPtr<Progress> progress;
        LocationInfo li;
        li.strPath = strTmpOvf;
        /* Start the reading from the fs */
        rc = readImpl(li, progress);
        if (FAILED(rc)) throw rc;

        /* Unlock the appliance for the reading thread */
        appLock.release();
        /* Wait until the reading is done, but report the progress back to the
           caller */
        ComPtr<IProgress> progressInt(progress);
        waitForAsyncProgress(pTask->progress, progressInt); /* Any errors will be thrown */

        /* Again lock the appliance for the next steps */
        appLock.acquire();
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }
    /* Cleanup */
    RTS3Destroy(hS3);
    /* Delete all files which where temporary created */
    if (RTPathExists(strTmpOvf.c_str()))
    {
        vrc = RTFileDelete(strTmpOvf.c_str());
        if(RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Cannot delete file '%s' (%Rrc)"), strTmpOvf.c_str(), vrc);
    }
    /* Delete the temporary directory */
    if (RTPathExists(pszTmpDir))
    {
        vrc = RTDirRemove(pszTmpDir);
        if(RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Cannot delete temporary directory '%s' (%Rrc)"), pszTmpDir, vrc);
    }
    if (pszTmpDir)
        RTStrFree(pszTmpDir);

    pTask->rc = rc;

    if (!pTask->progress.isNull())
        pTask->progress->notifyComplete(rc);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}

/**
 * Helper that converts VirtualSystem attachment values into VirtualBox attachment values.
 * Throws HRESULT values on errors!
 *
 * @param hdc
 * @param vd
 * @param mhda
 */
void Appliance::convertDiskAttachmentValues(const HardDiskController &hdc,
                                            uint32_t ulAddressOnParent,
                                            Bstr &controllerType,
                                            int32_t &lChannel,
                                            int32_t &lDevice)
{
    switch (hdc.system)
    {
        case HardDiskController::IDE:
            // For the IDE bus, the channel parameter can be either 0 or 1, to specify the primary
            // or secondary IDE controller, respectively. For the primary controller of the IDE bus,
            // the device number can be either 0 or 1, to specify the master or the slave device,
            // respectively. For the secondary IDE controller, the device number is always 1 because
            // the master device is reserved for the CD-ROM drive.
            controllerType = Bstr("IDE Controller");
            switch (ulAddressOnParent)
            {
                case 0:     // interpret this as primary master
                    lChannel = (long)0;
                    lDevice = (long)0;
                break;

                case 1:     // interpret this as primary slave
                    lChannel = (long)0;
                    lDevice = (long)1;
                break;

                case 2:     // interpret this as secondary master
                    lChannel = (long)1;
                    lDevice = (long)0;
                break;

                case 3:     // interpret this as secondary slave
                    lChannel = (long)1;
                    lDevice = (long)1;
                break;

                default:
                    throw setError(VBOX_E_NOT_SUPPORTED,
                                    tr("Invalid channel %RI16 specified; IDE controllers support only 0, 1 or 2"), ulAddressOnParent);
                break;
            }
        break;

        case HardDiskController::SATA:
            controllerType = Bstr("SATA Controller");
            lChannel = (long)ulAddressOnParent;
            lDevice = (long)0;
        break;

        case HardDiskController::SCSI:
            controllerType = Bstr("SCSI Controller");
            lChannel = (long)ulAddressOnParent;
            lDevice = (long)0;
        break;

        default: break;
    }
}

/**
 * Implementation of the import code. This gets called from the public Appliance::ImportMachines()
 * method as well as Appliance::importS3().
 * @param aLocInfo
 * @param aProgress
 * @return
 */
HRESULT Appliance::importImpl(const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress)
{
    /* Initialize our worker task */
    std::auto_ptr<TaskImportOVF> task(new TaskImportOVF(this));
    /* What should the task do */
    task->taskType = TaskImportOVF::Import;
    /* Copy the current location info to the task */
    task->locInfo = aLocInfo;

    Bstr progressDesc = BstrFmt(tr("Import appliance '%s'"),
                                aLocInfo.strPath.c_str());

    HRESULT rc = S_OK;

    /* todo: This progress init stuff should be done a little bit more generic */
    if (task->locInfo.storageType == VFSType_File)
        rc = setUpProgressFS(aProgress, progressDesc);
    else
        rc = setUpProgressImportS3(aProgress, progressDesc);
    if (FAILED(rc)) throw rc;

    task->progress = aProgress;

    rc = task->startThread();
    if (FAILED(rc)) throw rc;

    /* Don't destruct on success */
    task.release();

    return rc;
}

/**
 * Worker thread implementation for Read() (ovf reader).
 * @param aThread
 * @param pvUser
 */
/* static */
DECLCALLBACK(int) Appliance::taskThreadImportOVF(RTTHREAD /* aThread */, void *pvUser)
{
    std::auto_ptr<TaskImportOVF> task(static_cast<TaskImportOVF*>(pvUser));
    AssertReturn(task.get(), VERR_GENERAL_FAILURE);

    Appliance *pAppliance = task->pAppliance;

    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", pAppliance));

    switch (task->taskType)
    {
        case TaskImportOVF::Read:
        {
            if (task->locInfo.storageType == VFSType_File)
                pAppliance->readFS(task.get());
            else if (task->locInfo.storageType == VFSType_S3)
                pAppliance->readS3(task.get());
            break;
        }
        case TaskImportOVF::Import:
        {
            if (task->locInfo.storageType == VFSType_File)
                pAppliance->importFS(task.get());
            else if (task->locInfo.storageType == VFSType_S3)
                pAppliance->importS3(task.get());
            break;
        }
    }

    LogFlowFuncLeave();

    return VINF_SUCCESS;
}

/**
 *
 * @return
 */
int Appliance::TaskImportOVF::startThread()
{
    int vrc = RTThreadCreate(NULL, Appliance::taskThreadImportOVF, this,
                             0, RTTHREADTYPE_MAIN_HEAVY_WORKER, 0,
                             "Appliance::Task");

    ComAssertMsgRCRet(vrc,
                      ("Could not create taskThreadImportOVF (%Rrc)\n", vrc), E_FAIL);

    return S_OK;
}

/**
 *
 * @param pTask
 * @return
 */
int Appliance::importFS(TaskImportOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    if (!isApplianceIdle())
        return VERR_ACCESS_DENIED;

    // Change the appliance state so we can safely leave the lock while doing time-consuming
    // disk imports; also the below method calls do all kinds of locking which conflicts with
    // the appliance object lock
    m->state = Data::ApplianceImporting;
    appLock.release();

    HRESULT rc = S_OK;

    // rollback for errors:
    // a list of images that we created/imported
    list<MyHardDiskAttachment> llHardDiskAttachments;
    list< ComPtr<IMedium> > llHardDisksCreated;
    list<Bstr> llMachinesRegistered;            // list of string UUIDs

    ComPtr<ISession> session;
    bool fSessionOpen = false;
    rc = session.createInprocObject(CLSID_Session);
    if (FAILED(rc)) return rc;

    const OVFReader &reader = *m->pReader;
    // this is safe to access because this thread only gets started
    // if pReader != NULL

    /* If an manifest file exists, verify the content. Therefore we need all
     * files which are referenced by the OVF & the OVF itself */
    Utf8Str strMfFile = manifestFileName(pTask->locInfo.strPath);
    list<Utf8Str> filesList;
    if (RTPathExists(strMfFile.c_str()))
    {
        Utf8Str strSrcDir(pTask->locInfo.strPath);
        strSrcDir.stripFilename();
        /* Add every disks of every virtual system to an internal list */
        list< ComObjPtr<VirtualSystemDescription> >::const_iterator it;
        for (it = m->virtualSystemDescriptions.begin();
             it != m->virtualSystemDescriptions.end();
             ++it)
        {
            ComObjPtr<VirtualSystemDescription> vsdescThis = (*it);
            std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskImage);
            std::list<VirtualSystemDescriptionEntry*>::const_iterator itH;
            for (itH = avsdeHDs.begin();
                 itH != avsdeHDs.end();
                 ++itH)
            {
                VirtualSystemDescriptionEntry *vsdeHD = *itH;
                /* Find the disk from the OVF's disk list */
                DiskImagesMap::const_iterator itDiskImage = reader.m_mapDisks.find(vsdeHD->strRef);
                const DiskImage &di = itDiskImage->second;
                Utf8StrFmt strSrcFilePath("%s%c%s", strSrcDir.c_str(), RTPATH_DELIMITER, di.strHref.c_str());
                filesList.push_back(strSrcFilePath);
            }
        }
        /* Create the test list */
        PRTMANIFESTTEST pTestList = (PRTMANIFESTTEST)RTMemAllocZ(sizeof(RTMANIFESTTEST)*(filesList.size()+1));
        pTestList[0].pszTestFile = (char*)pTask->locInfo.strPath.c_str();
        pTestList[0].pszTestDigest = (char*)m->strOVFSHA1Digest.c_str();
        int vrc = VINF_SUCCESS;
        size_t i = 1;
        list<Utf8Str>::const_iterator it1;
        for (it1 = filesList.begin();
             it1 != filesList.end();
             ++it1, ++i)
        {
            char* pszDigest;
            vrc = RTSha1Digest((*it1).c_str(), &pszDigest);
            pTestList[i].pszTestFile = (char*)(*it1).c_str();
            pTestList[i].pszTestDigest = pszDigest;
        }
        size_t cIndexOnError;
        vrc = RTManifestVerify(strMfFile.c_str(), pTestList, filesList.size() + 1, &cIndexOnError);
        if (vrc == VERR_MANIFEST_DIGEST_MISMATCH)
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("The SHA1 digest of '%s' doesn't match to the one in '%s'"),
                          RTPathFilename(pTestList[cIndexOnError].pszTestFile),
                          RTPathFilename(strMfFile.c_str()));
        else if (RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Couldn't verify the content of '%s' against the available files (%Rrc)"),
                          RTPathFilename(strMfFile.c_str()),
                          vrc);
        /* Cleanup */
        for (size_t j = 1;
             j < filesList.size();
             ++j)
            RTStrFree(pTestList[j].pszTestDigest);
        RTMemFree(pTestList);
        if (FAILED(rc))
        {
            /* Return on error */
            pTask->rc = rc;

            if (!pTask->progress.isNull())
                pTask->progress->notifyComplete(rc);
            return rc;
        }
    }

    list<VirtualSystem>::const_iterator it;
    list< ComObjPtr<VirtualSystemDescription> >::const_iterator it1;
    /* Iterate through all virtual systems of that appliance */
    size_t i = 0;
    for (it = reader.m_llVirtualSystems.begin(),
         it1 = m->virtualSystemDescriptions.begin();
         it != reader.m_llVirtualSystems.end();
         ++it, ++it1, ++i)
    {
        const VirtualSystem &vsysThis = *it;
        ComObjPtr<VirtualSystemDescription> vsdescThis = (*it1);

        ComPtr<IMachine> pNewMachine;

        /* Catch possible errors */
        try
        {
            /* Guest OS type */
            std::list<VirtualSystemDescriptionEntry*> vsdeOS;
            vsdeOS = vsdescThis->findByType(VirtualSystemDescriptionType_OS);
            if (vsdeOS.size() < 1)
                throw setError(VBOX_E_FILE_ERROR,
                               tr("Missing guest OS type"));
            const Utf8Str &strOsTypeVBox = vsdeOS.front()->strVbox;

            /* Now that we know the base system get our internal defaults based on that. */
            ComPtr<IGuestOSType> osType;
            rc = mVirtualBox->GetGuestOSType(Bstr(strOsTypeVBox), osType.asOutParam());
            if (FAILED(rc)) throw rc;

            /* Create the machine */
            /* First get the name */
            std::list<VirtualSystemDescriptionEntry*> vsdeName = vsdescThis->findByType(VirtualSystemDescriptionType_Name);
            if (vsdeName.size() < 1)
                throw setError(VBOX_E_FILE_ERROR,
                               tr("Missing VM name"));
            const Utf8Str &strNameVBox = vsdeName.front()->strVbox;
            rc = mVirtualBox->CreateMachine(Bstr(strNameVBox), Bstr(strOsTypeVBox),
                                                 Bstr(), Bstr(), FALSE,
                                                 pNewMachine.asOutParam());
            if (FAILED(rc)) throw rc;

            // and the description
            std::list<VirtualSystemDescriptionEntry*> vsdeDescription = vsdescThis->findByType(VirtualSystemDescriptionType_Description);
            if (vsdeDescription.size())
            {
                const Utf8Str &strDescription = vsdeDescription.front()->strVbox;
                rc = pNewMachine->COMSETTER(Description)(Bstr(strDescription));
                if (FAILED(rc)) throw rc;
            }

            /* CPU count */
            std::list<VirtualSystemDescriptionEntry*> vsdeCPU = vsdescThis->findByType(VirtualSystemDescriptionType_CPU);
            ComAssertMsgThrow(vsdeCPU.size() == 1, ("CPU count missing"), E_FAIL);
            const Utf8Str &cpuVBox = vsdeCPU.front()->strVbox;
            ULONG tmpCount = (ULONG)RTStrToUInt64(cpuVBox.c_str());
            rc = pNewMachine->COMSETTER(CPUCount)(tmpCount);
            if (FAILED(rc)) throw rc;
            bool fEnableIOApic = false;
            /* We need HWVirt & IO-APIC if more than one CPU is requested */
            if (tmpCount > 1)
            {
                rc = pNewMachine->SetHWVirtExProperty(HWVirtExPropertyType_Enabled, TRUE);
                if (FAILED(rc)) throw rc;

                fEnableIOApic = true;
            }

            /* RAM */
            std::list<VirtualSystemDescriptionEntry*> vsdeRAM = vsdescThis->findByType(VirtualSystemDescriptionType_Memory);
            ComAssertMsgThrow(vsdeRAM.size() == 1, ("RAM size missing"), E_FAIL);
            const Utf8Str &memoryVBox = vsdeRAM.front()->strVbox;
            ULONG tt = (ULONG)RTStrToUInt64(memoryVBox.c_str());
            rc = pNewMachine->COMSETTER(MemorySize)(tt);
            if (FAILED(rc)) throw rc;

            /* VRAM */
            /* Get the recommended VRAM for this guest OS type */
            ULONG vramVBox;
            rc = osType->COMGETTER(RecommendedVRAM)(&vramVBox);
            if (FAILED(rc)) throw rc;

            /* Set the VRAM */
            rc = pNewMachine->COMSETTER(VRAMSize)(vramVBox);
            if (FAILED(rc)) throw rc;

            /* I/O APIC: so far we have no setting for this. Enable it if we
              import a Windows VM because if if Windows was installed without IOAPIC,
              it will not mind finding an one later on, but if Windows was installed
              _with_ an IOAPIC, it will bluescreen if it's not found */
            Bstr bstrFamilyId;
            rc = osType->COMGETTER(FamilyId)(bstrFamilyId.asOutParam());
            if (FAILED(rc)) throw rc;

            Utf8Str strFamilyId(bstrFamilyId);
            if (strFamilyId == "Windows")
                fEnableIOApic = true;

            /* If IP-APIC should be enabled could be have different reasons.
               See CPU count & the Win test above. Here we enable it if it was
               previously requested. */
            if (fEnableIOApic)
            {
                ComPtr<IBIOSSettings> pBIOSSettings;
                rc = pNewMachine->COMGETTER(BIOSSettings)(pBIOSSettings.asOutParam());
                if (FAILED(rc)) throw rc;

                rc = pBIOSSettings->COMSETTER(IOAPICEnabled)(TRUE);
                if (FAILED(rc)) throw rc;
            }

            /* Audio Adapter */
            std::list<VirtualSystemDescriptionEntry*> vsdeAudioAdapter = vsdescThis->findByType(VirtualSystemDescriptionType_SoundCard);
            /* @todo: we support one audio adapter only */
            if (vsdeAudioAdapter.size() > 0)
            {
                const Utf8Str& audioAdapterVBox = vsdeAudioAdapter.front()->strVbox;
                if (audioAdapterVBox.compare("null", Utf8Str::CaseInsensitive) != 0)
                {
                    uint32_t audio = RTStrToUInt32(audioAdapterVBox.c_str());
                    ComPtr<IAudioAdapter> audioAdapter;
                    rc = pNewMachine->COMGETTER(AudioAdapter)(audioAdapter.asOutParam());
                    if (FAILED(rc)) throw rc;
                    rc = audioAdapter->COMSETTER(Enabled)(true);
                    if (FAILED(rc)) throw rc;
                    rc = audioAdapter->COMSETTER(AudioController)(static_cast<AudioControllerType_T>(audio));
                    if (FAILED(rc)) throw rc;
                }
            }

#ifdef VBOX_WITH_USB
            /* USB Controller */
            std::list<VirtualSystemDescriptionEntry*> vsdeUSBController = vsdescThis->findByType(VirtualSystemDescriptionType_USBController);
            // USB support is enabled if there's at least one such entry; to disable USB support,
            // the type of the USB item would have been changed to "ignore"
            bool fUSBEnabled = vsdeUSBController.size() > 0;

            ComPtr<IUSBController> usbController;
            rc = pNewMachine->COMGETTER(USBController)(usbController.asOutParam());
            if (FAILED(rc)) throw rc;
            rc = usbController->COMSETTER(Enabled)(fUSBEnabled);
            if (FAILED(rc)) throw rc;
#endif /* VBOX_WITH_USB */

            /* Change the network adapters */
            std::list<VirtualSystemDescriptionEntry*> vsdeNW = vsdescThis->findByType(VirtualSystemDescriptionType_NetworkAdapter);
            if (vsdeNW.size() == 0)
            {
                /* No network adapters, so we have to disable our default one */
                ComPtr<INetworkAdapter> nwVBox;
                rc = pNewMachine->GetNetworkAdapter(0, nwVBox.asOutParam());
                if (FAILED(rc)) throw rc;
                rc = nwVBox->COMSETTER(Enabled)(false);
                if (FAILED(rc)) throw rc;
            }
            else
            {
                list<VirtualSystemDescriptionEntry*>::const_iterator nwIt;
                /* Iterate through all network cards. We support 8 network adapters
                 * at the maximum. (@todo: warn if there are more!) */
                size_t a = 0;
                for (nwIt = vsdeNW.begin();
                     (nwIt != vsdeNW.end() && a < SchemaDefs::NetworkAdapterCount);
                     ++nwIt, ++a)
                {
                    const VirtualSystemDescriptionEntry* pvsys = *nwIt;

                    const Utf8Str &nwTypeVBox = pvsys->strVbox;
                    uint32_t tt1 = RTStrToUInt32(nwTypeVBox.c_str());
                    ComPtr<INetworkAdapter> pNetworkAdapter;
                    rc = pNewMachine->GetNetworkAdapter((ULONG)a, pNetworkAdapter.asOutParam());
                    if (FAILED(rc)) throw rc;
                    /* Enable the network card & set the adapter type */
                    rc = pNetworkAdapter->COMSETTER(Enabled)(true);
                    if (FAILED(rc)) throw rc;
                    rc = pNetworkAdapter->COMSETTER(AdapterType)(static_cast<NetworkAdapterType_T>(tt1));
                    if (FAILED(rc)) throw rc;

                    // default is NAT; change to "bridged" if extra conf says so
                    if (!pvsys->strExtraConfig.compare("type=Bridged", Utf8Str::CaseInsensitive))
                    {
                        /* Attach to the right interface */
                        rc = pNetworkAdapter->AttachToBridgedInterface();
                        if (FAILED(rc)) throw rc;
                        ComPtr<IHost> host;
                        rc = mVirtualBox->COMGETTER(Host)(host.asOutParam());
                        if (FAILED(rc)) throw rc;
                        com::SafeIfaceArray<IHostNetworkInterface> nwInterfaces;
                        rc = host->COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(nwInterfaces));
                        if (FAILED(rc)) throw rc;
                        /* We search for the first host network interface which
                         * is usable for bridged networking */
                        for (size_t j = 0;
                             j < nwInterfaces.size();
                             ++j)
                        {
                            HostNetworkInterfaceType_T itype;
                            rc = nwInterfaces[j]->COMGETTER(InterfaceType)(&itype);
                            if (FAILED(rc)) throw rc;
                            if (itype == HostNetworkInterfaceType_Bridged)
                            {
                                Bstr name;
                                rc = nwInterfaces[j]->COMGETTER(Name)(name.asOutParam());
                                if (FAILED(rc)) throw rc;
                                /* Set the interface name to attach to */
                                pNetworkAdapter->COMSETTER(HostInterface)(name);
                                if (FAILED(rc)) throw rc;
                                break;
                            }
                        }
                    }
                    /* Next test for host only interfaces */
                    else if (!pvsys->strExtraConfig.compare("type=HostOnly", Utf8Str::CaseInsensitive))
                    {
                        /* Attach to the right interface */
                        rc = pNetworkAdapter->AttachToHostOnlyInterface();
                        if (FAILED(rc)) throw rc;
                        ComPtr<IHost> host;
                        rc = mVirtualBox->COMGETTER(Host)(host.asOutParam());
                        if (FAILED(rc)) throw rc;
                        com::SafeIfaceArray<IHostNetworkInterface> nwInterfaces;
                        rc = host->COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(nwInterfaces));
                        if (FAILED(rc)) throw rc;
                        /* We search for the first host network interface which
                         * is usable for host only networking */
                        for (size_t j = 0;
                             j < nwInterfaces.size();
                             ++j)
                        {
                            HostNetworkInterfaceType_T itype;
                            rc = nwInterfaces[j]->COMGETTER(InterfaceType)(&itype);
                            if (FAILED(rc)) throw rc;
                            if (itype == HostNetworkInterfaceType_HostOnly)
                            {
                                Bstr name;
                                rc = nwInterfaces[j]->COMGETTER(Name)(name.asOutParam());
                                if (FAILED(rc)) throw rc;
                                /* Set the interface name to attach to */
                                pNetworkAdapter->COMSETTER(HostInterface)(name);
                                if (FAILED(rc)) throw rc;
                                break;
                            }
                        }
                    }
                }
            }

            /* Hard disk controller IDE */
            std::list<VirtualSystemDescriptionEntry*> vsdeHDCIDE = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskControllerIDE);
            if (vsdeHDCIDE.size() > 1)
                throw setError(VBOX_E_FILE_ERROR,
                               tr("Too many IDE controllers in OVF; import facility only supports one"));
            if (vsdeHDCIDE.size() == 1)
            {
                ComPtr<IStorageController> pController;
                rc = pNewMachine->AddStorageController(Bstr("IDE Controller"), StorageBus_IDE, pController.asOutParam());
                if (FAILED(rc)) throw rc;

                const char *pcszIDEType = vsdeHDCIDE.front()->strVbox.c_str();
                if (!strcmp(pcszIDEType, "PIIX3"))
                    rc = pController->COMSETTER(ControllerType)(StorageControllerType_PIIX3);
                else if (!strcmp(pcszIDEType, "PIIX4"))
                    rc = pController->COMSETTER(ControllerType)(StorageControllerType_PIIX4);
                else if (!strcmp(pcszIDEType, "ICH6"))
                    rc = pController->COMSETTER(ControllerType)(StorageControllerType_ICH6);
                else
                    throw setError(VBOX_E_FILE_ERROR,
                                   tr("Invalid IDE controller type \"%s\""),
                                   pcszIDEType);
                if (FAILED(rc)) throw rc;
            }
#ifdef VBOX_WITH_AHCI
            /* Hard disk controller SATA */
            std::list<VirtualSystemDescriptionEntry*> vsdeHDCSATA = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskControllerSATA);
            if (vsdeHDCSATA.size() > 1)
                throw setError(VBOX_E_FILE_ERROR,
                               tr("Too many SATA controllers in OVF; import facility only supports one"));
            if (vsdeHDCSATA.size() > 0)
            {
                ComPtr<IStorageController> pController;
                const Utf8Str &hdcVBox = vsdeHDCSATA.front()->strVbox;
                if (hdcVBox == "AHCI")
                {
                    rc = pNewMachine->AddStorageController(Bstr("SATA Controller"), StorageBus_SATA, pController.asOutParam());
                    if (FAILED(rc)) throw rc;
                }
                else
                    throw setError(VBOX_E_FILE_ERROR,
                                   tr("Invalid SATA controller type \"%s\""),
                                   hdcVBox.c_str());
            }
#endif /* VBOX_WITH_AHCI */

#ifdef VBOX_WITH_LSILOGIC
            /* Hard disk controller SCSI */
            std::list<VirtualSystemDescriptionEntry*> vsdeHDCSCSI = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskControllerSCSI);
            if (vsdeHDCSCSI.size() > 1)
                throw setError(VBOX_E_FILE_ERROR,
                               tr("Too many SCSI controllers in OVF; import facility only supports one"));
            if (vsdeHDCSCSI.size() > 0)
            {
                ComPtr<IStorageController> pController;
                StorageControllerType_T controllerType;
                const Utf8Str &hdcVBox = vsdeHDCSCSI.front()->strVbox;
                if (hdcVBox == "LsiLogic")
                    controllerType = StorageControllerType_LsiLogic;
                else if (hdcVBox == "BusLogic")
                    controllerType = StorageControllerType_BusLogic;
                else
                    throw setError(VBOX_E_FILE_ERROR,
                                   tr("Invalid SCSI controller type \"%s\""),
                                   hdcVBox.c_str());

                rc = pNewMachine->AddStorageController(Bstr("SCSI Controller"), StorageBus_SCSI, pController.asOutParam());
                if (FAILED(rc)) throw rc;
                rc = pController->COMSETTER(ControllerType)(controllerType);
                if (FAILED(rc)) throw rc;
            }
#endif /* VBOX_WITH_LSILOGIC */

            /* Now its time to register the machine before we add any hard disks */
            rc = mVirtualBox->RegisterMachine(pNewMachine);
            if (FAILED(rc)) throw rc;

            Bstr bstrNewMachineId;
            rc = pNewMachine->COMGETTER(Id)(bstrNewMachineId.asOutParam());
            if (FAILED(rc)) throw rc;

            // store new machine for roll-back in case of errors
            llMachinesRegistered.push_back(bstrNewMachineId);

            // Add floppies and CD-ROMs to the appropriate controllers.
            std::list<VirtualSystemDescriptionEntry*> vsdeFloppy = vsdescThis->findByType(VirtualSystemDescriptionType_Floppy);
            if (vsdeFloppy.size() > 1)
                throw setError(VBOX_E_FILE_ERROR,
                               tr("Too many floppy controllers in OVF; import facility only supports one"));
            std::list<VirtualSystemDescriptionEntry*> vsdeCDROM = vsdescThis->findByType(VirtualSystemDescriptionType_CDROM);
            if (    (vsdeFloppy.size() > 0)
                 || (vsdeCDROM.size() > 0)
               )
            {
                // If there's an error here we need to close the session, so
                // we need another try/catch block.

                try
                {
                    /* In order to attach things we need to open a session
                     * for the new machine */
                    rc = mVirtualBox->OpenSession(session, bstrNewMachineId);
                    if (FAILED(rc)) throw rc;
                    fSessionOpen = true;

                    ComPtr<IMachine> sMachine;
                    rc = session->COMGETTER(Machine)(sMachine.asOutParam());
                    if (FAILED(rc)) throw rc;

                    // floppy first
                    if (vsdeFloppy.size() == 1)
                    {
                        ComPtr<IStorageController> pController;
                        rc = sMachine->AddStorageController(Bstr("Floppy Controller"), StorageBus_Floppy, pController.asOutParam());
                        if (FAILED(rc)) throw rc;

                        Bstr bstrName;
                        rc = pController->COMGETTER(Name)(bstrName.asOutParam());
                        if (FAILED(rc)) throw rc;

                        // this is for rollback later
                        MyHardDiskAttachment mhda;
                        mhda.bstrUuid = bstrNewMachineId;
                        mhda.pMachine = pNewMachine;
                        mhda.controllerType = bstrName;
                        mhda.lChannel = 0;
                        mhda.lDevice = 0;

                        Log(("Attaching floppy\n"));

                        rc = sMachine->AttachDevice(mhda.controllerType,
                                                    mhda.lChannel,
                                                    mhda.lDevice,
                                                    DeviceType_Floppy,
                                                    NULL);
                        if (FAILED(rc)) throw rc;

                        llHardDiskAttachments.push_back(mhda);
                    }


                    // CD-ROMs next
                    for (std::list<VirtualSystemDescriptionEntry*>::const_iterator jt = vsdeCDROM.begin();
                         jt != vsdeCDROM.end();
                         ++jt)
                    {
                        // for now always attach to secondary master on IDE controller;
                        // there seems to be no useful information in OVF where else to
                        // attach jt (@todo test with latest versions of OVF software)

                        // find the IDE controller
                        const HardDiskController *pController = NULL;
                        for (ControllersMap::const_iterator kt = vsysThis.mapControllers.begin();
                             kt != vsysThis.mapControllers.end();
                             ++kt)
                        {
                            if (kt->second.system == HardDiskController::IDE)
                            {
                                pController = &kt->second;
                            }
                        }

                        if (!pController)
                            throw setError(VBOX_E_FILE_ERROR,
                                           tr("OVF wants a CD-ROM drive but cannot find IDE controller, which is required in this version of VirtualBox"));

                        // this is for rollback later
                        MyHardDiskAttachment mhda;
                        mhda.bstrUuid = bstrNewMachineId;
                        mhda.pMachine = pNewMachine;

                        convertDiskAttachmentValues(*pController,
                                                    2,     // interpreted as secondary master
                                                    mhda.controllerType,        // Bstr
                                                    mhda.lChannel,
                                                    mhda.lDevice);

                        Log(("Attaching CD-ROM to channel %d on device %d\n", mhda.lChannel, mhda.lDevice));

                        rc = sMachine->AttachDevice(mhda.controllerType,
                                                    mhda.lChannel,
                                                    mhda.lDevice,
                                                    DeviceType_DVD,
                                                    NULL);
                        if (FAILED(rc)) throw rc;

                        llHardDiskAttachments.push_back(mhda);
                    } // end for (itHD = avsdeHDs.begin();

                    rc = sMachine->SaveSettings();
                    if (FAILED(rc)) throw rc;

                    // only now that we're done with all disks, close the session
                    rc = session->Close();
                    if (FAILED(rc)) throw rc;
                    fSessionOpen = false;
                }
                catch(HRESULT /* aRC */)
                {
                    if (fSessionOpen)
                        session->Close();

                    throw;
                }
            }

            /* Create the hard disks & connect them to the appropriate controllers. */
            std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskImage);
            if (avsdeHDs.size() > 0)
            {
                // If there's an error here we need to close the session, so
                // we need another try/catch block.
                ComPtr<IMedium> srcHdVBox;
                bool fSourceHdNeedsClosing = false;

                try
                {
                    /* In order to attach hard disks we need to open a session
                     * for the new machine */
                    rc = mVirtualBox->OpenSession(session, bstrNewMachineId);
                    if (FAILED(rc)) throw rc;
                    fSessionOpen = true;

                    /* The disk image has to be on the same place as the OVF file. So
                     * strip the filename out of the full file path. */
                    Utf8Str strSrcDir(pTask->locInfo.strPath);
                    strSrcDir.stripFilename();

                    /* Iterate over all given disk images */
                    list<VirtualSystemDescriptionEntry*>::const_iterator itHD;
                    for (itHD = avsdeHDs.begin();
                         itHD != avsdeHDs.end();
                         ++itHD)
                    {
                        VirtualSystemDescriptionEntry *vsdeHD = *itHD;

                        /* Check if the destination file exists already or the
                         * destination path is empty. */
                        if (    vsdeHD->strVbox.isEmpty()
                             || RTPathExists(vsdeHD->strVbox.c_str())
                           )
                            /* This isn't allowed */
                            throw setError(VBOX_E_FILE_ERROR,
                                           tr("Destination file '%s' exists",
                                              vsdeHD->strVbox.c_str()));

                        /* Find the disk from the OVF's disk list */
                        DiskImagesMap::const_iterator itDiskImage = reader.m_mapDisks.find(vsdeHD->strRef);
                        /* vsdeHD->strRef contains the disk identifier (e.g. "vmdisk1"), which should exist
                           in the virtual system's disks map under that ID and also in the global images map. */
                        VirtualDisksMap::const_iterator itVirtualDisk = vsysThis.mapVirtualDisks.find(vsdeHD->strRef);

                        if (    itDiskImage == reader.m_mapDisks.end()
                             || itVirtualDisk == vsysThis.mapVirtualDisks.end()
                           )
                            throw setError(E_FAIL,
                                           tr("Internal inconsistency looking up disk images."));

                        const DiskImage &di = itDiskImage->second;
                        const VirtualDisk &vd = itVirtualDisk->second;

                        /* Make sure all target directories exists */
                        rc = VirtualBox::ensureFilePathExists(vsdeHD->strVbox.c_str());
                        if (FAILED(rc))
                            throw rc;

                        // subprogress object for hard disk
                        ComPtr<IProgress> pProgress2;

                        ComPtr<IMedium> dstHdVBox;
                        /* If strHref is empty we have to create a new file */
                        if (di.strHref.isEmpty())
                        {
                            /* Which format to use? */
                            Bstr srcFormat = L"VDI";
                            if (   di.strFormat.compare("http://www.vmware.com/specifications/vmdk.html#sparse", Utf8Str::CaseInsensitive)
                                || di.strFormat.compare("http://www.vmware.com/specifications/vmdk.html#compressed", Utf8Str::CaseInsensitive))
                                srcFormat = L"VMDK";
                            /* Create an empty hard disk */
                            rc = mVirtualBox->CreateHardDisk(srcFormat, Bstr(vsdeHD->strVbox), dstHdVBox.asOutParam());
                            if (FAILED(rc)) throw rc;

                            /* Create a dynamic growing disk image with the given capacity */
                            rc = dstHdVBox->CreateBaseStorage(di.iCapacity / _1M, MediumVariant_Standard, pProgress2.asOutParam());
                            if (FAILED(rc)) throw rc;

                            /* Advance to the next operation */
                            if (!pTask->progress.isNull())
                                pTask->progress->SetNextOperation(BstrFmt(tr("Creating virtual disk image '%s'"), vsdeHD->strVbox.c_str()),
                                                                 vsdeHD->ulSizeMB);     // operation's weight, as set up with the IProgress originally
                        }
                        else
                        {
                            /* Construct the source file path */
                            Utf8StrFmt strSrcFilePath("%s%c%s", strSrcDir.c_str(), RTPATH_DELIMITER, di.strHref.c_str());
                            /* Check if the source file exists */
                            if (!RTPathExists(strSrcFilePath.c_str()))
                                /* This isn't allowed */
                                throw setError(VBOX_E_FILE_ERROR,
                                               tr("Source virtual disk image file '%s' doesn't exist"),
                                                  strSrcFilePath.c_str());

                            /* Clone the disk image (this is necessary cause the id has
                             * to be recreated for the case the same hard disk is
                             * attached already from a previous import) */

                            /* First open the existing disk image */
                            rc = mVirtualBox->OpenHardDisk(Bstr(strSrcFilePath),
                                                           AccessMode_ReadOnly,
                                                           false,
                                                           NULL,
                                                           false,
                                                           NULL,
                                                           srcHdVBox.asOutParam());
                            if (FAILED(rc)) throw rc;
                            fSourceHdNeedsClosing = true;

                            /* We need the format description of the source disk image */
                            Bstr srcFormat;
                            rc = srcHdVBox->COMGETTER(Format)(srcFormat.asOutParam());
                            if (FAILED(rc)) throw rc;
                            /* Create a new hard disk interface for the destination disk image */
                            rc = mVirtualBox->CreateHardDisk(srcFormat, Bstr(vsdeHD->strVbox), dstHdVBox.asOutParam());
                            if (FAILED(rc)) throw rc;
                            /* Clone the source disk image */
                            rc = srcHdVBox->CloneTo(dstHdVBox, MediumVariant_Standard, NULL, pProgress2.asOutParam());
                            if (FAILED(rc)) throw rc;

                            /* Advance to the next operation */
                            if (!pTask->progress.isNull())
                                pTask->progress->SetNextOperation(BstrFmt(tr("Importing virtual disk image '%s'"), strSrcFilePath.c_str()),
                                                                 vsdeHD->ulSizeMB);     // operation's weight, as set up with the IProgress originally);
                        }

                        // now wait for the background disk operation to complete; this throws HRESULTs on error
                        waitForAsyncProgress(pTask->progress, pProgress2);

                        if (fSourceHdNeedsClosing)
                        {
                            rc = srcHdVBox->Close();
                            if (FAILED(rc)) throw rc;
                            fSourceHdNeedsClosing = false;
                        }

                        llHardDisksCreated.push_back(dstHdVBox);
                        /* Now use the new uuid to attach the disk image to our new machine */
                        ComPtr<IMachine> sMachine;
                        rc = session->COMGETTER(Machine)(sMachine.asOutParam());
                        if (FAILED(rc)) throw rc;
                        Bstr hdId;
                        rc = dstHdVBox->COMGETTER(Id)(hdId.asOutParam());
                        if (FAILED(rc)) throw rc;

                        /* For now we assume we have one controller of every type only */
                        HardDiskController hdc = (*vsysThis.mapControllers.find(vd.idController)).second;

                        // this is for rollback later
                        MyHardDiskAttachment mhda;
                        mhda.bstrUuid = bstrNewMachineId;
                        mhda.pMachine = pNewMachine;

                        convertDiskAttachmentValues(hdc,
                                                    vd.ulAddressOnParent,
                                                    mhda.controllerType,        // Bstr
                                                    mhda.lChannel,
                                                    mhda.lDevice);

                        Log(("Attaching disk %s to channel %d on device %d\n", vsdeHD->strVbox.c_str(), mhda.lChannel, mhda.lDevice));

                        rc = sMachine->AttachDevice(mhda.controllerType,
                                                    mhda.lChannel,
                                                    mhda.lDevice,
                                                    DeviceType_HardDisk,
                                                    hdId);
                        if (FAILED(rc)) throw rc;

                        llHardDiskAttachments.push_back(mhda);

                        rc = sMachine->SaveSettings();
                        if (FAILED(rc)) throw rc;
                    } // end for (itHD = avsdeHDs.begin();

                    // only now that we're done with all disks, close the session
                    rc = session->Close();
                    if (FAILED(rc)) throw rc;
                    fSessionOpen = false;
                }
                catch(HRESULT /* aRC */)
                {
                    if (fSourceHdNeedsClosing)
                        srcHdVBox->Close();

                    if (fSessionOpen)
                        session->Close();

                    throw;
                }
            }
        }
        catch(HRESULT aRC)
        {
            rc = aRC;
        }

        if (FAILED(rc))
            break;

    } // for (it = pAppliance->m->llVirtualSystems.begin(),

    if (FAILED(rc))
    {
        // with _whatever_ error we've had, do a complete roll-back of
        // machines and disks we've created; unfortunately this is
        // not so trivially done...

        HRESULT rc2;
        // detach all hard disks from all machines we created
        list<MyHardDiskAttachment>::iterator itM;
        for (itM = llHardDiskAttachments.begin();
             itM != llHardDiskAttachments.end();
             ++itM)
        {
            const MyHardDiskAttachment &mhda = *itM;
            Bstr bstrUuid(mhda.bstrUuid);           // make a copy, Windows can't handle const Bstr
            rc2 = mVirtualBox->OpenSession(session, bstrUuid);
            if (SUCCEEDED(rc2))
            {
                ComPtr<IMachine> sMachine;
                rc2 = session->COMGETTER(Machine)(sMachine.asOutParam());
                if (SUCCEEDED(rc2))
                {
                    rc2 = sMachine->DetachDevice(Bstr(mhda.controllerType), mhda.lChannel, mhda.lDevice);
                    rc2 = sMachine->SaveSettings();
                }
                session->Close();
            }
        }

        // now clean up all hard disks we created
        list< ComPtr<IMedium> >::iterator itHD;
        for (itHD = llHardDisksCreated.begin();
             itHD != llHardDisksCreated.end();
             ++itHD)
        {
            ComPtr<IMedium> pDisk = *itHD;
            ComPtr<IProgress> pProgress;
            rc2 = pDisk->DeleteStorage(pProgress.asOutParam());
            rc2 = pProgress->WaitForCompletion(-1);
        }

        // finally, deregister and remove all machines
        list<Bstr>::iterator itID;
        for (itID = llMachinesRegistered.begin();
             itID != llMachinesRegistered.end();
             ++itID)
        {
            Bstr bstrGuid = *itID;      // make a copy, Windows can't handle const Bstr
            ComPtr<IMachine> failedMachine;
            rc2 = mVirtualBox->UnregisterMachine(bstrGuid, failedMachine.asOutParam());
            if (SUCCEEDED(rc2))
                rc2 = failedMachine->DeleteSettings();
        }
    }

    // restore the appliance state
    appLock.acquire();
    m->state = Data::ApplianceIdle;

    pTask->rc = rc;

    if (!pTask->progress.isNull())
        pTask->progress->notifyComplete(rc);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}

/**
 *
 * @param pTask
 * @return
 */
int Appliance::importS3(TaskImportOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = VINF_SUCCESS;
    RTS3 hS3 = NIL_RTS3;
    char szOSTmpDir[RTPATH_MAX];
    RTPathTemp(szOSTmpDir, sizeof(szOSTmpDir));
    /* The template for the temporary directory created below */
    char *pszTmpDir;
    RTStrAPrintf(&pszTmpDir, "%s"RTPATH_SLASH_STR"vbox-ovf-XXXXXX", szOSTmpDir);
    list< pair<Utf8Str, ULONG> > filesList;

    HRESULT rc = S_OK;
    try
    {
        /* Extract the bucket */
        Utf8Str tmpPath = pTask->locInfo.strPath;
        Utf8Str bucket;
        parseBucket(tmpPath, bucket);

        /* We need a temporary directory which we can put the all disk images
         * in */
        vrc = RTDirCreateTemp(pszTmpDir);
        if (RT_FAILURE(vrc))
            throw setError(VBOX_E_FILE_ERROR,
                           tr("Cannot create temporary directory '%s'"), pszTmpDir);

        /* Add every disks of every virtual system to an internal list */
        list< ComObjPtr<VirtualSystemDescription> >::const_iterator it;
        for (it = m->virtualSystemDescriptions.begin();
             it != m->virtualSystemDescriptions.end();
             ++it)
        {
            ComObjPtr<VirtualSystemDescription> vsdescThis = (*it);
            std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskImage);
            std::list<VirtualSystemDescriptionEntry*>::const_iterator itH;
            for (itH = avsdeHDs.begin();
                 itH != avsdeHDs.end();
                 ++itH)
            {
                const Utf8Str &strTargetFile = (*itH)->strOvf;
                if (!strTargetFile.isEmpty())
                {
                    /* The temporary name of the target disk file */
                    Utf8StrFmt strTmpDisk("%s/%s", pszTmpDir, RTPathFilename(strTargetFile.c_str()));
                    filesList.push_back(pair<Utf8Str, ULONG>(strTmpDisk, (*itH)->ulSizeMB));
                }
            }
        }

        /* Next we have to download the disk images */
        vrc = RTS3Create(&hS3, pTask->locInfo.strUsername.c_str(), pTask->locInfo.strPassword.c_str(), pTask->locInfo.strHostname.c_str(), "virtualbox-agent/"VBOX_VERSION_STRING);
        if(RT_FAILURE(vrc))
            throw setError(VBOX_E_IPRT_ERROR,
                           tr("Cannot create S3 service handler"));
        RTS3SetProgressCallback(hS3, pTask->updateProgress, &pTask);

        /* Download all files */
        for (list< pair<Utf8Str, ULONG> >::const_iterator it1 = filesList.begin(); it1 != filesList.end(); ++it1)
        {
            const pair<Utf8Str, ULONG> &s = (*it1);
            const Utf8Str &strSrcFile = s.first;
            /* Construct the source file name */
            char *pszFilename = RTPathFilename(strSrcFile.c_str());
            /* Advance to the next operation */
            if (!pTask->progress.isNull())
                pTask->progress->SetNextOperation(BstrFmt(tr("Downloading file '%s'"), pszFilename), s.second);

            vrc = RTS3GetKey(hS3, bucket.c_str(), pszFilename, strSrcFile.c_str());
            if (RT_FAILURE(vrc))
            {
                if(vrc == VERR_S3_CANCELED)
                    throw S_OK; /* todo: !!!!!!!!!!!!! */
                else if(vrc == VERR_S3_ACCESS_DENIED)
                    throw setError(E_ACCESSDENIED,
                                   tr("Cannot download file '%s' from S3 storage server (Access denied). Make sure that your credentials are right. Also check that your host clock is properly synced"), pszFilename);
                else if(vrc == VERR_S3_NOT_FOUND)
                    throw setError(VBOX_E_FILE_ERROR,
                                   tr("Cannot download file '%s' from S3 storage server (File not found)"), pszFilename);
                else
                    throw setError(VBOX_E_IPRT_ERROR,
                                   tr("Cannot download file '%s' from S3 storage server (%Rrc)"), pszFilename, vrc);
            }
        }

        /* Provide a OVF file (haven't to exist) so the import routine can
         * figure out where the disk images/manifest file are located. */
        Utf8StrFmt strTmpOvf("%s/%s", pszTmpDir, RTPathFilename(tmpPath.c_str()));
        /* Now check if there is an manifest file. This is optional. */
        Utf8Str strManifestFile = manifestFileName(strTmpOvf);
        char *pszFilename = RTPathFilename(strManifestFile.c_str());
        if (!pTask->progress.isNull())
            pTask->progress->SetNextOperation(BstrFmt(tr("Downloading file '%s'"), pszFilename), 1);

        /* Try to download it. If the error is VERR_S3_NOT_FOUND, it isn't fatal. */
        vrc = RTS3GetKey(hS3, bucket.c_str(), pszFilename, strManifestFile.c_str());
        if (RT_SUCCESS(vrc))
            filesList.push_back(pair<Utf8Str, ULONG>(strManifestFile, 0));
        else if (RT_FAILURE(vrc))
        {
            if(vrc == VERR_S3_CANCELED)
                throw S_OK; /* todo: !!!!!!!!!!!!! */
            else if(vrc == VERR_S3_NOT_FOUND)
                vrc = VINF_SUCCESS; /* Not found is ok */
            else if(vrc == VERR_S3_ACCESS_DENIED)
                throw setError(E_ACCESSDENIED,
                               tr("Cannot download file '%s' from S3 storage server (Access denied). Make sure that your credentials are right. Also check that your host clock is properly synced"), pszFilename);
            else
                throw setError(VBOX_E_IPRT_ERROR,
                               tr("Cannot download file '%s' from S3 storage server (%Rrc)"), pszFilename, vrc);
        }

        /* Close the connection early */
        RTS3Destroy(hS3);
        hS3 = NIL_RTS3;

        if (!pTask->progress.isNull())
            pTask->progress->SetNextOperation(BstrFmt(tr("Importing appliance")), m->ulWeightPerOperation);

        ComObjPtr<Progress> progress;
        /* Import the whole temporary OVF & the disk images */
        LocationInfo li;
        li.strPath = strTmpOvf;
        rc = importImpl(li, progress);
        if (FAILED(rc)) throw rc;

        /* Unlock the appliance for the fs import thread */
        appLock.release();
        /* Wait until the import is done, but report the progress back to the
           caller */
        ComPtr<IProgress> progressInt(progress);
        waitForAsyncProgress(pTask->progress, progressInt); /* Any errors will be thrown */

        /* Again lock the appliance for the next steps */
        appLock.acquire();
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }
    /* Cleanup */
    RTS3Destroy(hS3);
    /* Delete all files which where temporary created */
    for (list< pair<Utf8Str, ULONG> >::const_iterator it1 = filesList.begin(); it1 != filesList.end(); ++it1)
    {
        const char *pszFilePath = (*it1).first.c_str();
        if (RTPathExists(pszFilePath))
        {
            vrc = RTFileDelete(pszFilePath);
            if(RT_FAILURE(vrc))
                rc = setError(VBOX_E_FILE_ERROR,
                              tr("Cannot delete file '%s' (%Rrc)"), pszFilePath, vrc);
        }
    }
    /* Delete the temporary directory */
    if (RTPathExists(pszTmpDir))
    {
        vrc = RTDirRemove(pszTmpDir);
        if(RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Cannot delete temporary directory '%s' (%Rrc)"), pszTmpDir, vrc);
    }
    if (pszTmpDir)
        RTStrFree(pszTmpDir);

    pTask->rc = rc;

    if (!pTask->progress.isNull())
        pTask->progress->notifyComplete(rc);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}


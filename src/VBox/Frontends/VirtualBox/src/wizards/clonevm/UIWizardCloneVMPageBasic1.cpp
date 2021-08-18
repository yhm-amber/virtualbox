/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardCloneVMPageBasic1 class implementation.
 */

/*
 * Copyright (C) 2011-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QDir>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "UIWizardCloneVM.h"
#include "UIWizardCloneVMPageBasic1.h"
#include "UICommon.h"

/* COM includes: */
#include "CVirtualBox.h"

QString UIWizardCloneVMNamePage::composeCloneFilePath(const QString &strCloneName, const QString &strGroup, const QString &strFolderPath)
{
    CVirtualBox vbox = uiCommon().virtualBox();
    return QDir::toNativeSeparators(vbox.ComposeMachineFilename(strCloneName, strGroup, QString(), strFolderPath));
}

UIWizardCloneVMPageBasic1::UIWizardCloneVMPageBasic1(const QString &strOriginalName, const QString &strDefaultPath, const QString &strGroup)
    : m_pNamePathEditor(0)
    , m_pAdditionalOptionsEditor(0)
    , m_strOriginalName(strOriginalName)
    , m_strGroup(strGroup)
{
    prepare(strDefaultPath);
}

void UIWizardCloneVMPageBasic1::retranslateUi()
{
    setTitle(UIWizardCloneVM::tr("New machine name and path"));

    if (m_pMainLabel)
        m_pMainLabel->setText(UIWizardCloneVM::tr("<p>Please choose a name and optionally a folder for the new virtual machine. "
                                                  "The new machine will be a clone of the machine <b>%1</b>.</p>")
                              .arg(m_strOriginalName));

    int iMaxWidth = 0;
    if (m_pNamePathEditor)
        iMaxWidth = qMax(iMaxWidth, m_pNamePathEditor->firstColumnWidth());
    if (m_pAdditionalOptionsEditor)
        iMaxWidth = qMax(iMaxWidth, m_pAdditionalOptionsEditor->firstColumnWidth());

    if (m_pNamePathEditor)
        m_pNamePathEditor->setFirstColumnWidth(iMaxWidth);
    if (m_pAdditionalOptionsEditor)
        m_pAdditionalOptionsEditor->setFirstColumnWidth(iMaxWidth);
}

void UIWizardCloneVMPageBasic1::initializePage()
{
    retranslateUi();
    if (m_pNamePathEditor)
    {
        m_pNamePathEditor->setFocus();
        if (!m_userModifiedParameters.contains("CloneName"))
            cloneVMWizardPropertySet(CloneName, m_pNamePathEditor->cloneName());
            if (!m_userModifiedParameters.contains("CloneFilePath"))
                cloneVMWizardPropertySet(CloneFilePath,
                             UIWizardCloneVMNamePage::composeCloneFilePath(m_pNamePathEditor->cloneName(), m_strGroup, m_pNamePathEditor->clonePath()));
    }
    if (m_pAdditionalOptionsEditor)
    {
        if (!m_userModifiedParameters.contains("MacAddressPolicy"))
            cloneVMWizardPropertySet(MacAddressPolicy, m_pAdditionalOptionsEditor->macAddressClonePolicy());
        if (!m_userModifiedParameters.contains("KeepDiskNames"))
            cloneVMWizardPropertySet(KeepDiskNames, m_pAdditionalOptionsEditor->keepDiskNames());
        if (!m_userModifiedParameters.contains("KeepHardwareUUIDs"))
            cloneVMWizardPropertySet(KeepHardwareUUIDs, m_pAdditionalOptionsEditor->keepHardwareUUIDs());
    }
}

void UIWizardCloneVMPageBasic1::prepare(const QString &strDefaultClonePath)
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);

    AssertReturnVoid(pMainLayout);

    m_pMainLabel = new QIRichTextLabel(this);
    if (m_pMainLabel)
        pMainLayout->addWidget(m_pMainLabel);

    m_pNamePathEditor = new UICloneVMNamePathEditor(m_strOriginalName, strDefaultClonePath);
    if (m_pNamePathEditor)
    {
        m_pNamePathEditor->setFlat(true);
        m_pNamePathEditor->setLayoutContentsMargins(0, 0, 0, 0);
        pMainLayout->addWidget(m_pNamePathEditor);
        connect(m_pNamePathEditor, &UICloneVMNamePathEditor::sigCloneNameChanged,
                this, &UIWizardCloneVMPageBasic1::sltCloneNameChanged);
        connect(m_pNamePathEditor, &UICloneVMNamePathEditor::sigClonePathChanged,
                this, &UIWizardCloneVMPageBasic1::sltClonePathChanged);
    }

    m_pAdditionalOptionsEditor = new UICloneVMAdditionalOptionsEditor;
    if (m_pAdditionalOptionsEditor)
    {
        m_pAdditionalOptionsEditor->setFlat(true);
        pMainLayout->addWidget(m_pAdditionalOptionsEditor);
        connect(m_pAdditionalOptionsEditor, &UICloneVMAdditionalOptionsEditor::sigMACAddressClonePolicyChanged,
                this, &UIWizardCloneVMPageBasic1::sltMACAddressClonePolicyChanged);
        connect(m_pAdditionalOptionsEditor, &UICloneVMAdditionalOptionsEditor::sigKeepDiskNamesToggled,
                this, &UIWizardCloneVMPageBasic1::sltKeepDiskNamesToggled);
        connect(m_pAdditionalOptionsEditor, &UICloneVMAdditionalOptionsEditor::sigKeepHardwareUUIDsToggled,
                this, &UIWizardCloneVMPageBasic1::sltKeepHardwareUUIDsToggled);
    }

    pMainLayout->addStretch();

    retranslateUi();
}

bool UIWizardCloneVMPageBasic1::isComplete() const
{
    return m_pNamePathEditor && m_pNamePathEditor->isComplete(m_strGroup);
}

void UIWizardCloneVMPageBasic1::sltCloneNameChanged(const QString &strCloneName)
{
    AssertReturnVoid(m_pNamePathEditor);
    m_userModifiedParameters << "CloneName";
    m_userModifiedParameters << "CloneFilePath";
    cloneVMWizardPropertySet(CloneName, strCloneName);
    cloneVMWizardPropertySet(CloneFilePath,
                             UIWizardCloneVMNamePage::composeCloneFilePath(strCloneName, m_strGroup, m_pNamePathEditor->clonePath()));
    emit completeChanged();
}

void UIWizardCloneVMPageBasic1::sltClonePathChanged(const QString &strClonePath)
{
    AssertReturnVoid(m_pNamePathEditor);
    m_userModifiedParameters << "CloneFilePath";
    cloneVMWizardPropertySet(CloneFilePath,
                             UIWizardCloneVMNamePage::composeCloneFilePath(m_pNamePathEditor->cloneName(), m_strGroup, strClonePath));
    emit completeChanged();
}

void UIWizardCloneVMPageBasic1::sltMACAddressClonePolicyChanged(MACAddressClonePolicy enmMACAddressClonePolicy)
{
    m_userModifiedParameters << "MacAddressPolicy";
    cloneVMWizardPropertySet(MacAddressPolicy, enmMACAddressClonePolicy);
    emit completeChanged();
}

void UIWizardCloneVMPageBasic1::sltKeepDiskNamesToggled(bool fKeepDiskNames)
{
    m_userModifiedParameters << "KeepDiskNames";
    cloneVMWizardPropertySet(KeepDiskNames, fKeepDiskNames);
    emit completeChanged();
}

void UIWizardCloneVMPageBasic1::sltKeepHardwareUUIDsToggled(bool fKeepHardwareUUIDs)
{
    m_userModifiedParameters << "KeepHardwareUUIDs";
    cloneVMWizardPropertySet(KeepHardwareUUIDs, fKeepHardwareUUIDs);
    emit completeChanged();
}

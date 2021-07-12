/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardNewVMUnattendedPageBasic class declaration.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMUnattendedPageBasic_h
#define FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMUnattendedPageBasic_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Local includes: */
#include "QIWithRetranslateUI.h"
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QCheckBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QIRichTextLabel;
class UIFilePathSelector;
class UIUserNamePasswordEditor;
class UIHostnameDomainEditor;
struct UIUnattendedInstallData;


namespace UIWizardNewVMUnattendedPage
{
    /** Returns false if ISO path selector is non empty but has invalid file path. */
    bool checkGAISOFile(UIFilePathSelector *pGAISOFilePathSelector);
}

class UIWizardNewVMUnattendedPageBasic : public UINativeWizardPage
{
    Q_OBJECT;

public:

    UIWizardNewVMUnattendedPageBasic();

protected:

    virtual void showEvent(QShowEvent *pEvent) /* override final*/;
    /** Don't reset the user entered values in case of "back" button press. */
    virtual void cleanupPage() /* override */;

private slots:

    void sltInstallGACheckBoxToggle(bool fChecked);
    void sltGAISOPathChanged(const QString &strPath);
    void sltPasswordChanged(const QString &strPassword);
    void sltUserNameChanged(const QString &strUserName);
    void sltHostnameDomainChanged(const QString &strHostname);
    void sltProductKeyChanged(const QString &strProductKey);
    void sltStartHeadlessChanged(bool fStartHeadless);

private:

    void prepare();
    void createConnections();
    QWidget *createUserNameWidgets();
    QWidget *createAdditionalOptionsWidgets();
    QWidget *createGAInstallWidgets();

    void retranslateUi();
    void initializePage();
    bool isComplete() const;
    /** Returns true if we show the widgets for guest os product key. */
    bool isProductKeyWidgetEnabled() const;
    void disableEnableProductKeyWidgets(bool fEnabled);
    void disableEnableGAWidgets(bool fEnabled);
    void markWidgets() const;

    /** @name Widgets
      * @{ */
        QIRichTextLabel *m_pLabel;
        QGroupBox *m_pUserNameContainer;
        QGroupBox *m_pAdditionalOptionsContainer;
        QGroupBox *m_pGAInstallationISOContainer;
        QCheckBox *m_pStartHeadlessCheckBox;
        UIUserNamePasswordEditor *m_pUserNamePasswordEditor;
        UIHostnameDomainEditor *m_pHostnameDomainEditor;
        // QLineEdit *m_pHostnameLineEdit;
        // QLabel    *m_pHostnameLabel;
        QLabel    *m_pGAISOPathLabel;
        UIFilePathSelector *m_pGAISOFilePathSelector;
        /** Product key stuff. */
        QLineEdit *m_pProductKeyLineEdit;
        QLabel     *m_pProductKeyLabel;
    /** @} */
    QSet<QString> m_userModifiedParameters;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMUnattendedPageBasic_h */

/* $Id$ */
/** @file
 * VBox Qt GUI - UIVideoMemoryEditor class declaration.
 */

/*
 * Copyright (C) 2019-2022 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIVideoMemoryEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIVideoMemoryEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "QIWithRetranslateUI.h"
#include "UILibraryDefs.h"

/* COM includes: */
#include "COMEnums.h"
#include "CGuestOSType.h"

/* Forward declarations: */
class QGridLayout;
class QLabel;
class QSpinBox;
class QIAdvancedSlider;

/** QWidget subclass used as a video memory editor. */
class SHARED_LIBRARY_STUFF UIVideoMemoryEditor : public QIWithRetranslateUI<QWidget>
{
    Q_OBJECT;

signals:

    /** Notifies listeners about value has became @a fValid. */
    void sigValidChanged(bool fValid);

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIVideoMemoryEditor(QWidget *pParent = 0);

    /** Defines editor @a iValue. */
    void setValue(int iValue);
    /** Returns editor value. */
    int value() const;

    /** Defines @a comGuestOSType. */
    void setGuestOSType(const CGuestOSType &comGuestOSType);

    /** Defines @a cGuestScreenCount. */
    void setGuestScreenCount(int cGuestScreenCount);

    /** Defines @a enmGraphicsControllerType. */
    void setGraphicsControllerType(const KGraphicsControllerType &enmGraphicsControllerType);

#ifdef VBOX_WITH_3D_ACCELERATION
    /** Defines whether 3D acceleration is @a fSupported. */
    void set3DAccelerationSupported(bool fSupported);
    /** Defines whether 3D acceleration is @a fEnabled. */
    void set3DAccelerationEnabled(bool fEnabled);
#endif

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE;

private slots:

    /** Handles slider value changes. */
    void sltHandleSliderChange();
    /** Handles spin-box value changes. */
    void sltHandleSpinBoxChange();

private:

    /** Prepares all. */
    void prepare();

    /** Update requirements. */
    void updateRequirements();

    /** Revalidates and emits validity change signal. */
    void revalidate();

    /** Calculates the reasonably sane slider page step. */
    static int calculatePageStep(int iMax);

    /** Holds the value to be selected. */
    int  m_iValue;

    /** @name Options
     * @{ */
        /** Holds the guest OS type ID. */
        CGuestOSType             m_comGuestOSType;
        /** Holds the guest screen count. */
        int                      m_cGuestScreenCount;
        /** Holds the graphics controller type. */
        KGraphicsControllerType  m_enmGraphicsControllerType;
#ifdef VBOX_WITH_3D_ACCELERATION
        /** Holds whether 3D acceleration is supported. */
        bool                     m_f3DAccelerationSupported;
        /** Holds whether 3D acceleration is enabled. */
        bool                     m_f3DAccelerationEnabled;
#endif

        /** Holds the minimum lower limit of VRAM (MiB). */
        int  m_iMinVRAM;
        /** Holds the maximum upper limit of VRAM (MiB). */
        int  m_iMaxVRAM;
        /** Holds the upper limit of VRAM (MiB) for this dialog.
          * @note This value is lower than m_iMaxVRAM to save
          *       careless users from setting useless big values. */
        int  m_iMaxVRAMVisible;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the main layout instance. */
        QGridLayout      *m_pLayout;
        /** Holds the memory label instance. */
        QLabel           *m_pLabelMemory;
        /** Holds the memory slider instance. */
        QIAdvancedSlider *m_pSlider;
        /** Holds minimum memory label instance. */
        QLabel           *m_pLabelMemoryMin;
        /** Holds maximum memory label instance. */
        QLabel           *m_pLabelMemoryMax;
        /** Holds the memory spin-box instance. */
        QSpinBox         *m_pSpinBox;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIVideoMemoryEditor_h */

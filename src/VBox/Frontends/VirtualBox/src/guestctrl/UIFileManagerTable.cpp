/* $Id$ */
/** @file
 * VBox Qt GUI - UIFileManagerTable class implementation.
 */

/*
 * Copyright (C) 2016-2022 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/* Qt includes: */
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemDelegate>
#include <QGridLayout>
#include <QStackedWidget>
#include <QTextEdit>

/* GUI includes: */
#include "QIDialog.h"
#include "QIDialogButtonBox.h"
#include "QILabel.h"
#include "QILineEdit.h"
#include "QIToolBar.h"
#include "QIToolButton.h"
#include "UIActionPool.h"
#include "UICommon.h"
#include "UICustomFileSystemModel.h"
#include "UIErrorString.h"
#include "UIFileManager.h"
#include "UIFileManagerGuestTable.h"
#include "UIFileManagerTable.h"
#include "UIIconPool.h"
#include "UIPathOperations.h"
#include "UITranslator.h"

/* COM includes: */
#include "CFsObjInfo.h"
#include "CGuestFsObjInfo.h"
#include "CGuestDirectory.h"
#include "CProgress.h"


/*********************************************************************************************************************************
*   UIFileManagerHistoryComboBox definition.                                                                                     *
*********************************************************************************************************************************/
/** A QCombo extension used as location history list in the UIFileManagerNavigationWidget. */
class UIFileManagerHistoryComboBox : public QComboBox
{
    Q_OBJECT;

signals:

    void  sigHidePopup();

public:

    UIFileManagerHistoryComboBox(QWidget *pParent = 0);
    /** Emit sigHidePopup as the popup is hidded. */
    virtual void hidePopup() RT_OVERRIDE;
};


/*********************************************************************************************************************************
*   UIFileManagerBreadCrumbs definition.                                                                                         *
*********************************************************************************************************************************/
/** A QLabel extension. It shows the current path as text and hightligts the folder name
  *  as the mouse hovers over it. Clicking on the highlighted folder name make the file table tp
  *  navigate to that folder. */
class UIFileManagerBreadCrumbs : public QLabel
{
    Q_OBJECT;

public:

    UIFileManagerBreadCrumbs(QWidget *pParent = 0);
    void setPath(const QString &strPath);
    void setPathSeparator(const QChar &separator);

protected:

    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

private:

    QString m_strPath;
    QChar   m_pathSeparator;
};


/*********************************************************************************************************************************
*   UIFileManagerNavigationWidget definition.                                                                                    *
*********************************************************************************************************************************/
/** UIFileManagerNavigationWidget contains a UIFileManagerBreadCrumbs, QComboBox for history and a QToolButton.
  * basically it is a container for these mentioned widgets. */
class UIFileManagerNavigationWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigPathChanged(const QString &strPath);

public:

    UIFileManagerNavigationWidget(QWidget *pParent = 0);
    void setPath(const QString &strLocation);
    void reset();
    void setPathSeparator(const QChar &separator);

private slots:

    void sltHandleSwitch();
    /* Makes sure that we switch to breadcrumbs widget as soon as the combo box popup is hidden. */
    void sltHandleHidePopup();
    void sltHandlePathChange(const QString &strPath);

private:

    void prepare();

    QStackedWidget               *m_pContainer;
    UIFileManagerBreadCrumbs     *m_pBreadCrumbs;
    UIFileManagerHistoryComboBox *m_pHistoryComboBox;
    QToolButton                  *m_pSwitchButton;
    QChar                         m_pathSeparator;
};


/*********************************************************************************************************************************
*   UIGuestControlFileView definition.                                                                                           *
*********************************************************************************************************************************/

/** Using QITableView causes the following problem when I click on the table items
    Qt WARNING: Cannot creat accessible child interface for object:  UIGuestControlFileView.....
    so for now subclass QTableView */
class UIGuestControlFileView : public QTableView
{

    Q_OBJECT;

signals:

    void sigSelectionChanged(const QItemSelection & selected, const QItemSelection & deselected);

public:

    UIGuestControlFileView(QWidget * parent = 0);
    bool hasSelection() const;
    bool isInEditState() const;

protected:

    virtual void selectionChanged(const QItemSelection & selected, const QItemSelection & deselected) /*override */;

private:

    void configure();
    QWidget *m_pParent;
};


/*********************************************************************************************************************************
*   UIFileDelegate definition.                                                                                                   *
*********************************************************************************************************************************/
/** A QItemDelegate child class to disable dashed lines drawn around selected cells in QTableViews */
class UIFileDelegate : public QItemDelegate
{

    Q_OBJECT;

public:

    UIFileDelegate(QObject *pParent)
        : QItemDelegate(pParent){}

protected:

    virtual void drawFocus ( QPainter * /*painter*/, const QStyleOptionViewItem & /*option*/, const QRect & /*rect*/ ) const {}
};


/*********************************************************************************************************************************
*   UStringInputDialog definition.                                                                                          *
*********************************************************************************************************************************/

/** A QIDialog child including a line edit whose text exposed when the dialog is accepted */
class UIStringInputDialog : public QIDialog
{

    Q_OBJECT;

public:

    UIStringInputDialog(QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());
    QString getString() const;

private:

    QILineEdit      *m_pLineEdit;
};


/*********************************************************************************************************************************
*   UIFileDeleteConfirmationDialog definition.                                                                                   *
*********************************************************************************************************************************/

/** A QIDialog child including a line edit whose text exposed when the dialog is accepted */
class UIFileDeleteConfirmationDialog : public QIDialog
{

    Q_OBJECT;

public:

    UIFileDeleteConfirmationDialog(QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());
    /** Returns whether m_pAskNextTimeCheckBox is checked or not. */
    bool askDeleteConfirmationNextTime() const;

private:

    QCheckBox *m_pAskNextTimeCheckBox;
    QILabel   *m_pQuestionLabel;
};


/*********************************************************************************************************************************
*   UIFileManagerHistoryComboBox implementation.                                                                                 *
*********************************************************************************************************************************/

UIFileManagerHistoryComboBox::UIFileManagerHistoryComboBox(QWidget *pParent /* = 0 */)
    :QComboBox(pParent)
{

}

void UIFileManagerHistoryComboBox::hidePopup()
{
    QComboBox::hidePopup();
    emit sigHidePopup();
}


/*********************************************************************************************************************************
*   UIFileManagerNavigationWidget implementation.                                                                                *
*********************************************************************************************************************************/

UIFileManagerNavigationWidget::UIFileManagerNavigationWidget(QWidget *pParent /* = 0 */)
    :QWidget(pParent)
    , m_pContainer(0)
    , m_pBreadCrumbs(0)
    , m_pHistoryComboBox(0)
    , m_pSwitchButton(0)
    , m_pathSeparator('/')
{
    prepare();
}

void UIFileManagerNavigationWidget::setPath(const QString &strLocation)
{
    if (m_pBreadCrumbs)
        m_pBreadCrumbs->setPath(strLocation);

    if (m_pHistoryComboBox)
    {
        QString strNativeLocation(strLocation);
        strNativeLocation.replace('/', m_pathSeparator);
        int itemIndex = m_pHistoryComboBox->findText(strNativeLocation,
                                                      Qt::MatchExactly | Qt::MatchCaseSensitive);
        if (itemIndex == -1)
        {
            m_pHistoryComboBox->insertItem(m_pHistoryComboBox->count(), strNativeLocation);
            itemIndex = m_pHistoryComboBox->count() - 1;
        }
        m_pHistoryComboBox->setCurrentIndex(itemIndex);
    }
}

void UIFileManagerNavigationWidget::reset()
{
    if (m_pHistoryComboBox)
    {
        m_pHistoryComboBox->blockSignals(true);
        m_pHistoryComboBox->clear();
        m_pHistoryComboBox->blockSignals(false);
    }

    if (m_pBreadCrumbs)
        m_pBreadCrumbs->setPath(QString());
}

void UIFileManagerNavigationWidget::setPathSeparator(const QChar &separator)
{
    m_pathSeparator = separator;
    if (m_pBreadCrumbs)
        m_pBreadCrumbs->setPathSeparator(m_pathSeparator);
}

void UIFileManagerNavigationWidget::prepare()
{
    QHBoxLayout *pLayout = new QHBoxLayout;
    if (!pLayout)
        return;
    pLayout->setSpacing(0);
    pLayout->setContentsMargins(0, 0, 0, 0);

    m_pContainer = new QStackedWidget;
    if (m_pContainer)
    {
        m_pBreadCrumbs = new UIFileManagerBreadCrumbs;
        m_pHistoryComboBox = new UIFileManagerHistoryComboBox;

        if (m_pBreadCrumbs && m_pHistoryComboBox)
        {
            m_pBreadCrumbs->setIndent(0.5 * qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin));
            connect(m_pBreadCrumbs, &UIFileManagerBreadCrumbs::linkActivated,
                    this, &UIFileManagerNavigationWidget::sltHandlePathChange);
            connect(m_pHistoryComboBox, &UIFileManagerHistoryComboBox::sigHidePopup,
                    this, &UIFileManagerNavigationWidget::sltHandleHidePopup);
            connect(m_pHistoryComboBox, &UIFileManagerHistoryComboBox::currentTextChanged,
                    this, &UIFileManagerNavigationWidget::sltHandlePathChange);

            m_pContainer->addWidget(m_pBreadCrumbs);
            m_pContainer->addWidget(m_pHistoryComboBox);
            m_pContainer->setCurrentIndex(0);
        }
        pLayout->addWidget(m_pContainer);
    }

    m_pSwitchButton = new QToolButton;
    if (m_pSwitchButton)
    {
        QStyle *pStyle = QApplication::style();
        QIcon buttonIcon;
        if (pStyle)
        {
            buttonIcon = pStyle->standardIcon(QStyle::SP_TitleBarUnshadeButton);
            m_pSwitchButton->setIcon(buttonIcon);
        }
        pLayout->addWidget(m_pSwitchButton);
        connect(m_pSwitchButton, &QToolButton::clicked,
                this, &UIFileManagerNavigationWidget::sltHandleSwitch);
    }
    setLayout(pLayout);
}

void UIFileManagerNavigationWidget::sltHandleHidePopup()
{
    m_pContainer->setCurrentIndex(0);
}

void UIFileManagerNavigationWidget::sltHandlePathChange(const QString &strPath)
{
    QString strNonNativePath(strPath);
    strNonNativePath.replace(m_pathSeparator, '/');
    emit sigPathChanged(strNonNativePath);
}

void UIFileManagerNavigationWidget::sltHandleSwitch()
{
    if (m_pContainer->currentIndex() == 0)
    {
        m_pContainer->setCurrentIndex(1);
        m_pHistoryComboBox->showPopup();
    }
    else
    {
        m_pContainer->setCurrentIndex(0);
        m_pHistoryComboBox->hidePopup();
    }
}


/*********************************************************************************************************************************
*   UIFileManagerBreadCrumbs implementation.                                                                                     *
*********************************************************************************************************************************/

UIFileManagerBreadCrumbs::UIFileManagerBreadCrumbs(QWidget *pParent /* = 0 */)
    :QLabel(pParent)
    , m_pathSeparator('/')
{
    float fFontMult = 1.f;
    QFont mFont = font();
    if (mFont.pixelSize() == -1)
        mFont.setPointSize(fFontMult * mFont.pointSize());
    else
        mFont.setPixelSize(fFontMult * mFont.pixelSize());
    setFont(mFont);

    setFrameShape(QFrame::Box);
    setLineWidth(1);
    setAutoFillBackground(true);
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Active, QPalette::Window, pal.color(QPalette::Active, QPalette::Base));
    setPalette(pal);
    /* Allow the label become smaller than the current text. calling setpath in resizeEvent truncated the text anyway: */
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}

void UIFileManagerBreadCrumbs::setPath(const QString &strPath)
{
    m_strPath = strPath;

    const QChar separator('/');
    clear();

    if (strPath.isEmpty())
        return;

    QStringList folderList = UIPathOperations::pathTrail(strPath);
    folderList.push_front(separator);

    QString strLabelText;
    QVector<QString> strPathUpto;
    strPathUpto.resize(folderList.size());

    for (int i = 0; i < folderList.size(); ++i)
    {
        QString strFolder = UIPathOperations::removeTrailingDelimiters(folderList.at(i));
        if (i != 0)
            strPathUpto[i] = strPathUpto[i - 1];
        if (i == 0 || i == folderList.size() - 1)
            strPathUpto[i].append(QString("%1").arg(strFolder));
        else
            strPathUpto[i].append(QString("%1%2").arg(strFolder).arg(separator));
    }

    int iWidth = 0;
    for (int i = folderList.size() - 1; i >= 0; --i)
    {
        QString strFolder = UIPathOperations::removeTrailingDelimiters(folderList.at(i)).replace('/', m_pathSeparator);
        QString strWord = QString("<a href=\"%1\" style=\"color:black;text-decoration:none;\">%2</a>").arg(strPathUpto[i]).arg(strFolder);

        if (i < folderList.size() - 1)
        {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
            iWidth += fontMetrics().horizontalAdvance(" > ");
#else
            iWidth += fontMetrics().width(" > ");
#endif
            strWord.append("<b> > </b>");
        }
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
        iWidth += fontMetrics().horizontalAdvance(strFolder);
#else
        iWidth += fontMetrics().width(strFolder);
#endif

        if (iWidth < width())
            strLabelText.prepend(strWord);
    }
    setText(strLabelText);
}

void UIFileManagerBreadCrumbs::setPathSeparator(const QChar &separator)
{
    m_pathSeparator = separator;
}

void UIFileManagerBreadCrumbs::resizeEvent(QResizeEvent *pEvent)
{
    /* Truncate the text the way we want: */
    setPath(m_strPath);
    QLabel::resizeEvent(pEvent);
}


/*********************************************************************************************************************************
*   UIHostDirectoryDiskUsageComputer implementation.                                                                             *
*********************************************************************************************************************************/

UIDirectoryDiskUsageComputer::UIDirectoryDiskUsageComputer(QObject *parent, QStringList pathList)
    :QThread(parent)
    , m_pathList(pathList)
    , m_fOkToContinue(true)
{
}

void UIDirectoryDiskUsageComputer::run()
{
    for (int i = 0; i < m_pathList.size(); ++i)
        directoryStatisticsRecursive(m_pathList[i], m_resultStatistics);
}

void UIDirectoryDiskUsageComputer::stopRecursion()
{
    m_mutex.lock();
    m_fOkToContinue = false;
    m_mutex.unlock();
}

bool UIDirectoryDiskUsageComputer::isOkToContinue() const
{
    return m_fOkToContinue;
}


/*********************************************************************************************************************************
*   UIGuestControlFileView implementation.                                                                                       *
*********************************************************************************************************************************/

UIGuestControlFileView::UIGuestControlFileView(QWidget *parent)
    :QTableView(parent)
    , m_pParent(parent)
{
    configure();
}

void UIGuestControlFileView::configure()
{
    setContextMenuPolicy(Qt::CustomContextMenu);
    setShowGrid(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    verticalHeader()->setVisible(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    /* Minimize the row height: */
    verticalHeader()->setDefaultSectionSize(verticalHeader()->minimumSectionSize());
    setAlternatingRowColors(true);
    installEventFilter(m_pParent);
}

bool UIGuestControlFileView::hasSelection() const
{
    QItemSelectionModel *pSelectionModel =  selectionModel();
    if (!pSelectionModel)
        return false;
    return pSelectionModel->hasSelection();
}

bool UIGuestControlFileView::isInEditState() const
{
    return state() == QAbstractItemView::EditingState;
}

void UIGuestControlFileView::selectionChanged(const QItemSelection & selected, const QItemSelection & deselected)
{
    emit sigSelectionChanged(selected, deselected);
    QTableView::selectionChanged(selected, deselected);
}


/*********************************************************************************************************************************
*   UIFileStringInputDialog implementation.                                                                                      *
*********************************************************************************************************************************/

UIStringInputDialog::UIStringInputDialog(QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    :QIDialog(pParent, enmFlags)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    m_pLineEdit = new QILineEdit(this);
    layout->addWidget(m_pLineEdit);

    QIDialogButtonBox *pButtonBox =
        new QIDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    layout->addWidget(pButtonBox);
    connect(pButtonBox, &QIDialogButtonBox::accepted, this, &UIStringInputDialog::accept);
    connect(pButtonBox, &QIDialogButtonBox::rejected, this, &UIStringInputDialog::reject);
}

QString UIStringInputDialog::getString() const
{
    if (!m_pLineEdit)
        return QString();
    return m_pLineEdit->text();
}


/*********************************************************************************************************************************
*   UIPropertiesDialog implementation.                                                                                           *
*********************************************************************************************************************************/

UIPropertiesDialog::UIPropertiesDialog(QWidget *pParent, Qt::WindowFlags enmFlags)
    :QIDialog(pParent, enmFlags)
    , m_pMainLayout(new QVBoxLayout)
    , m_pInfoEdit(new QTextEdit)
{
    setLayout(m_pMainLayout);

    if (m_pMainLayout)
        m_pMainLayout->addWidget(m_pInfoEdit);
    if (m_pInfoEdit)
    {
        m_pInfoEdit->setReadOnly(true);
        m_pInfoEdit->setFrameStyle(QFrame::NoFrame);
    }
    QIDialogButtonBox *pButtonBox =
        new QIDialogButtonBox(QDialogButtonBox::Ok, Qt::Horizontal, this);
    m_pMainLayout->addWidget(pButtonBox);
    connect(pButtonBox, &QIDialogButtonBox::accepted, this, &UIStringInputDialog::accept);
}

void UIPropertiesDialog::setPropertyText(const QString &strProperty)
{
    if (!m_pInfoEdit)
        return;
    m_strProperty = strProperty;
    m_pInfoEdit->setHtml(strProperty);
}

void UIPropertiesDialog::addDirectoryStatistics(UIDirectoryStatistics directoryStatistics)
{
    if (!m_pInfoEdit)
        return;
    // QString propertyString = m_pInfoEdit->toHtml();
    // propertyString += "<b>Total Size:</b> " + QString::number(directoryStatistics.m_totalSize) + QString(" bytes");
    // if (directoryStatistics.m_totalSize >= UIFileManagerTable::m_iKiloByte)
    //     propertyString += " (" + UIFileManagerTable::humanReadableSize(directoryStatistics.m_totalSize) + ")";
    // propertyString += "<br/>";
    // propertyString += "<b>File Count:</b> " + QString::number(directoryStatistics.m_uFileCount);

    // m_pInfoEdit->setHtml(propertyString);

    QString detailsString(m_strProperty);
    detailsString += "<br/>";
    detailsString += "<b>" + UIFileManager::tr("Total Size") + "</b> " +
        QString::number(directoryStatistics.m_totalSize) + UIFileManager::tr(" bytes");
    if (directoryStatistics.m_totalSize >= UIFileManagerTable::m_iKiloByte)
        detailsString += " (" + UIFileManagerTable::humanReadableSize(directoryStatistics.m_totalSize) + ")";
    detailsString += "<br/>";

    detailsString += "<b>" + UIFileManager::tr("File Count") + ":</b> " +
        QString::number(directoryStatistics.m_uFileCount);

    m_pInfoEdit->setHtml(detailsString);
}


/*********************************************************************************************************************************
*   UIDirectoryStatistics implementation.                                                                                        *
*********************************************************************************************************************************/

UIDirectoryStatistics::UIDirectoryStatistics()
    : m_totalSize(0)
    , m_uFileCount(0)
    , m_uDirectoryCount(0)
    , m_uSymlinkCount(0)
{
}

/*********************************************************************************************************************************
+*   UIFileDeleteConfirmationDialog implementation.                                                                                *
+*********************************************************************************************************************************/

UIFileDeleteConfirmationDialog::UIFileDeleteConfirmationDialog(QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    :QIDialog(pParent, enmFlags)
    , m_pAskNextTimeCheckBox(0)
    , m_pQuestionLabel(0)
{
    QVBoxLayout *pLayout = new QVBoxLayout(this);

    m_pQuestionLabel = new QILabel;
    if (m_pQuestionLabel)
    {
        pLayout->addWidget(m_pQuestionLabel);
        m_pQuestionLabel->setText(UIFileManager::tr("Delete the selected file(s) and/or folder(s)"));
    }

    QIDialogButtonBox *pButtonBox =
        new QIDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    if (pButtonBox)
    {
        pLayout->addWidget(pButtonBox, 0, Qt::AlignCenter);
        connect(pButtonBox, &QIDialogButtonBox::accepted, this, &UIStringInputDialog::accept);
        connect(pButtonBox, &QIDialogButtonBox::rejected, this, &UIStringInputDialog::reject);
    }

    m_pAskNextTimeCheckBox = new QCheckBox;

    if (m_pAskNextTimeCheckBox)
    {
        UIFileManagerOptions *pFileManagerOptions = UIFileManagerOptions::instance();
        if (pFileManagerOptions)
            m_pAskNextTimeCheckBox->setChecked(pFileManagerOptions->fAskDeleteConfirmation);

        pLayout->addWidget(m_pAskNextTimeCheckBox);
        m_pAskNextTimeCheckBox->setText(UIFileManager::tr("Ask for this confirmation next time"));
        m_pAskNextTimeCheckBox->setToolTip(UIFileManager::tr("Delete confirmation can be "
                                                                         "disabled/enabled also from the Options panel."));
    }
}

bool UIFileDeleteConfirmationDialog::askDeleteConfirmationNextTime() const
{
    if (!m_pAskNextTimeCheckBox)
        return true;
    return m_pAskNextTimeCheckBox->isChecked();
}


/*********************************************************************************************************************************
*   UIFileManagerTable implementation.                                                                                      *
*********************************************************************************************************************************/
const unsigned UIFileManagerTable::m_iKiloByte = 1024; /**< Our kilo bytes are a power of two! (bird) */

UIFileManagerTable::UIFileManagerTable(UIActionPool *pActionPool, QWidget *pParent /* = 0 */)
    :QIWithRetranslateUI<QWidget>(pParent)
    , m_eFileOperationType(FileOperationType_None)
    , m_pLocationLabel(0)
    , m_pPropertiesDialog(0)
    , m_pActionPool(pActionPool)
    , m_pToolBar(0)
    , m_pMainLayout(0)
    , m_pModel(0)
    , m_pView(0)
    , m_pProxyModel(0)
    , m_pNavigationWidget(0)
    , m_pathSeparator('/')
    , m_pToolBarLayout(0)
{
    prepareObjects();
}

UIFileManagerTable::~UIFileManagerTable()
{
}

void UIFileManagerTable::reset()
{
    if (m_pModel)
        m_pModel->reset();

    if (m_pNavigationWidget)
        m_pNavigationWidget->reset();
}

void UIFileManagerTable::prepareObjects()
{
    m_pMainLayout = new QGridLayout();
    if (!m_pMainLayout)
        return;
    m_pMainLayout->setSpacing(0);
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(m_pMainLayout);

    m_pToolBarLayout = new QHBoxLayout;
    if (m_pToolBarLayout)
    {
        m_pToolBarLayout->setSpacing(0);
        m_pToolBarLayout->setContentsMargins(0, 0, 0, 0);

        m_pToolBar = new QIToolBar;
        if (m_pToolBar)
        {
            m_pToolBarLayout->addWidget(m_pToolBar);
            m_sessionWidgets << m_pToolBar;
        }

        m_pMainLayout->addLayout(m_pToolBarLayout, 0, 0, 1, 7);
    }

    m_pLocationLabel = new QILabel;
    if (m_pLocationLabel)
    {
        m_pMainLayout->addWidget(m_pLocationLabel, 1, 0, 1, 1);
        m_sessionWidgets << m_pLocationLabel;
    }

    m_pNavigationWidget = new UIFileManagerNavigationWidget;
    if (m_pNavigationWidget)
    {
        m_pNavigationWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Maximum);
        connect(m_pNavigationWidget, &UIFileManagerNavigationWidget::sigPathChanged,
                this, &UIFileManagerTable::sltHandleNavigationWidgetPathChange);
        m_pMainLayout->addWidget(m_pNavigationWidget, 1, 1, 1, 6);
        m_sessionWidgets << m_pNavigationWidget;
    }

    m_pModel = new UICustomFileSystemModel(this);
    if (!m_pModel)
        return;
    connect(m_pModel, &UICustomFileSystemModel::sigItemRenamed,
            this, &UIFileManagerTable::sltHandleItemRenameAttempt);

    m_pProxyModel = new UICustomFileSystemProxyModel(this);
    if (!m_pProxyModel)
        return;
    m_pProxyModel->setSourceModel(m_pModel);

    m_pView = new UIGuestControlFileView(this);
    if (m_pView)
    {
        m_pView->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
        m_pMainLayout->addWidget(m_pView, 2, 0, 5, 7);

        QHeaderView *pHorizontalHeader = m_pView->horizontalHeader();
        if (pHorizontalHeader)
        {
            pHorizontalHeader->setHighlightSections(false);
            pHorizontalHeader->setSectionResizeMode(QHeaderView::ResizeToContents);
            pHorizontalHeader->setStretchLastSection(true);
        }

        m_pView->setModel(m_pProxyModel);
        m_pView->setItemDelegate(new UIFileDelegate(this));
        m_pView->setSortingEnabled(true);
        m_pView->sortByColumn(0, Qt::AscendingOrder);

        connect(m_pView, &UIGuestControlFileView::doubleClicked,
                this, &UIFileManagerTable::sltItemDoubleClicked);
        connect(m_pView, &UIGuestControlFileView::clicked,
                this, &UIFileManagerTable::sltItemClicked);
        connect(m_pView, &UIGuestControlFileView::sigSelectionChanged,
                this, &UIFileManagerTable::sltSelectionChanged);
        connect(m_pView, &UIGuestControlFileView::customContextMenuRequested,
                this, &UIFileManagerTable::sltCreateFileViewContextMenu);
        m_pView->hideColumn(UICustomFileSystemModelColumn_Path);
        m_pView->hideColumn(UICustomFileSystemModelColumn_LocalPath);
        m_sessionWidgets << m_pView;
    }

    m_pSearchLineEdit = new QILineEdit;
    if (m_pSearchLineEdit)
    {
        m_pMainLayout->addWidget(m_pSearchLineEdit, 8, 0, 1, 7);
        m_pSearchLineEdit->hide();
        m_pSearchLineEdit->setClearButtonEnabled(true);
        m_searchLineUnmarkColor = m_pSearchLineEdit->palette().color(QPalette::Base);
        m_searchLineMarkColor = QColor(m_searchLineUnmarkColor.green(),
                                       0.5 * m_searchLineUnmarkColor.green(),
                                       0.5 * m_searchLineUnmarkColor.blue());
        connect(m_pSearchLineEdit, &QLineEdit::textChanged,
                this, &UIFileManagerTable::sltSearchTextChanged);
    }
    optionsUpdated();
}

void UIFileManagerTable::updateCurrentLocationEdit(const QString& strLocation)
{
    if (m_pNavigationWidget)
    {
        m_pNavigationWidget->setPath(strLocation);
    }
}

void UIFileManagerTable::changeLocation(const QModelIndex &index)
{
    if (!index.isValid() || !m_pView)
        return;
    m_pView->setRootIndex(m_pProxyModel->mapFromSource(index));

    if (m_pView->selectionModel())
        m_pView->selectionModel()->reset();

    UICustomFileSystemItem *item = static_cast<UICustomFileSystemItem*>(index.internalPointer());
    if (item)
    {
        updateCurrentLocationEdit(item->path());
    }
    setSelectionDependentActionsEnabled(false);

    m_pView->scrollToTop();
}

void UIFileManagerTable::initializeFileTree()
{
    if (m_pModel)
        m_pModel->reset();
    if (!rootItem())
        return;

    const QString startPath("/");
    UICustomFileSystemItem* startItem = new UICustomFileSystemItem(startPath, rootItem(), KFsObjType_Directory);
    startItem->setPath(startPath);
    startItem->setIsOpened(false);
    populateStartDirectory(startItem);

    m_pModel->signalUpdate();
    updateCurrentLocationEdit(startPath);
    m_pView->setRootIndex(m_pProxyModel->mapFromSource(m_pModel->rootIndex()));
}

void UIFileManagerTable::populateStartDirectory(UICustomFileSystemItem *startItem)
{
    determineDriveLetters();
    if (m_driveLetterList.isEmpty())
    {
        /* Read the root directory and get the list: */
        readDirectory(startItem->path(), startItem, true);
    }
    else
    {
        for (int i = 0; i < m_driveLetterList.size(); ++i)
        {
            UICustomFileSystemItem* driveItem = new UICustomFileSystemItem(UIPathOperations::removeTrailingDelimiters(m_driveLetterList[i]),
                                                                           startItem, KFsObjType_Directory);
            driveItem->setPath(m_driveLetterList[i]);
            driveItem->setIsOpened(false);
            driveItem->setIsDriveItem(true);
            startItem->setIsOpened(true);
        }
    }
}

void UIFileManagerTable::checkDotDot(QMap<QString,UICustomFileSystemItem*> &map,
                                     UICustomFileSystemItem *parent, bool isStartDir)
{
    if (!parent)
        return;
    /* Make sure we have an item representing up directory, and make sure it is not there for the start dir: */
    if (!map.contains(UICustomFileSystemModel::strUpDirectoryString)  && !isStartDir)
    {
        UICustomFileSystemItem *item = new UICustomFileSystemItem(UICustomFileSystemModel::strUpDirectoryString,
                                                                  parent, KFsObjType_Directory);
        item->setIsOpened(false);
        map.insert(UICustomFileSystemModel::strUpDirectoryString, item);
    }
    else if (map.contains(UICustomFileSystemModel::strUpDirectoryString)  && isStartDir)
    {
        map.remove(UICustomFileSystemModel::strUpDirectoryString);
    }
}

void UIFileManagerTable::sltItemDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid() ||  !m_pModel || !m_pView)
        return;
    QModelIndex nIndex = m_pProxyModel ? m_pProxyModel->mapToSource(index) : index;
    goIntoDirectory(nIndex);
}

void UIFileManagerTable::sltItemClicked(const QModelIndex &index)
{
    Q_UNUSED(index);
    disableSelectionSearch();
}

void UIFileManagerTable::sltGoUp()
{
    if (!m_pView || !m_pModel)
        return;
    QModelIndex currentRoot = currentRootIndex();

    if (!currentRoot.isValid())
        return;
    if (currentRoot != m_pModel->rootIndex())
    {
        QModelIndex parentIndex = currentRoot.parent();
        if (parentIndex.isValid())
        {
            changeLocation(currentRoot.parent());
            m_pView->selectRow(currentRoot.row());
        }
    }
}

void UIFileManagerTable::sltGoHome()
{
    goToHomeDirectory();
}

void UIFileManagerTable::sltRefresh()
{
    refresh();
}

void UIFileManagerTable::goIntoDirectory(const QModelIndex &itemIndex)
{
    if (!m_pModel)
        return;

    /* Make sure the colum is 0: */
    QModelIndex index = m_pModel->index(itemIndex.row(), 0, itemIndex.parent());
    if (!index.isValid())
        return;

    UICustomFileSystemItem *item = static_cast<UICustomFileSystemItem*>(index.internalPointer());
    if (!item)
        return;

    /* check if we need to go up: */
    if (item->isUpDirectory())
    {
        QModelIndex parentIndex = m_pModel->parent(m_pModel->parent(index));
        if (parentIndex.isValid())
            changeLocation(parentIndex);
        return;
    }

    if (item->isDirectory() || item->isSymLinkToADirectory())
    {
        if (!item->isOpened())
            readDirectory(item->path(),item);
        changeLocation(index);
    }
}

void UIFileManagerTable::goIntoDirectory(const QStringList &pathTrail)
{
    UICustomFileSystemItem *parent = getStartDirectoryItem();

    for(int i = 0; i < pathTrail.size(); ++i)
    {
        if (!parent)
            return;
        /* Make sure parent is already opened: */
        if (!parent->isOpened())
            readDirectory(parent->path(), parent, parent == getStartDirectoryItem());
        /* search the current path item among the parent's children: */
        UICustomFileSystemItem *item = parent->child(pathTrail.at(i));
        if (!item)
            return;
        parent = item;
    }
    if (!parent)
        return;
    if (!parent->isOpened())
        readDirectory(parent->path(), parent, parent == getStartDirectoryItem());
    goIntoDirectory(parent);
}

void UIFileManagerTable::goIntoDirectory(UICustomFileSystemItem *item)
{
    if (!item || !m_pModel)
        return;
    goIntoDirectory(m_pModel->index(item));
}

UICustomFileSystemItem* UIFileManagerTable::indexData(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;
    return static_cast<UICustomFileSystemItem*>(index.internalPointer());
}

void UIFileManagerTable::refresh()
{
    if (!m_pView || !m_pModel)
        return;
    QModelIndex currentIndex = currentRootIndex();

    UICustomFileSystemItem *treeItem = indexData(currentIndex);
    if (!treeItem)
        return;
    bool isRootDir = (m_pModel->rootIndex() == currentIndex);
    m_pModel->beginReset();
    /* For now we clear the whole subtree (that isrecursively) which is an overkill: */
    treeItem->clearChildren();
    if (isRootDir)
        populateStartDirectory(treeItem);
    else
        readDirectory(treeItem->path(), treeItem, isRootDir);
    m_pModel->endReset();
    m_pView->setRootIndex(m_pProxyModel->mapFromSource(currentIndex));
    setSelectionDependentActionsEnabled(m_pView->hasSelection());
}

void UIFileManagerTable::relist()
{
    if (!m_pProxyModel)
        return;
    m_pProxyModel->invalidate();
}

void UIFileManagerTable::sltDelete()
{
    if (!checkIfDeleteOK())
        return;

    if (!m_pView || !m_pModel)
        return;

    if (!m_pView || !m_pModel)
        return;
    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
    if (!selectionModel)
        return;

    QModelIndexList selectedItemIndices = selectionModel->selectedRows();
    for(int i = 0; i < selectedItemIndices.size(); ++i)
    {
        QModelIndex index =
            m_pProxyModel ? m_pProxyModel->mapToSource(selectedItemIndices.at(i)) : selectedItemIndices.at(i);
        deleteByIndex(index);
    }
    /** @todo dont refresh here, just delete the rows and update the table view: */
    refresh();
}

void UIFileManagerTable::sltRename()
{
    if (!m_pView || !m_pModel)
        return;
    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
    if (!selectionModel)
        return;

    QModelIndexList selectedItemIndices = selectionModel->selectedRows();
    if (selectedItemIndices.size() == 0)
        return;
    QModelIndex modelIndex =
        m_pProxyModel ? m_pProxyModel->mapToSource(selectedItemIndices.at(0)) : selectedItemIndices.at(0);
    UICustomFileSystemItem *item = indexData(modelIndex);
    if (!item || item->isUpDirectory())
        return;
    m_pView->edit(selectedItemIndices.at(0));
}

void UIFileManagerTable::sltCreateNewDirectory()
{
    if (!m_pModel || !m_pView)
        return;
    QModelIndex currentIndex = currentRootIndex();
    if (!currentIndex.isValid())
        return;
    UICustomFileSystemItem *parentFolderItem = static_cast<UICustomFileSystemItem*>(currentIndex.internalPointer());
    if (!parentFolderItem)
        return;

    QString newDirectoryName(UICustomFileSystemModel::tr("New Directory"));

    if (!createDirectory(parentFolderItem->path(), newDirectoryName))
        return;

    /* Refesh the current directory so that we correctly populate the child list of parentFolderItem: */
    /** @todo instead of refreshing here (an overkill) just add the
        rows and update the tabel view: */
    sltRefresh();

    /* Now we try to edit the newly created item thereby enabling the user to rename the new item: */
    QList<UICustomFileSystemItem*> content = parentFolderItem->children();
    UICustomFileSystemItem* newItem = 0;
    /* Search the new item: */
    foreach (UICustomFileSystemItem* childItem, content)
    {

        if (childItem && newDirectoryName == childItem->name())
            newItem = childItem;
    }

    if (!newItem)
        return;
    QModelIndex newItemIndex = m_pProxyModel->mapFromSource(m_pModel->index(newItem));
    if (!newItemIndex.isValid())
        return;
    m_pView->edit(newItemIndex);
}

void UIFileManagerTable::sltCopy()
{
    m_copyCutBuffer = selectedItemPathList();
    m_eFileOperationType = FileOperationType_Copy;
    setPasteActionEnabled(true);
}

void UIFileManagerTable::sltCut()
{
    m_copyCutBuffer = selectedItemPathList();
    m_eFileOperationType = FileOperationType_Cut;
    setPasteActionEnabled(true);
}

void UIFileManagerTable::sltPaste()
{
    m_copyCutBuffer.clear();

    m_eFileOperationType = FileOperationType_None;
    setPasteActionEnabled(false);
}

void UIFileManagerTable::sltShowProperties()
{
    showProperties();
}

void UIFileManagerTable::sltSelectionChanged(const QItemSelection & selected, const QItemSelection & deselected)
{
    Q_UNUSED(selected);
    Q_UNUSED(deselected);
    setSelectionDependentActionsEnabled(m_pView->hasSelection());
}

void UIFileManagerTable::sltSelectAll()
{
    if (!m_pModel || !m_pView)
        return;
    m_pView->selectAll();
    deSelectUpDirectoryItem();
}

void UIFileManagerTable::sltInvertSelection()
{
    setSelectionForAll(QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    deSelectUpDirectoryItem();
}

void UIFileManagerTable::sltSearchTextChanged(const QString &strText)
{
    performSelectionSearch(strText);
}

void UIFileManagerTable::sltHandleItemRenameAttempt(UICustomFileSystemItem *pItem, QString strOldName, QString strNewName)
{
    if (!pItem)
        return;
    /* Attempt to chage item name in the file system: */
    if (!renameItem(pItem, strNewName))
    {
        /* Restore the previous name. relist the view: */
        pItem->setData(strOldName, static_cast<int>(UICustomFileSystemModelColumn_Name));
        relist();
        emit sigLogOutput(QString(pItem->path()).append(" could not be renamed"), QString(), FileManagerLogType_Error);
    }
}

void UIFileManagerTable::sltCreateFileViewContextMenu(const QPoint &point)
{
    QWidget *pSender = qobject_cast<QWidget*>(sender());
    if (!pSender)
        return;
    createFileViewContextMenu(pSender, point);
}

void UIFileManagerTable::sltHandleNavigationWidgetPathChange(const QString& strPath)
{
    goIntoDirectory(UIPathOperations::pathTrail(strPath));
}

void UIFileManagerTable::deSelectUpDirectoryItem()
{
    if (!m_pView)
        return;
    QItemSelectionModel *pSelectionModel = m_pView->selectionModel();
    if (!pSelectionModel)
        return;
    QModelIndex currentRoot = currentRootIndex();
    if (!currentRoot.isValid())
        return;

    /* Make sure that "up directory item" (if exists) is deselected: */
    for (int i = 0; i < m_pModel->rowCount(currentRoot); ++i)
    {
        QModelIndex index = m_pModel->index(i, 0, currentRoot);
        if (!index.isValid())
            continue;

        UICustomFileSystemItem *item = static_cast<UICustomFileSystemItem*>(index.internalPointer());
        if (item && item->isUpDirectory())
        {
            QModelIndex indexToDeselect = m_pProxyModel ? m_pProxyModel->mapFromSource(index) : index;
            pSelectionModel->select(indexToDeselect, QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
        }
    }
}

void UIFileManagerTable::setSelectionForAll(QItemSelectionModel::SelectionFlags flags)
{
    if (!m_pView)
        return;
    QItemSelectionModel *pSelectionModel = m_pView->selectionModel();
    if (!pSelectionModel)
        return;
    QModelIndex currentRoot = currentRootIndex();
    if (!currentRoot.isValid())
        return;

    for (int i = 0; i < m_pModel->rowCount(currentRoot); ++i)
    {
        QModelIndex index = m_pModel->index(i, 0, currentRoot);
        if (!index.isValid())
            continue;
        QModelIndex indexToSelect = m_pProxyModel ? m_pProxyModel->mapFromSource(index) : index;
        pSelectionModel->select(indexToSelect, flags);
    }
}

void UIFileManagerTable::setSelection(const QModelIndex &indexInProxyModel)
{
    if (!m_pView)
        return;
    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
    if (!selectionModel)
        return;
    selectionModel->select(indexInProxyModel, QItemSelectionModel::Current | QItemSelectionModel::Rows | QItemSelectionModel::Select);
    m_pView->scrollTo(indexInProxyModel, QAbstractItemView::EnsureVisible);
}

void UIFileManagerTable::deleteByIndex(const QModelIndex &itemIndex)
{
    UICustomFileSystemItem *treeItem = indexData(itemIndex);
    if (!treeItem)
        return;
    deleteByItem(treeItem);
}

void UIFileManagerTable::retranslateUi()
{
    UICustomFileSystemItem *pRootItem = rootItem();
    if (pRootItem)
    {
        pRootItem->setData(UIFileManager::tr("Name"), UICustomFileSystemModelColumn_Name);
        pRootItem->setData(UIFileManager::tr("Size"), UICustomFileSystemModelColumn_Size);
        pRootItem->setData(UIFileManager::tr("Change Time"), UICustomFileSystemModelColumn_ChangeTime);
        pRootItem->setData(UIFileManager::tr("Owner"), UICustomFileSystemModelColumn_Owner);
        pRootItem->setData(UIFileManager::tr("Permissions"), UICustomFileSystemModelColumn_Permissions);
    }
}

bool UIFileManagerTable::eventFilter(QObject *pObject, QEvent *pEvent) /* override */
{
    /* Handle only events sent to m_pView: */
    if (pObject != m_pView)
        return QIWithRetranslateUI<QWidget>::eventFilter(pObject, pEvent);

    if (pEvent->type() == QEvent::KeyPress)
    {
        QKeyEvent *pKeyEvent = dynamic_cast<QKeyEvent*>(pEvent);
        if (pKeyEvent)
        {
            if (pKeyEvent->key() == Qt::Key_Enter || pKeyEvent->key() == Qt::Key_Return)
            {
                if (m_pView && m_pModel && !m_pView->isInEditState())
                {
                    /* Get the selected item. If there are 0 or more than 1 selection do nothing: */
                    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
                    if (selectionModel)
                    {
                        QModelIndexList selectedItemIndices = selectionModel->selectedRows();
                        if (selectedItemIndices.size() == 1 && m_pModel)
                            goIntoDirectory( m_pProxyModel->mapToSource(selectedItemIndices.at(0)));
                    }
                }
                return true;
            }
            else if (pKeyEvent->key() == Qt::Key_Delete)
            {
                sltDelete();
                return true;
            }
            else if (pKeyEvent->key() == Qt::Key_Backspace)
            {
                sltGoUp();
                return true;
            }
            else if (pKeyEvent->text().length() == 1 &&
                     (pKeyEvent->text().at(0).isDigit() ||
                      pKeyEvent->text().at(0).isLetter()))
            {
                if (m_pSearchLineEdit)
                {
                    markUnmarkSearchLineEdit(false);
                    m_pSearchLineEdit->clear();
                    m_pSearchLineEdit->show();
                    m_pSearchLineEdit->setFocus();
                    m_pSearchLineEdit->setText(pKeyEvent->text());
                }
            }
            else if (pKeyEvent->key() == Qt::Key_Tab)
            {
                return true;
            }
        }
    }
    else if (pEvent->type() == QEvent::FocusOut)
    {
        disableSelectionSearch();
    }

    /* Call to base-class: */
    return QIWithRetranslateUI<QWidget>::eventFilter(pObject, pEvent);
}

UICustomFileSystemItem *UIFileManagerTable::getStartDirectoryItem()
{
    UICustomFileSystemItem* pRootItem = rootItem();
    if (!pRootItem)
        return 0;
    if (pRootItem->childCount() <= 0)
        return 0;
    return pRootItem->child(0);
}

QString UIFileManagerTable::currentDirectoryPath() const
{
    if (!m_pView)
        return QString();
    QModelIndex currentRoot = currentRootIndex();
    if (!currentRoot.isValid())
        return QString();
    UICustomFileSystemItem *item = static_cast<UICustomFileSystemItem*>(currentRoot.internalPointer());
    if (!item)
        return QString();
    /* be paranoid: */
    if (!item->isDirectory())
        return QString();
    return item->path();
}

QStringList UIFileManagerTable::selectedItemPathList()
{
    QItemSelectionModel *selectionModel =  m_pView->selectionModel();
    if (!selectionModel)
        return QStringList();

    QStringList pathList;
    QModelIndexList selectedItemIndices = selectionModel->selectedRows();
    for(int i = 0; i < selectedItemIndices.size(); ++i)
    {
        QModelIndex index =
            m_pProxyModel ? m_pProxyModel->mapToSource(selectedItemIndices.at(i)) : selectedItemIndices.at(i);
        UICustomFileSystemItem *item = static_cast<UICustomFileSystemItem*>(index.internalPointer());
        if (!item)
            continue;

        /* Make sure to remove any trailing delimiters for directory paths here (e.g. "C:\foo\bar\" -> "C:\foo\bar"),
         * as we want to copy entire directories, not only its contents (see Guest Control SDK docs). */
        pathList.push_back(item->path(true /* fRemoveTrailingDelimiters */));
    }
    return pathList;
}

CGuestFsObjInfo UIFileManagerTable::guestFsObjectInfo(const QString& path, CGuestSession &comGuestSession) const
{
    if (comGuestSession.isNull())
        return CGuestFsObjInfo();
    CGuestFsObjInfo comFsObjInfo = comGuestSession.FsObjQueryInfo(path, true /*aFollowSymlinks*/);
    if (!comFsObjInfo.isOk())
        return CGuestFsObjInfo();
    return comFsObjInfo;
}

void UIFileManagerTable::setSelectionDependentActionsEnabled(bool fIsEnabled)
{
    foreach (QAction *pAction, m_selectionDependentActions)
        pAction->setEnabled(fIsEnabled);
    if (m_pView)
        emit sigSelectionChanged(m_pView->hasSelection());
}

UICustomFileSystemItem* UIFileManagerTable::rootItem()
{
    if (!m_pModel)
        return 0;
    return m_pModel->rootItem();
}

void UIFileManagerTable::setPathSeparator(const QChar &separator)
{
    m_pathSeparator = separator;
    if (m_pNavigationWidget)
        m_pNavigationWidget->setPathSeparator(m_pathSeparator);
}

QHBoxLayout* UIFileManagerTable::toolBarLayout()
{
    return m_pToolBarLayout;
}

bool UIFileManagerTable::event(QEvent *pEvent)
{
    if (pEvent->type() == QEvent::EnabledChange)
        retranslateUi();
    return QIWithRetranslateUI<QWidget>::event(pEvent);
}

QString UIFileManagerTable::fileTypeString(KFsObjType type)
{
    QString strType = UIFileManager::tr("Unknown");
    switch (type)
    {
        case KFsObjType_File:
            strType = UIFileManager::tr("File");
            break;
        case KFsObjType_Directory:
            strType = UIFileManager::tr("Directory");
            break;
        case KFsObjType_Symlink:
            strType = UIFileManager::tr("Symbolic Link");
            break;
        case KFsObjType_Unknown:
        default:
            strType = UIFileManager::tr("Unknown");
            break;
    }
    return strType;
}

/* static */ QString UIFileManagerTable::humanReadableSize(ULONG64 size)
{
    return UITranslator::formatSize(size);
}

void UIFileManagerTable::optionsUpdated()
{
    UIFileManagerOptions *pOptions = UIFileManagerOptions::instance();
    if (pOptions)
    {
        if (m_pProxyModel)
        {
            m_pProxyModel->setListDirectoriesOnTop(pOptions->fListDirectoriesOnTop);
            m_pProxyModel->setShowHiddenObjects(pOptions->fShowHiddenObjects);
        }
        if (m_pModel)
            m_pModel->setShowHumanReadableSizes(pOptions->fShowHumanReadableSizes);
    }
    relist();
}

bool UIFileManagerTable::hasSelection() const
{
    if (m_pView)
        return m_pView->hasSelection();
    return false;
}

void UIFileManagerTable::sltReceiveDirectoryStatistics(UIDirectoryStatistics statistics)
{
    if (!m_pPropertiesDialog)
        return;
    m_pPropertiesDialog->addDirectoryStatistics(statistics);
}

QModelIndex UIFileManagerTable::currentRootIndex() const
{
    if (!m_pView)
        return QModelIndex();
    if (!m_pProxyModel)
        return m_pView->rootIndex();
    return m_pProxyModel->mapToSource(m_pView->rootIndex());
}

void UIFileManagerTable::performSelectionSearch(const QString &strSearchText)
{
    if (!m_pProxyModel | !m_pView)
        return;

    if (strSearchText.isEmpty())
    {
        markUnmarkSearchLineEdit(false);
        return;
    }

    int rowCount = m_pProxyModel->rowCount(m_pView->rootIndex());
    UICustomFileSystemItem *pFoundItem = 0;
    QModelIndex index;
    for (int i = 0; i < rowCount && !pFoundItem; ++i)
    {
        index = m_pProxyModel->index(i, 0, m_pView->rootIndex());
        if (!index.isValid())
            continue;
        pFoundItem = static_cast<UICustomFileSystemItem*>(m_pProxyModel->mapToSource(index).internalPointer());
        if (!pFoundItem)
            continue;
        const QString &strName = pFoundItem->name();
        if (!strName.startsWith(m_pSearchLineEdit->text(), Qt::CaseInsensitive))
            pFoundItem = 0;
    }
    if (pFoundItem)
    {
        /* Deselect anything that is already selected: */
        m_pView->clearSelection();
        setSelection(index);
    }
    markUnmarkSearchLineEdit(!pFoundItem);
}

void UIFileManagerTable::disableSelectionSearch()
{
    if (!m_pSearchLineEdit)
        return;
    m_pSearchLineEdit->blockSignals(true);
    m_pSearchLineEdit->clear();
    m_pSearchLineEdit->hide();
    m_pSearchLineEdit->blockSignals(false);
}

bool UIFileManagerTable::checkIfDeleteOK()
{
    UIFileManagerOptions *pFileManagerOptions = UIFileManagerOptions::instance();
    if (!pFileManagerOptions)
        return true;
    if (!pFileManagerOptions->fAskDeleteConfirmation)
        return true;
    UIFileDeleteConfirmationDialog *pDialog =
        new UIFileDeleteConfirmationDialog(this);

    bool fContinueWithDelete = (pDialog->execute() == QDialog::Accepted);
    bool bAskNextTime = pDialog->askDeleteConfirmationNextTime();

    /* Update the file manager options only if it is necessary: */
    if (pFileManagerOptions->fAskDeleteConfirmation != bAskNextTime)
    {
        pFileManagerOptions->fAskDeleteConfirmation = bAskNextTime;
        /* Notify file manager options panel so that the check box there is updated: */
        emit sigDeleteConfirmationOptionChanged();
    }

    delete pDialog;

    return fContinueWithDelete;

}

void UIFileManagerTable::markUnmarkSearchLineEdit(bool fMark)
{
    if (!m_pSearchLineEdit)
        return;
    QPalette palette = m_pSearchLineEdit->palette();

    if (fMark)
        palette.setColor(QPalette::Base, m_searchLineMarkColor);
    else
        palette.setColor(QPalette::Base, m_searchLineUnmarkColor);
    m_pSearchLineEdit->setPalette(palette);
}

void UIFileManagerTable::setSessionWidgetsEnabled(bool fEnabled)
{
    foreach (QWidget *pWidget, m_sessionWidgets)
    {
        if (pWidget)
            pWidget->setEnabled(fEnabled);
    }
}
#include "UIFileManagerTable.moc"

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "wavcombine.h"
#include "wavextract.h"
#include <QMessageBox>
#include <QValidator>
#include <kira/lib_helper/kfr_helper.h>
#include "wavcombinedialog.h"
#include "wavextractdialog.h"
#include <QPalette>
#include <kira/i18n/translationmanager.h>
#include <kira/darkmode.h>
#include <kira/updatechecker.h>

QMenu* MainWindow::createHelpMenu()
{
    auto helpMenu = new QMenu(this);
    auto aboutAction = helpMenu->addAction(tr("About"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::about);
    helpMenu->addSeparator();
    helpMenu->addMenu(UpdateChecker::createMenuForSchedule());
    auto checkUpdateAction = helpMenu->addAction(tr("Check update now"));
    connect(checkUpdateAction, &QAction::triggered, this, [this](){
        UpdateChecker::checkManully(updateChecker);
    });

    return helpMenu;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setFixedHeight(sizeHint().height());
    setWindowFlags(Qt::Window | Qt::CustomizeWindowHint |
                   Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint |
                   Qt::WindowCloseButtonHint);

    updateChecker = new UpdateChecker::GithubReleaseChecker("shine5402", "KiraWAVTar");
    UpdateChecker::triggerScheduledCheck(updateChecker);

    connect(ui->resetButton, &QPushButton::clicked, this, &MainWindow::reset);
    connect(ui->modeButtonGroup, &QButtonGroup::idClicked, this, &MainWindow::updateStackWidgetIndex);
    connect(ui->runButton, &QPushButton::clicked, this, &MainWindow::run);
    //connect(ui->aboutButton, &QPushButton::clicked, this, &MainWindow::about);

    auto helpMenu = createHelpMenu();
    ui->helpButton->setMenu(helpMenu);

    connect(ui->combineDirPathWidget, &DirNameEditWithBrowse::browseTriggered, this, &MainWindow::fillResultPath);
    connect(ui->combineDirPathWidget, &DirNameEditWithBrowse::dropTriggered, this, &MainWindow::fillResultPath);
    connect(ui->extractSrcPathWidget, &FileNameEditWithBrowse::browseTriggered, this, &MainWindow::fillResultPath);
    connect(ui->extractSrcPathWidget, &FileNameEditWithBrowse::dropTriggered, this, &MainWindow::fillResultPath);

    //Make line separator looks nicer
    QPalette linePalette;
    linePalette.setColor(QPalette::WindowText, linePalette.color(QPalette::Dark));
    ui->leftBottomButtonLine->setPalette(linePalette);

    //i18n menu
    ui->langButton->setMenu(TranslationManager::getManager()->getI18nMenu());

    //ui theme menu
    ui->uiThemeButton->setMenu(DarkMode::getDarkModeSettingMenu());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::reset()
{
    ui->combineDirPathWidget->setDirName({});
    ui->subdirCheckBox->setChecked(true);
    ui->combineResultPathWidget->setFileName({});
    ui->combineWAVFormatWidget->reset();

    ui->extractSrcPathWidget->setFileName({});
    ui->extractResultPathWidget->setDirName({});
    ui->extractFormatSrcRadioButton->setChecked(true);
    ui->extractFormatCustomChooser->reset();
    ui->extractSelectionCheckBox->setChecked(false);
}

void MainWindow::updateStackWidgetIndex()
{
    if (ui->combineWAVRadioButton->isChecked())
        ui->optionStackedWidget->setCurrentIndex(0);//Combine
    else
        ui->optionStackedWidget->setCurrentIndex(1);//Extract

    reset();
}

void MainWindow::run()
{
    if (ui->combineWAVRadioButton->isChecked())
    {
        auto rootDirName = ui->combineDirPathWidget->dirName();
        auto recursive = ui->subdirCheckBox->isChecked();
        auto targetFormat = ui->combineWAVFormatWidget->getFormat();
        auto saveFileName = ui->combineResultPathWidget->fileName();

        if (rootDirName.isEmpty() || saveFileName.isEmpty()){
            QMessageBox::critical(this, {}, tr("Needed paths are empty. Please check your input and try again."));
            return;
        }

        auto dialog = new WAVCombineDialog(rootDirName, recursive, targetFormat, saveFileName, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        dialog->open();
    }
    else
    {
        constexpr auto invalidFormat = kfr::audio_format{0, kfr::audio_sample_type::unknown, 0, kfr::audio_format::riff};
        auto targetFormat = ui->extractFormatSrcRadioButton->isChecked() ? invalidFormat : ui->extractFormatCustomChooser->getFormat();
        auto srcWAVFileName = ui->extractSrcPathWidget->fileName();
        auto dstDirName = ui->extractResultPathWidget->dirName();
        auto extractResultSelection = ui->extractSelectionCheckBox->isChecked();
        auto removeDCOffset = ui->removeDCCheckBox->isChecked();
        if (srcWAVFileName.isEmpty() || dstDirName.isEmpty()){
            QMessageBox::critical(this, {}, tr("Needed paths are empty. Please check your input and try again."));
            return;
        }
        auto dialog = new WAVExtractDialog(srcWAVFileName, dstDirName, targetFormat, extractResultSelection, removeDCOffset, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        dialog->open();
    }

}

void MainWindow::fillResultPath()
{
    if (ui->combineWAVRadioButton->isChecked())
        //TODO: remove last (if exists) "/" and "\" on dirName, as on specific platform, they will have this on the end.
        ui->combineResultPathWidget->setFileName(ui->combineDirPathWidget->dirName() + ".wav");
    else
    {
        auto fileInfo = QFileInfo{ui->extractSrcPathWidget->fileName()};
        auto dir = fileInfo.absoluteDir();
        auto baseFileName = fileInfo.completeBaseName();
        ui->extractResultPathWidget->setDirName(dir.absoluteFilePath(baseFileName + "_result"));
    }
}

void MainWindow::about()
{
    auto isBeta = QStringLiteral(GIT_BRANCH) == QStringLiteral("dev");
    QString versionStr = tr("<p>Version %1%4, <i>branch: %2, commit: %3, build on %5 %6<i></p>")
            .arg(qApp->applicationVersion(), GIT_BRANCH, GIT_HASH, isBeta ? "-beta" : "", __DATE__, __TIME__);
    if (isBeta)
        versionStr += tr("<p style=\"color:orange\">You are using a BETA build. "
                         "<b>Use it at your own risk.</b>"
                         " If any problems occured, please provide feedback on Github Issues.</p>");

    QMessageBox msgBox;
    msgBox.setIconPixmap(QPixmap(":/icon/appIcon").scaled(64,64,Qt::KeepAspectRatio, Qt::SmoothTransformation));
    msgBox.setWindowTitle(tr("About"));
    msgBox.setText(tr(
                       R"(<h2>KiraWAVTar</h2>
<p>Copyright 2021-2022 <a href="https://shine5402.top/about-me">shine_5402</a></p>
%1
<h3>About</h3>
<p>A fast and easy-to-use WAV combine/extract tool.</p>
<h3>License</h3>
<p> This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.<br>
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.<br>
You should have received a copy of the GNU General Public License
along with this program.  If not, see <a href="https://www.gnu.org/licenses/">https://www.gnu.org/licenses/</a>.<br>
 In addition, as a special exception, the copyright holders give
 permission to link the code of portions of this program with the
 OpenSSL library under certain conditions as described in each
 individual source file, and distribute linked combinations
 including the two.<br>
 You must obey the GNU General Public License in all respects
 for all of the code used other than OpenSSL.  If you modify
 file(s) with this exception, you may extend this exception to your
 version of the file(s), but you are not obligated to do so.  If you
 do not wish to do so, delete this exception statement from your
 version.  If you delete this exception statement from all source
 files in the program, then also delete it here.</p>

<h3>3rd party libraries used by this project</h3>
<ul>
<li>Qt %2, The Qt Company Ltd, under LGPL v3.</li>
<li><a href="https://www.kfrlib.com/">KFR - Fast, modern C++ DSP framework</a>, under GNU GPL v2+, <a href="https://github.com/shine5402/kfr">using our own modified version</a></li>
<li><a href="https://github.com/shine5402/KiraCommonUtils">KiraCommonUtils</a>, shine_5402, mainly under the Apache License, Version 2.0</li>
<li><a href="https://github.com/mapbox/eternal">eternal</a>, mapbox, under ISC License</li>
<li>This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit. (<a href='http://www.openssl.org'>http://www.openssl.org/</a>)</li>
</ul>
)").arg(versionStr).arg(QT_VERSION_STR));
    msgBox.exec();
}

void MainWindow::changeEvent(QEvent* event){
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        ui->helpButton->menu()->deleteLater();
        ui->helpButton->setMenu(createHelpMenu());
        ui->uiThemeButton->setMenu(DarkMode::getDarkModeSettingMenu());
    }
}

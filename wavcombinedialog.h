#ifndef WAVCOMBINEDIALOG_H
#define WAVCOMBINEDIALOG_H

#include <QDialog>
#include <kfr/all.hpp>

class QLabel;
class QProgressBar;
class QDialogButtonBox;

class WAVCombineDialog : public QDialog
{
    Q_OBJECT
public:
    WAVCombineDialog(QString rootDirName, bool recursive, const kfr::audio_format& targetFormat, QString saveFileName, QWidget* parent = nullptr);

private:
    QLabel* label;
    QProgressBar* progressBar;
    QDialogButtonBox* buttonBox;

    QString rootDirName;
    bool recursive;
    kfr::audio_format targetFormat;
    QString saveFileName;

private slots:
    void startWork();
    void preCheckDone();
    void readAndCombineWorkDone();
    void writeResultDone();

signals:
    void opened();
    // QDialog interface
public slots:
    void open() override;
    int exec() override;
};

#endif // WAVCOMBINEDIALOG_H

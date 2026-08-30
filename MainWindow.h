#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void start();
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void on_action_BackgroundColor_triggered();
    void on_action_Quit_triggered();
    void on_actionLog_to_Console_toggled(bool arg);
    void on_actionDelete_Logfile_at_start_toggled(bool arg);
    void on_actionLog_to_File_toggled(bool arg);

private:
    Ui::MainWindow *ui{nullptr};

    class Renderer* mVulkanWindow{nullptr};

    void quitApp();

    // Our own logger in the MainWindow
    class Logger& mLogger;

    //Logger class uses private ui pointer from this class
    //so needs to have access to this class
    friend class Logger;

};
#endif // MAINWINDOW_H

#include "MainWindow.h"
#include "Logger.h"
#include "ui_MainWindow.h"
#include "Renderer.h"
#include <QKeyEvent>
#include <QColorDialog>

MainWindow::MainWindow(QWidget *parent) :
        QMainWindow(parent),
        ui(new Ui::MainWindow),
        mLogger(Logger::getInstance())
{
    ui->setupUi(this);

    // Logger is the OutputLog at the bottom of the program
    // feed in MainWindow to the logger - has to be done, else the logger will crash the program
    mLogger.setMainWindow(this);

    //MainWindow size:
    resize(1300, 850);
    //Main app title
    setWindowTitle("Vulkan 26");

    // VulkanWindow is the part of the program that renders the Vulkan graphic
    mVulkanWindow = new Renderer();
    mVulkanWindow->setTitle("Renderer");    //Render window title - not shown
    mVulkanWindow->setMainWindow(this);     //Tells the Renderer of this MainWindow

    // Have to set the size of the Vulkan window here,
    // before initVulkan()
    // otherwise it can not set up the Vulkan swapchain correctly
    mVulkanWindow->setWidth(1000);
    mVulkanWindow->setHeight(800);
    mVulkanWindow->initVulkan();

    // Wrap VulkanWindow (QWindow) into a QWidget
    // This way the VulkanWindow can be integrated into the main window
    QWidget* vulkanWidget = QWidget::createWindowContainer(mVulkanWindow, this);
    // This will be the smallest you can scale the program window
    vulkanWidget->setMinimumSize(1000, 600);

    // Adding the Vulkan renderer to the main programs UI:
    // In the MainWindow.ui file we have a QVBoxLayout called VulkanLayout
    ui->VulkanLayout->addWidget(vulkanWidget);

    // You can also send messages to the statusbar at the very bottom:
    statusBar()->showMessage(" Everybody loves Vulkan! ");
}

MainWindow::~MainWindow()
{
    if(mVulkanWindow)
    {
        delete mVulkanWindow;
        mVulkanWindow = nullptr;
    }
    delete ui;
}

void MainWindow::start()
{
    LOGW("Start is called");
    // This starts the update and rendering in the VulkanWindow
    mVulkanWindow->requestUpdate();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    // Example: close window on ESC
    if (event->key() == Qt::Key_Escape) {
        quitApp();
    }
    // Example: SPACE key calling a function
    if (event->key() == Qt::Key_Space) {
        start();
    }
}

void MainWindow::on_action_BackgroundColor_triggered()
{
    // Here you should get the current color from the Renderer class
    QColor currentColor = QColor::fromRgbF(1.0, 0.0, 0.0); // Red as default for now

    //QColor has a strange format, so have to fix the string a bit for logging
    QString colorString = QString("currentColor: %1").arg(currentColor.name());
    LOGH(colorString.toStdString());

    // Open color dialog - with currentColor selected
    QColor color = QColorDialog::getColor(currentColor, this, "Choose Color");
    colorString = QString("new Color: %1").arg(color.name());
    LOGW(colorString.toStdString());

    //Now you can push this color to the Renderer, to use this as the VkClearValue clearColor!
}

void MainWindow::on_action_Quit_triggered()
{
    quitApp();
}

void MainWindow::quitApp()
{
    delete mVulkanWindow;
    mVulkanWindow = nullptr;
    close(); // calls Qt to close the app
}

void MainWindow::on_actionLog_to_Console_toggled(bool arg)
{
    mLogger.setPrintToConsole( arg );
}

void MainWindow::on_actionDelete_Logfile_at_start_toggled(bool arg)
{
    mLogger.setDeleteLogFileAtStart( arg );
}

void MainWindow::on_actionLog_to_File_toggled(bool arg)
{
    mLogger.setLogToFile( arg );
}


#include "Logger.h"
#include "MainWindow.h"
#include "ui_mainwindow.h"

#include <QPlainTextEdit>
#include <QFile>
#include <QDateTime>
#include <string>

//See Logger.h for documentation

Logger::Logger()
{ }

Logger& Logger::getInstance()
{
    static Logger mInstance;
    return mInstance;
}

void Logger::setMainWindow(MainWindow *mainWindowIn)
{
    mMainWindow = mainWindowIn;

    if(mDeleteLogFileAtStart)
    {
        if (mLogFile.exists())
        {
            if (!mLogFile.remove())
                mLogStream << "Logger: failed to delete log file " << mLogFile.fileName().toStdString() << std::endl;

			logText("Logger: deleted log file", LogType::HIGHLIGHT);
        }
	}

    if (mLogToFile)
    {
        const QString timeStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        // Append to a log file on disk
        if (mLogFile.open(QIODevice::Append | QIODevice::Text))
        {
            QTextStream out(&mLogFile);
            out << "\n--------------------------------------------------------------\n" << timeStamp << '\n';
            mLogFile.close();
        }
        else
            logText("Logger: failed to open log file ", LogType::ERR);
    }
    logText("Logger started", LogType::HIGHLIGHT);
}

void Logger::logText(std::string input, LogType colorIn)
{
    QColor textColor;

    //Setting the color of the text to be printed
    switch (colorIn)
    {
    case LogType::LOG :
        textColor = Qt::white;
        break;
    case LogType::HIGHLIGHT :
        textColor = Qt::darkGreen;
        break;
    case LogType::WARNING :
        textColor = Qt::yellow;
        break;
    case LogType::ERR :
        textColor = Qt::red;
        break;
    default:
        textColor = Qt::darkGray;
    }

    // Doing the requirements for printing the text to the Output widget
    // The MainWindow.ui has a QPlainTextEdit widget at the bottom
    // The name of this is OutputLog
    QTextCharFormat textformat;
    textformat = mMainWindow->ui->OutputLog->currentCharFormat();
    textformat.setForeground(QBrush((textColor)));
    mMainWindow->ui->OutputLog->setCurrentCharFormat(textformat);
    mMainWindow->ui->OutputLog->appendPlainText(input.c_str());

    // Append to a log file on disk (one new line per input)
    if (mLogToFile)
    {
        if (mLogFile.open(QIODevice::Append | QIODevice::Text))
        {
            QTextStream out(&mLogFile);
            out << QString::fromStdString(input) << '\n';
            mLogFile.close();
        }
        else
            logText("Logger: failed to open log file ", LogType::ERR);

    }

    if(mPrintToConsole)
        qDebug() << input;

    mLogStream.clear();
}

void Logger::printf(const char *format_str, ...)
{
    va_list args;
    va_start(args, format_str);

    // First, calculate the required buffer size
    int size = vsnprintf(nullptr, 0, format_str, args);
    va_end(args);

    // Allocate buffer and format the string
    va_start(args, format_str);
    std::vector<char> buffer(size + 1);
    vsnprintf(buffer.data(), size + 1, format_str, args);
    va_end(args);
    logText(buffer.data());
}

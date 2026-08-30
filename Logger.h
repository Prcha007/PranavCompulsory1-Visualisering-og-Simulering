#ifndef LOGGER_H
#define LOGGER_H

#include <cstdarg>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <QFile>

/* This thing is just a utility class to get messages into a program widget
   instead of having to cout or qDebug() to a console.

   It does not have anything to do with the Vulkan code

   It is used by getting an instance of the class and calling the LOG macro function
*/

// Custom manipulator to mark line completion
struct LineEnd {
    LineEnd() = default;
};

#define LOGGER(msg, type) mLogger.logText(std::string(msg), Logger::LogType::type)
#define LOG(msg) mLogger.logText(std::string(msg), Logger::LogType::NONE)
#define LOGH(msg) mLogger.logText(std::string(msg), Logger::LogType::HIGHLIGHT)
#define LOGW(msg) mLogger.logText(std::string(msg), Logger::LogType::WARNING)
#define LOGE(msg) mLogger.logText(std::string(msg), Logger::LogType::ERR)
#define LOGP(msg, ...) mLogger.printf(msg, __VA_ARGS__)


/// Basic logging functionality to the widget in the MainWindow.
/// This class is a singleton, so you get it by calling getInstance();
/// see https://gameprogrammingpatterns.com/singleton.html
class Logger
{
public:
    // Colors for the Logger class
    enum class LogType
    {
        LOG = 0,
        HIGHLIGHT,
        WARNING,
        ERR, //using ERROR gives a name clash with Windows defines
        NONE
    };

    // Getting the instance of this class. This class is a singleton.
    static Logger& getInstance();

    // Setting the pointer to MainWindow, so the logger knows where to print
    void setMainWindow(class MainWindow *mainWindowIn);

    // Printing the given string to the log window. Optional logtype (sets color) as second parameter
    void logText(std::string input, LogType colorIn = LogType::NONE);

    void printf(const char* format_str, ...);

    // << operator overloading to add flexibility to log different datatypes
    // Specific versions for common types
    Logger& operator<<(const std::string& value)
    {
        mLogStream << value;
        // logText(mLogStream.str());
        return *this;
    }

    Logger& operator<<(int value)
    {
        mLogStream << value << " ";
        // logText(mLogStream.str());
        return *this;
    }

    Logger& operator<<(double value)
    {
        mLogStream << value << " ";
        // logText(mLogStream.str());
        return *this;
    }

    Logger& operator<<(float value)
    {
        mLogStream << value << " ";
        // logText(mLogStream.str());
        return *this;
    }

    // Template for everything else
    template<typename T>
    Logger& operator<<(const T& value)
    {
        mLogStream << value;
        // logText(mLogStream.str());
        return *this;
    }

    Logger& operator<<(const LineEnd&) {
        mLogStream << std::endl;  // Add newline
        logText(mLogStream.str());
        return *this;
    }

    void setPrintToConsole(bool argIn) {mPrintToConsole = argIn; }
    void setLogToFile(bool argIn) { mLogToFile = argIn; }
    void setDeleteLogFileAtStart(bool argIn){ mDeleteLogFileAtStart = argIn; }

private:
    // Protecting the constructors since this class is a singleton
    Logger();
    void operator=(Logger&){};  //Assignment operator
    Logger(const Logger&){};    //Copy constructor

    // Needs a pointer to the MainWindow, since the widget is held by MainWindows UI
    class MainWindow* mMainWindow{nullptr};

    // a variable that can receive other values than string, using overloaded operator<<
    std::ostringstream mLogStream;

    bool mPrintToConsole{ true };
	bool mLogToFile{ true };
    bool mDeleteLogFileAtStart{ true };

    // Single QFile instance used across the class
    QFile mLogFile{ QStringLiteral("../../Application.log") };
};

#endif // LOGGER_H

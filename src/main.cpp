#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include "websdkconnector.h"

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_LOGGING_RULES")) {
        qputenv("QT_LOGGING_RULES", "*.debug=false");
    }

    QCoreApplication app(argc, argv);
    app.setApplicationName("ravkav-nfc-proxy");
    app.setApplicationVersion(APP_VERSION);

    if (argc < 2) {
        qCritical() << "Error: Application needs the 'ravkav:' scheme URL string parameter to trigger context loops."; //
        return -1;
    }

    QString incomingUri = QString::fromLocal8Bit(argv[1]);
    if (!incomingUri.startsWith(QStringLiteral("ravkav:"))) { //
        qCritical() << "Error: Invalid link context scheme payload match."; //
        return -2;
    }

    // Extract transaction endpoint string segment following the 'ravkav:' declaration header
    QString proxyTargetUrl = incomingUri.mid(7); 

    WebSdkConnector connector;
    connector.connectToSdk(proxyTargetUrl); //

    return app.exec();
}

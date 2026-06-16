#ifndef RAVKAVLOADMANAGER_H
#define RAVKAVLOADMANAGER_H

#define APP_VERSION "0.1.4"

#include <QObject>
#include <QWebSocket>
#include <QNearFieldTarget>
#include <QJsonObject>
#include <QMap>

class RavKavLoadManager : public QObject
{
    Q_OBJECT
public:
    explicit RavKavLoadManager(QObject *parent = nullptr);
    void load(QNearFieldTarget *target, const QString &transactionKey);
    void cancel();
    bool isActive() const;

signals:
    void preLoad();
    void postLoad(const QJsonObject &result);

private slots:
    void onConnected();
    void onTextMessageReceived(const QString &message);
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    
    // Qt Asynchronous Request Handler
    void handleRequestCompleted(const QNearFieldTarget::RequestId &id);

private:
    void handleAction1(const QJsonObject &messageJson);
    void sendAction2Response();
    void setStatusAndRunPostLoad(const QString &finishStatus, bool retryEnabled, int cause, const QJsonObject &result = QJsonObject());
    void closeAndClean();
    void sendNextCommand();

    QWebSocket *m_webSocket;
    QNearFieldTarget *m_cardTarget;
    bool m_isWriting;
    QString m_finishStatus;
    
    QList<QByteArray> m_pendingCommands;
    QList<QByteArray> m_collectedResponses;
    int m_currentCommandIndex;

    QNearFieldTarget::RequestId m_currentRequestId;

    // State Trackers
    bool m_isFetchingResponse = false;
    QByteArray m_originalStatusWord;
    QByteArray m_accumulatedGetData;
    QByteArray m_currentResponseAccumulator;
};

#endif // RAVKAVLOADMANAGER_H

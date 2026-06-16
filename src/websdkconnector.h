#ifndef WEBSDKCONNECTOR_H
#define WEBSDKCONNECTOR_H

#include <QObject>
#include <QWebSocket>
#include <QNearFieldManager>
#include <QNearFieldTarget>
#include "ravkavloadmanager.h"

class WebSdkConnector : public QObject
{
    Q_OBJECT
public:
    explicit WebSdkConnector(QObject *parent = nullptr);
    void connectToSdk(const QString &proxyUrl);

private slots:
    void onConnected();
    void onTextMessageReceived(const QString &message);
    
    // Qt Standard Target Lifecycle
    void onTargetDetected(QNearFieldTarget *target);
    void onTargetLost(QNearFieldTarget *target);
    void onTargetError(QNearFieldTarget::Error error, const QNearFieldTarget::RequestId &id);
    
    void onPreLoadTriggered();
    void onPostLoadTriggered(const QJsonObject &result);

private:
    void sendStateMessage(const QString &state);
    void sendJsonRpcResponse(const QJsonValue &id, const QJsonObject &result);

    QWebSocket *m_webSocket;
    QNearFieldManager *m_nfcManager;
    QNearFieldTarget *m_activeTarget;
    RavKavLoadManager *m_loadManager;
    
    QJsonValue m_lastMethodId;
    bool m_isBusy;
    QString m_currentState;
};

#endif // WEBSDKCONNECTOR_H

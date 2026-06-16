#include "websdkconnector.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

WebSdkConnector::WebSdkConnector(QObject *parent)
    : QObject(parent), m_webSocket(new QWebSocket()), m_nfcManager(new QNearFieldManager(this)),
      m_activeTarget(nullptr), m_loadManager(new RavKavLoadManager(this)), m_isBusy(false), m_currentState(QStringLiteral("NO_CARD"))
{
    connect(m_webSocket, &QWebSocket::connected, this, &WebSdkConnector::onConnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &WebSdkConnector::onTextMessageReceived);

    connect(m_nfcManager, &QNearFieldManager::targetDetected, this, &WebSdkConnector::onTargetDetected);
    connect(m_nfcManager, &QNearFieldManager::targetLost, this, &WebSdkConnector::onTargetLost);

    connect(m_loadManager, &RavKavLoadManager::preLoad, this, &WebSdkConnector::onPreLoadTriggered);
    connect(m_loadManager, &RavKavLoadManager::postLoad, this, &WebSdkConnector::onPostLoadTriggered);
    
    qInfo() << "[NFC_INIT] Launching Native Qt 6 PC/SC Polling Loop Engine...";
    
    // Pass TagCommandAccess to allow raw APDU exchanges on Smart Cards
    m_nfcManager->startTargetDetection(QNearFieldTarget::TagTypeSpecificAccess);
}

void WebSdkConnector::connectToSdk(const QString &proxyUrl)
{
    qInfo() << "[WS_LINK] Opening channel to backend server:" << proxyUrl;
    m_webSocket->open(QUrl(proxyUrl));
}

void WebSdkConnector::onConnected() {
    qInfo() << "[WS_LINK] Connection handshake complete.";

    // Send Version frame
    QJsonObject vParams;
    vParams.insert("version", QStringLiteral("0.1.4")); // other versions are a lot harder to implement and this allows us to use legacy API (SAM ENDPOINT)
    QJsonObject vMsg;
    vMsg.insert("id", QJsonValue::Null);
    vMsg.insert("method", QStringLiteral("VERSION"));
    vMsg.insert("params", vParams);
    m_webSocket->sendTextMessage(QJsonDocument(vMsg).toJson(QJsonDocument::Compact));

    // Send Initial State frame
    sendStateMessage(m_currentState); // "NO_CARD"
}

void WebSdkConnector::onTextMessageReceived(const QString &message)
{
    qDebug() << "[DEBUG WebSdkConnector INCOMING]:" << message;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject json = doc.object();
    QString method = json.value("method").toString();
    m_lastMethodId = json.value(QStringLiteral("id"));

    if (method == QStringLiteral("VERSION")) { 
        QJsonObject versionResult;
        versionResult.insert(QStringLiteral("version"), QStringLiteral(APP_VERSION));
        sendJsonRpcResponse(m_lastMethodId, versionResult);
    }
    else if (method == QStringLiteral("SAMSERVER_ENDPOINT")) { // this is what we talk to for raw APDU commands/data
        QString targetEndpoint = json.value("params").toObject().value("endpoint").toString();
        qInfo() << "[SAM_ROUTE] Tunneling APDU targets to endpoint:" << targetEndpoint;
        
        m_isBusy = true;
        sendStateMessage(QStringLiteral("BUSY"));
        m_loadManager->load(m_activeTarget, targetEndpoint);
    }
}

void WebSdkConnector::onTargetDetected(QNearFieldTarget *target)
{
    if (!target) return;

    // Check if we are interacting with a duplicate event or currently busy handling a load tunnel operation
    if (m_isBusy || m_activeTarget == target) {
        qDebug() << "[HARDWARE_NFC] Target interaction skipped - Loop Busy or duplicate event.";
        return;
    }

    m_activeTarget = target;
    // Set parent ownership to ensure Qt's event tree keeps the object instance allocated
    m_activeTarget->setParent(this); 

    m_currentState = QStringLiteral("READY");
    sendStateMessage(m_currentState);

    // Capture standard error callbacks from the raw physical target pipeline
    connect(m_activeTarget, &QNearFieldTarget::error, this, &WebSdkConnector::onTargetError);

    qInfo() << "[HARDWARE_NFC] SMARTCARD DETECTED";

    // Trigger processing route passing the parsed active session URL arguments or stored method context id
    m_isBusy = true;

    if (m_lastMethodId.toString().isEmpty()) {
        qDebug() << "could not parse URL!";
        return;
    } 

    m_loadManager->load(m_activeTarget, m_lastMethodId.toString());
}

void WebSdkConnector::onTargetLost(QNearFieldTarget *target)
{
    qInfo() << "[HARDWARE_NFC] Smartcard removed from polling field boundary.";
    if (m_activeTarget == target) {
        m_loadManager->cancel();
        m_activeTarget = nullptr;
        m_currentState = QStringLiteral("NO_CARD");
        sendStateMessage(m_currentState);
        
        // Clean up the object here, since WebSdkConnector owns the lifecycle
        target->deleteLater();
    }
}

void WebSdkConnector::onTargetError(QNearFieldTarget::Error error, const QNearFieldTarget::RequestId &id)
{
    qCritical() << "[NFC_ERROR] Error Event Flagged by Backend! Code:" << error << "| Active Request ID Valid:" << id.isValid();
}

void WebSdkConnector::onPreLoadTriggered() {}

void WebSdkConnector::onPostLoadTriggered(const QJsonObject &result)
{
    m_isBusy = false;
    sendJsonRpcResponse(m_lastMethodId, result);
    sendStateMessage(m_currentState);
}

void WebSdkConnector::sendStateMessage(const QString &state)
{
    QJsonObject prm;
    prm.insert("state", state);
    
    QJsonObject msg;
    msg.insert("id", QJsonValue::Null);
    msg.insert("method", QStringLiteral("STATE"));
    msg.insert("params", prm);

    if (m_webSocket->isValid()) {
        qDebug() << "[DEBUG WebSdkConnector OUTGOING State]:" << QJsonDocument(msg).toJson(QJsonDocument::Compact);
        m_webSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }
}

void WebSdkConnector::sendJsonRpcResponse(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject msg;
    
    // Only insert the ID field if it is a valid, specified value
    if (!id.isUndefined()) {
        msg.insert(QStringLiteral("id"), id);
    }
    msg.insert(QStringLiteral("result"), result);

    if (m_webSocket->isValid()) {
        qDebug() << "[DEBUG WebSdkConnector OUTGOING RPC Resp]:" << QJsonDocument(msg).toJson(QJsonDocument::Compact);
        m_webSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }
}
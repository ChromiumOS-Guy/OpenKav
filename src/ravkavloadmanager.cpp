#include "ravkavloadmanager.h"
#include "iso7816apdu.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QUrl>

RavKavLoadManager::RavKavLoadManager(QObject *parent)
    : QObject(parent), m_webSocket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this)),
      m_cardTarget(nullptr), m_isWriting(false), m_currentCommandIndex(0)
{
    connect(m_webSocket, &QWebSocket::connected, this, &RavKavLoadManager::onConnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &RavKavLoadManager::onTextMessageReceived);
    connect(m_webSocket, &QWebSocket::disconnected, this, &RavKavLoadManager::onDisconnected);
    connect(m_webSocket, &QWebSocket::errorOccurred, this, &RavKavLoadManager::onError);
}

void RavKavLoadManager::load(QNearFieldTarget *target, const QString &transactionKey)
{
    if (m_isWriting) {
        if (m_cardTarget) {
            disconnect(m_cardTarget, &QNearFieldTarget::requestCompleted, this, &RavKavLoadManager::handleRequestCompleted);
        }
        if (m_webSocket && m_webSocket->isValid()) {
            m_webSocket->close(QWebSocketProtocol::CloseCodeNormal, QStringLiteral("Channel swap handshake reset"));
        }
    }

    m_cardTarget = target;
    m_isWriting = true;
    m_finishStatus = QString();
    m_currentCommandIndex = 0;
    m_currentRequestId = QNearFieldTarget::RequestId();
    m_pendingCommands.clear();
    m_collectedResponses.clear();
    
    // Reset states
    m_isFetchingResponse = false;
    m_originalStatusWord.clear();
    m_accumulatedGetData.clear();

    if (!m_cardTarget) {
        qCritical() << "[TUNNEL_6] Aborting transmission loop: Target structure uninitialized.";
        setStatusAndRunPostLoad(QStringLiteral("IO_ERROR"), true, 1004);
        return;
    }

    qInfo() << "[TUNNEL_6] Handshaking secure channel to upstream endpoint key:" << transactionKey;

    QUrl connectionUrl(transactionKey);
    if (!connectionUrl.isValid() || connectionUrl.scheme().isEmpty()) {
        qCritical() << "[TUNNEL_6] Invalid or empty target SAM routing path URL structural footprint.";
        setStatusAndRunPostLoad(QStringLiteral("IO_ERROR"), true, 1009);
        return;
    }

    m_webSocket->open(connectionUrl);
}

void RavKavLoadManager::cancel()
{
    setStatusAndRunPostLoad(QStringLiteral("CANCELLED"), false, 1000);
}

bool RavKavLoadManager::isActive() const
{
    return m_isWriting;
}

void RavKavLoadManager::onConnected()
{
    qInfo() << "[TUNNEL_6] WebSocket operational. Beginning transmission arrays.";
}

void RavKavLoadManager::onTextMessageReceived(const QString &message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject messageJson = doc.object();
    int action = messageJson.value("action").toInt();

    if (action == 1) { 
        handleAction1(messageJson);
    } else if (action == 3 || action == 4) { 
        QString status = (action == 3) ? QStringLiteral("SUCCESS") : QStringLiteral("LOAD_ERROR");
        qInfo() << "[TUNNEL_6] Finalized transaction loop code result state:" << status;
        setStatusAndRunPostLoad(status, messageJson.value("retry_enabled").toBool(), messageJson.value("cause").toInt(), messageJson.value("result").toObject());
    }
}

void RavKavLoadManager::handleAction1(const QJsonObject &messageJson)
{
    QJsonArray arguments = messageJson.value("arguments").toArray();
    m_pendingCommands.clear();
    m_collectedResponses.clear();
    m_currentCommandIndex = 0;

    qInfo() << "[APDU_STREAM] Incoming transmission row array payload length:" << arguments.size();

    for (int i = 0; i < arguments.size(); ++i) {
        QByteArray cmd = QByteArray::fromBase64(arguments.at(i).toString().toUtf8(), QByteArray::Base64UrlEncoding);
        m_pendingCommands.append(cmd);
    }

    if (!m_pendingCommands.isEmpty() && m_cardTarget) {
        disconnect(m_cardTarget, &QNearFieldTarget::requestCompleted, this, &RavKavLoadManager::handleRequestCompleted);
        connect(m_cardTarget, &QNearFieldTarget::requestCompleted, this, &RavKavLoadManager::handleRequestCompleted);
        
        qInfo() << "[APDU_STREAM] Transmitting block line 0 to peripheral smart card hardware...";
        sendNextCommand();
    }
}

void RavKavLoadManager::sendNextCommand()
{
    if (m_currentCommandIndex < m_pendingCommands.size()) {
        m_currentRequestId = m_cardTarget->sendCommand(m_pendingCommands.at(m_currentCommandIndex));
    } else {
        qInfo() << "[APDU_STREAM] Completed command row sequences. Streaming responses downstream.";
        sendAction2Response();
    }
}

void RavKavLoadManager::handleRequestCompleted(const QNearFieldTarget::RequestId &id)
{
    if (!m_cardTarget || !m_isWriting || id != m_currentRequestId) return;

    QByteArray rawResponse = m_cardTarget->requestResponse(id).toByteArray();
    qDebug() << "[APDU_HW_REPLY] Cmd Index" << m_currentCommandIndex << "Response:" << rawResponse.toHex().toUpper();

    if (rawResponse.size() < 2) {
        qCritical() << "[APDU] Malformed or truncated hardware frame received.";
        setStatusAndRunPostLoad(QStringLiteral("IO_ERROR"), true, 1005);
        return;
    }

    Iso7816Apdu::Response packet = Iso7816Apdu::parseResponse(rawResponse);
    // a bunch of error handling because server is lazy or gives us bad data:

    // handle 6Cxx (WRONG LENGTH):
    // 6C is wrong length, xx is hex for reissue length
    if (packet.hasWrongLength()) {
        qInfo() << "[APDU Emulation] 0x6Cxx detected. Re-issuing command with correct Le =" << packet.bytesAvailableOrExpected();
        QByteArray originalCmd = m_pendingCommands.at(m_currentCommandIndex);
        QByteArray mutatedCmd = Iso7816Apdu::mutateLe(originalCmd, packet.bytesAvailableOrExpected());
        m_currentRequestId = m_cardTarget->sendCommand(mutatedCmd);
        return;
    }

    // handle 61xx (MORE DATA WAITING):
    // If the card executed the command but has data sitting in the buffer
    if (packet.hasMoreData()) {
        qInfo() << "[APDU Emulation] 0x61xx detected. Fetching" << packet.bytesAvailableOrExpected() << "bytes...";
        m_currentResponseAccumulator.append(packet.data);
        QByteArray getResponseCmd = Iso7816Apdu::createGetResponse(packet.bytesAvailableOrExpected());
        m_currentRequestId = m_cardTarget->sendCommand(getResponseCmd);
        return;
    }

    // empty success (9000 but 0 bytes):
    // If it succeeded but returned nothing, force a pull because the server is lazy
    if (!m_isFetchingResponse && packet.isSuccess() && packet.data.isEmpty()) {
        qInfo() << "[APDU_HACK] Empty 9000 response. Emulating Java GET_RESPONSE loop...";
        m_isFetchingResponse = true;
        
        m_originalStatusWord.clear();
        m_originalStatusWord.append(static_cast<char>(packet.sw1));
        m_originalStatusWord.append(static_cast<char>(packet.sw2));
        
        QByteArray getResponseCmd = Iso7816Apdu::createGetResponse(0x00);
        m_currentRequestId = m_cardTarget->sendCommand(getResponseCmd);
        return;
    }

    //  empty success, logic loop:
    if (m_isFetchingResponse) {
        // If it fails (like getting a 69 85), ignore it and use the saved 90 00
        if (!packet.isSuccess()) {
            qInfo() << "[APDU_HACK] Card rejected forced fetch. Reverting to original status.";
            m_collectedResponses.append(m_originalStatusWord);
        } else {
            m_currentResponseAccumulator.append(packet.data);
            m_currentResponseAccumulator.append(m_originalStatusWord); // Mask it with the original 9000
            m_collectedResponses.append(m_currentResponseAccumulator);
        }

        m_currentResponseAccumulator.clear();
        m_isFetchingResponse = false;
        m_originalStatusWord.clear();
        m_currentCommandIndex++;
        sendNextCommand();
        return;
    }

    // command completion after error handling:
    m_currentResponseAccumulator.append(packet.data);
    m_currentResponseAccumulator.append(static_cast<char>(packet.sw1));
    m_currentResponseAccumulator.append(static_cast<char>(packet.sw2));

    m_collectedResponses.append(m_currentResponseAccumulator);
    m_currentResponseAccumulator.clear();

    m_currentCommandIndex++;
    sendNextCommand();
}

void RavKavLoadManager::sendAction2Response()
{
    QJsonObject responseJson;
    responseJson.insert("action", 2); 
    QJsonArray responseArguments;

    for (const QByteArray &resp : m_collectedResponses) {
        // Standard URL Safe Base64 mapping
        QString base64 = QString::fromUtf8(resp.toBase64(QByteArray::Base64UrlEncoding));
        responseArguments.append(base64);
    }

    responseJson.insert("arguments", responseArguments);
    
    QJsonDocument doc(responseJson);
    qDebug() << "[DEBUG ACTION 2 OUTGOING]:" << doc.toJson(QJsonDocument::Compact);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
}

void RavKavLoadManager::setStatusAndRunPostLoad(const QString &finishStatus, bool retryEnabled, int cause, const QJsonObject &result)
{
    m_finishStatus = finishStatus;
    QJsonObject resultJson;
    resultJson.insert("finish_status", finishStatus);
    resultJson.insert("cause", cause);
    resultJson.insert("retry_enabled", retryEnabled);
    resultJson.insert("result", result);

    emit postLoad(resultJson);
    closeAndClean();
}

void RavKavLoadManager::closeAndClean()
{
    if (m_cardTarget) {
        disconnect(m_cardTarget, &QNearFieldTarget::requestCompleted, this, &RavKavLoadManager::handleRequestCompleted);
    }
    if (m_webSocket && m_webSocket->isValid()) {
        m_webSocket->close(QWebSocketProtocol::CloseCodeNormal, QStringLiteral("RavKavLoadManager complete state cleanup"));
    }
    
    m_cardTarget = nullptr;
    m_isWriting = false;
    m_currentCommandIndex = 0;
    m_currentRequestId = QNearFieldTarget::RequestId();
}

void RavKavLoadManager::onDisconnected() 
{ 
    closeAndClean(); 
}

void RavKavLoadManager::onError(QAbstractSocket::SocketError error) 
{ 
    qCritical() << "[SOCKET_CRITICAL] Pipe connection error flagged down. Variant error enum:" << error;
    setStatusAndRunPostLoad(QStringLiteral("IO_ERROR"), true, 1009); 
}

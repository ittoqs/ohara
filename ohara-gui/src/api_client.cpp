#include "api_client.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHttpPart>

ApiClient::ApiClient(QObject *parent) : QObject(parent), m_networkManager(new QNetworkAccessManager(this)) {
}

void ApiClient::sendChatMessage(const QString &message, const QString &modelName, const QString &filename, const QString &systemPrompt, int sessionId, const QString &imageBase64) {
    QUrl url("http://127.0.0.1:8000/chat");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject json;
    json["message"] = message;
    json["model_name"] = modelName;
    json["filename"] = filename;
    json["system_prompt"] = systemPrompt;
    json["session_id"] = sessionId;
    json["image_base64"] = imageBase64;

    QJsonDocument doc(json);
    QNetworkReply *reply = m_networkManager->post(request, doc.toJson());
    
    // For streaming
    connect(reply, &QNetworkReply::readyRead, this, &ApiClient::onChatReadyRead);
    connect(reply, &QNetworkReply::finished, this, &ApiClient::onChatReplyFinished);
    connect(reply, &QNetworkReply::errorOccurred, this, &ApiClient::onNetworkError);
}

void ApiClient::onChatReadyRead() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    while (reply->canReadLine()) {
        QByteArray line = reply->readLine().trimmed();
        if (line.startsWith("data: ")) {
            QByteArray dataStr = line.mid(6);
            if (dataStr == "[DONE]") {
                emit chatResponseFinished();
                continue;
            }
            QJsonDocument doc = QJsonDocument::fromJson(dataStr);
            QJsonObject obj = doc.object();
            if (obj.contains("text")) {
                emit chatChunkReceived(obj["text"].toString());
            } else if (obj.contains("error")) {
                emit errorOccurred(obj["error"].toString());
            }
        }
    }
}

void ApiClient::onChatReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (reply) reply->deleteLater();
}

void ApiClient::downloadModel(const QString &repoId, const QString &filename, const QString &mmprojFilename) {
    QUrl url("http://127.0.0.1:8000/download_model");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject json;
    json["repo_id"] = repoId;
    json["filename"] = filename;
    json["mmproj_filename"] = mmprojFilename;
    QJsonDocument doc(json);
    QNetworkReply *reply = m_networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, &ApiClient::onDownloadReplyFinished);
    connect(reply, &QNetworkReply::errorOccurred, this, &ApiClient::onNetworkError);
}

void ApiClient::onDownloadReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        QJsonObject jsonObj = jsonDoc.object();
        emit modelDownloadFinished(jsonObj["status"].toString(), jsonObj["path"].toString());
    }
    reply->deleteLater();
}

QString ApiClient::imageToBase64(const QString &filePath) {
    QString path = filePath;
    if (path.startsWith("file://")) {
        path = path.mid(7);
    }
    
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open image file:" << path;
        return "";
    }
    
    QByteArray imageData = file.readAll();
    return QString(imageData.toBase64());
}

void ApiClient::uploadDocument(const QString &filePath) {
    // Remove "file://" prefix if present
    QString path = filePath;
    if (path.startsWith("file://")) {
        path = path.mid(7);
    }

    QFile *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        emit errorOccurred("Cannot open file: " + path);
        delete file;
        return;
    }

    QUrl url("http://127.0.0.1:8000/upload_doc");
    QNetworkRequest request(url);

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"file\"; filename=\"" + QFileInfo(path).fileName() + "\""));
    filePart.setBodyDevice(file);
    file->setParent(multiPart); // File deleted with multiPart
    multiPart->append(filePart);

    QNetworkReply *reply = m_networkManager->post(request, multiPart);
    multiPart->setParent(reply);
    
    connect(reply, &QNetworkReply::finished, this, &ApiClient::onUploadReplyFinished);
    connect(reply, &QNetworkReply::errorOccurred, this, &ApiClient::onNetworkError);
}

void ApiClient::onUploadReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        QJsonObject jsonObj = jsonDoc.object();
        emit documentUploaded(jsonObj["status"].toString(), jsonObj["message"].toString());
    }
    reply->deleteLater();
}

void ApiClient::fetchSessions() {
    QUrl url("http://127.0.0.1:8000/sessions");
    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &ApiClient::onFetchSessionsFinished);
    connect(reply, &QNetworkReply::errorOccurred, this, &ApiClient::onNetworkError);
}

void ApiClient::onFetchSessionsFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray arr = jsonDoc.object()["sessions"].toArray();
        QVariantList list;
        for (const QJsonValue &val : arr) {
            list.append(val.toObject().toVariantMap());
        }
        emit sessionsFetched(list);
    }
    reply->deleteLater();
}

void ApiClient::createNewSession(const QString &title) {
    QUrl url("http://127.0.0.1:8000/session");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject json;
    json["title"] = title;
    QJsonDocument doc(json);
    QNetworkReply *reply = m_networkManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, &ApiClient::onCreateSessionFinished);
    connect(reply, &QNetworkReply::errorOccurred, this, &ApiClient::onNetworkError);
}

void ApiClient::onCreateSessionFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = jsonDoc.object();
        emit sessionCreated(obj["id"].toInt(), obj["title"].toString());
    }
    reply->deleteLater();
}

void ApiClient::fetchSessionHistory(int sessionId) {
    QUrl url(QString("http://127.0.0.1:8000/session/%1").arg(sessionId));
    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &ApiClient::onFetchHistoryFinished);
    connect(reply, &QNetworkReply::errorOccurred, this, &ApiClient::onNetworkError);
}

void ApiClient::onFetchHistoryFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray arr = jsonDoc.object()["messages"].toArray();
        QVariantList list;
        for (const QJsonValue &val : arr) {
            list.append(val.toObject().toVariantMap());
        }
        emit sessionHistoryFetched(list);
    }
    reply->deleteLater();
}

void ApiClient::onNetworkError(QNetworkReply::NetworkError code) {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (reply) {
        emit errorOccurred(reply->errorString());
    }
}

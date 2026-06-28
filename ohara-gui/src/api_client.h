#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHttpMultiPart>

class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);

    Q_INVOKABLE void sendChatMessage(const QString &message, const QString &modelName, const QString &filename, const QString &systemPrompt, int sessionId, const QString &imageBase64 = "");
    Q_INVOKABLE void downloadModel(const QString &repoId, const QString &filename, const QString &mmprojFilename = "");
    Q_INVOKABLE void uploadDocument(const QString &filePath);
    
    Q_INVOKABLE QString imageToBase64(const QString &filePath);
    
    Q_INVOKABLE void fetchSessions();
    Q_INVOKABLE void createNewSession(const QString &title);
    Q_INVOKABLE void fetchSessionHistory(int sessionId);

signals:
    // Chat stream signals
    void chatChunkReceived(const QString &chunk);
    void chatResponseFinished();
    
    void modelDownloadFinished(const QString &status, const QString &path);
    void documentUploaded(const QString &status, const QString &message);
    
    // History signals
    void sessionsFetched(const QVariantList &sessions);
    void sessionCreated(int id, const QString &title);
    void sessionHistoryFetched(const QVariantList &messages);
    
    void errorOccurred(const QString &error);

private slots:
    void onChatReadyRead();
    void onChatReplyFinished();
    void onDownloadReplyFinished();
    void onUploadReplyFinished();
    void onFetchSessionsFinished();
    void onCreateSessionFinished();
    void onFetchHistoryFinished();
    void onNetworkError(QNetworkReply::NetworkError code);

private:
    QNetworkAccessManager *m_networkManager;
};

#endif // API_CLIENT_H

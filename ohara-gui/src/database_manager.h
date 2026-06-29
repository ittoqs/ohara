#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QObject>
#include <QThread>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QSqlDatabase>
#include <QMutex>

class DatabaseManager : public QObject {
    Q_OBJECT

public:
    explicit DatabaseManager(const QString &dbPath, QObject *parent = nullptr);
    ~DatabaseManager();

    // Sessions
    Q_INVOKABLE int createSession(const QString &title, const QString &modelName = "",
                                   const QString &systemPrompt = "");
    Q_INVOKABLE QVariantList getSessions();
    Q_INVOKABLE QVariantMap getSession(int sessionId);
    Q_INVOKABLE void deleteSession(int sessionId);
    Q_INVOKABLE void renameSession(int sessionId, const QString &newTitle);
    Q_INVOKABLE void updateSessionModel(int sessionId, const QString &modelName);
    Q_INVOKABLE void updateSessionPrompt(int sessionId, const QString &systemPrompt);
    Q_INVOKABLE void updateSessionPersonality(int sessionId, const QString &personality);

    // Messages
    Q_INVOKABLE void addMessage(int sessionId, const QString &sender, const QString &text,
                                 const QString &imageBase64 = "");
    Q_INVOKABLE QVariantList getMessages(int sessionId, int limit = 50);
    Q_INVOKABLE QVariantList getRecentMessages(int sessionId, int limit = 20);
    Q_INVOKABLE void deleteMessage(int messageId);
    Q_INVOKABLE int getMessageCount(int sessionId);
    Q_INVOKABLE QVariantList searchMessages(const QString &query, int limit = 20);

    // Documents (FTS5 RAG)
    Q_INVOKABLE void indexDocument(const QString &filename, const QString &content);
    Q_INVOKABLE QVariantList searchDocuments(const QString &query, int limit = 3);
    Q_INVOKABLE void deleteDocument(const QString &filename);
    Q_INVOKABLE QVariantList getIndexedDocuments();

    // Settings key-value store
    Q_INVOKABLE QVariant getSetting(const QString &key, const QVariant &defaultValue = QVariant());
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);

    // Data Management
    Q_INVOKABLE void clearChatHistory();
    Q_INVOKABLE void clearAllData();

signals:
    void sessionCreated(int id, const QString &title);
    void sessionDeleted(int id);
    void sessionRenamed(int id, const QString &newTitle);
    void messageAdded(int sessionId, int messageId);
    void documentIndexed(const QString &filename, int chunkCount);
    void errorOccurred(const QString &error);

private:
    void initDatabase();
    void createTables();
    void enableWAL();
    QSqlDatabase getConnection();

    QString m_dbPath;
    QMutex m_mutex;
    int m_connectionCounter = 0;
};

#endif // DATABASE_MANAGER_H

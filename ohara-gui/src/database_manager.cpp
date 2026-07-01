#include "database_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>

DatabaseManager::DatabaseManager(const QString &dbPath, QObject *parent)
    : QObject(parent), m_dbPath(dbPath)
{
    initDatabase();
}

DatabaseManager::~DatabaseManager()
{
    // Close all connections
    QStringList connections = QSqlDatabase::connectionNames();
    for (const QString &name : connections) {
        if (name.startsWith("ohara_")) {
            QSqlDatabase::removeDatabase(name);
        }
    }
}

QSqlDatabase DatabaseManager::getConnection()
{
    QMutexLocker locker(&m_mutex);
    QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString connName = "ohara_" + threadId;

    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase db = QSqlDatabase::database(connName);
        if (db.isOpen()) return db;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(m_dbPath);

    if (!db.open()) {
        qWarning() << "Failed to open database:" << db.lastError().text();
        emit errorOccurred("Database error: " + db.lastError().text());
    }

    return db;
}

void DatabaseManager::enableWAL()
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA synchronous=NORMAL");
    q.exec("PRAGMA cache_size=-8000"); // 8MB cache
    q.exec("PRAGMA temp_store=MEMORY");
    q.exec("PRAGMA mmap_size=268435456"); // 256MB mmap
}

void DatabaseManager::initDatabase()
{
    enableWAL();
    createTables();
}

void DatabaseManager::createTables()
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);

    // Sessions table with metadata
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            model_name TEXT DEFAULT '',
            system_prompt TEXT DEFAULT '',
            personality TEXT DEFAULT 'general',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )");

    // Messages table
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id INTEGER NOT NULL,
            sender TEXT NOT NULL,
            text TEXT NOT NULL,
            image_base64 TEXT DEFAULT '',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
        )
    )");

    // Enable foreign keys
    q.exec("PRAGMA foreign_keys = ON");

    // FTS5 virtual table for document search
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS documents_vec (id INTEGER PRIMARY KEY AUTOINCREMENT, filename TEXT, content TEXT, chunk_index INTEGER, vector BLOB)
    )");

    // Settings key-value store
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS app_settings (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )");

    // Indexes for performance
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(session_id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sessions_updated ON sessions(updated_at DESC)");

    if (q.lastError().isValid()) {
        qWarning() << "Table creation error:" << q.lastError().text();
    }
}

// ============== Sessions ==============

int DatabaseManager::createSession(const QString &title, const QString &modelName,
                                    const QString &systemPrompt)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("INSERT INTO sessions (title, model_name, system_prompt) VALUES (?, ?, ?)");
    q.addBindValue(title);
    q.addBindValue(modelName);
    q.addBindValue(systemPrompt);

    if (q.exec()) {
        int id = q.lastInsertId().toInt();
        emit sessionCreated(id, title);
        return id;
    }
    qWarning() << "Create session failed:" << q.lastError().text();
    return -1;
}

QVariantList DatabaseManager::getSessions()
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.exec(R"(
        SELECT s.id, s.title, s.model_name, s.personality, s.updated_at,
               (SELECT COUNT(*) FROM messages WHERE session_id = s.id) as msg_count
        FROM sessions s
        ORDER BY s.updated_at DESC
    )");

    QVariantList sessions;
    while (q.next()) {
        QVariantMap session;
        session["id"] = q.value(0).toInt();
        session["title"] = q.value(1).toString();
        session["model_name"] = q.value(2).toString();
        session["personality"] = q.value(3).toString();
        session["updated_at"] = q.value(4).toString();
        session["message_count"] = q.value(5).toInt();
        sessions.append(session);
    }
    return sessions;
}

QVariantMap DatabaseManager::getSession(int sessionId)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("SELECT id, title, model_name, system_prompt, personality, created_at FROM sessions WHERE id = ?");
    q.addBindValue(sessionId);

    QVariantMap session;
    if (q.exec() && q.next()) {
        session["id"] = q.value(0).toInt();
        session["title"] = q.value(1).toString();
        session["model_name"] = q.value(2).toString();
        session["system_prompt"] = q.value(3).toString();
        session["personality"] = q.value(4).toString();
        session["created_at"] = q.value(5).toString();
    }
    return session;
}

void DatabaseManager::deleteSession(int sessionId)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);

    // Delete messages first (cascade might not work with all SQLite builds)
    q.prepare("DELETE FROM messages WHERE session_id = ?");
    q.addBindValue(sessionId);
    q.exec();

    q.prepare("DELETE FROM sessions WHERE id = ?");
    q.addBindValue(sessionId);
    if (q.exec()) {
        emit sessionDeleted(sessionId);
    }
}

void DatabaseManager::renameSession(int sessionId, const QString &newTitle)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("UPDATE sessions SET title = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
    q.addBindValue(newTitle);
    q.addBindValue(sessionId);
    if (q.exec()) {
        emit sessionRenamed(sessionId, newTitle);
    }
}

void DatabaseManager::updateSessionModel(int sessionId, const QString &modelName)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("UPDATE sessions SET model_name = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
    q.addBindValue(modelName);
    q.addBindValue(sessionId);
    q.exec();
}

void DatabaseManager::updateSessionPrompt(int sessionId, const QString &systemPrompt)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("UPDATE sessions SET system_prompt = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
    q.addBindValue(systemPrompt);
    q.addBindValue(sessionId);
    q.exec();
}

void DatabaseManager::updateSessionPersonality(int sessionId, const QString &personality)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("UPDATE sessions SET personality = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
    q.addBindValue(personality);
    q.addBindValue(sessionId);
    q.exec();
}

// ============== Messages ==============

void DatabaseManager::addMessage(int sessionId, const QString &sender, const QString &text,
                                  const QString &imageBase64)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("INSERT INTO messages (session_id, sender, text, image_base64) VALUES (?, ?, ?, ?)");
    q.addBindValue(sessionId);
    q.addBindValue(sender);
    q.addBindValue(text);
    q.addBindValue(imageBase64);

    if (q.exec()) {
        int msgId = q.lastInsertId().toInt();
        // Update session timestamp
        QSqlQuery u(db);
        u.prepare("UPDATE sessions SET updated_at = CURRENT_TIMESTAMP WHERE id = ?");
        u.addBindValue(sessionId);
        u.exec();
        emit messageAdded(sessionId, msgId);
    }
}

QVariantList DatabaseManager::getMessages(int sessionId, int limit)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare(R"(
        SELECT id, sender, text, image_base64, created_at
        FROM messages WHERE session_id = ?
        ORDER BY id ASC LIMIT ?
    )");
    q.addBindValue(sessionId);
    q.addBindValue(limit);

    QVariantList messages;
    if (q.exec()) {
        while (q.next()) {
            QVariantMap msg;
            msg["id"] = q.value(0).toInt();
            msg["sender"] = q.value(1).toString();
            msg["text"] = q.value(2).toString();
            msg["image_base64"] = q.value(3).toString();
            msg["created_at"] = q.value(4).toString();
            messages.append(msg);
        }
    }
    return messages;
}

QVariantList DatabaseManager::getRecentMessages(int sessionId, int limit)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    // Get the N most recent, but return them in chronological order
    q.prepare(R"(
        SELECT id, sender, text, image_base64, created_at FROM (
            SELECT id, sender, text, image_base64, created_at
            FROM messages WHERE session_id = ?
            ORDER BY id DESC LIMIT ?
        ) sub ORDER BY id ASC
    )");
    q.addBindValue(sessionId);
    q.addBindValue(limit);

    QVariantList messages;
    if (q.exec()) {
        while (q.next()) {
            QVariantMap msg;
            msg["id"] = q.value(0).toInt();
            msg["sender"] = q.value(1).toString();
            msg["text"] = q.value(2).toString();
            msg["image_base64"] = q.value(3).toString();
            msg["created_at"] = q.value(4).toString();
            messages.append(msg);
        }
    }
    return messages;
}

void DatabaseManager::deleteMessage(int messageId)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM messages WHERE id = ?");
    q.addBindValue(messageId);
    q.exec();
}

int DatabaseManager::getMessageCount(int sessionId)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM messages WHERE session_id = ?");
    q.addBindValue(sessionId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

QVariantList DatabaseManager::searchMessages(const QString &query, int limit)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare(R"(
        SELECT m.id, m.session_id, m.sender, m.text, s.title
        FROM messages m
        JOIN sessions s ON m.session_id = s.id
        WHERE m.text LIKE ?
        ORDER BY m.created_at DESC LIMIT ?
    )");
    q.addBindValue("%" + query + "%");
    q.addBindValue(limit);

    QVariantList results;
    if (q.exec()) {
        while (q.next()) {
            QVariantMap msg;
            msg["id"] = q.value(0).toInt();
            msg["session_id"] = q.value(1).toInt();
            msg["sender"] = q.value(2).toString();
            msg["text"] = q.value(3).toString();
            msg["session_title"] = q.value(4).toString();
            results.append(msg);
        }
    }
    return results;
}

// ============== Documents (FTS5) ==============

void DatabaseManager::indexDocument(const QString &filename, const QString &content)
{
    QSqlDatabase db = getConnection();

    QSqlQuery del(db);
    del.prepare("DELETE FROM documents_vec WHERE filename = ?");
    del.addBindValue(filename);
    del.exec();

    const int chunkSize = 800;
    const int overlap = 100;
    int chunkIndex = 0;

    for (int i = 0; i < content.length(); i += (chunkSize - overlap)) {
        QString chunk = content.mid(i, chunkSize).trimmed();
        if (chunk.isEmpty()) continue;

        // Basic character-frequency vectorization (simulating embedding)
        QByteArray vectorData;
        int vectorSize = 256;
        vectorData.resize(vectorSize * sizeof(float));
        float* vecPtr = reinterpret_cast<float*>(vectorData.data());
        for (int j = 0; j < vectorSize; ++j) vecPtr[j] = 0.0f;

        for (int j = 0; j < chunk.length(); ++j) {
            int charIdx = chunk[j].unicode() % vectorSize;
            vecPtr[charIdx] += 1.0f;
        }

        // Normalize
        float norm = 0.0f;
        for (int j = 0; j < vectorSize; ++j) norm += vecPtr[j] * vecPtr[j];
        if (norm > 0) {
            norm = std::sqrt(norm);
            for (int j = 0; j < vectorSize; ++j) vecPtr[j] /= norm;
        }

        QSqlQuery q(db);
        q.prepare("INSERT INTO documents_vec (filename, content, chunk_index, vector) VALUES (?, ?, ?, ?)");
        q.addBindValue(filename);
        q.addBindValue(chunk);
        q.addBindValue(QString::number(chunkIndex++));
        q.addBindValue(vectorData);
        q.exec();
    }

    emit documentIndexed(filename, chunkIndex);
}

QVariantList DatabaseManager::searchDocuments(const QString &query, int limit)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("SELECT filename, content, vector FROM documents_vec");

    QVariantList results;
    if (q.exec()) {
        // Create query vector
        int vectorSize = 256;
        std::vector<float> queryVec(vectorSize, 0.0f);
        for (int j = 0; j < query.length(); ++j) {
            int charIdx = query[j].unicode() % vectorSize;
            queryVec[charIdx] += 1.0f;
        }
        float norm = 0.0f;
        for (int j = 0; j < vectorSize; ++j) norm += queryVec[j] * queryVec[j];
        if (norm > 0) {
            norm = std::sqrt(norm);
            for (int j = 0; j < vectorSize; ++j) queryVec[j] /= norm;
        }

        QMap<double, QVariantMap> scoredDocs;

        while (q.next()) {
            QVariantMap doc;
            doc["filename"] = q.value(0).toString();
            doc["content"] = q.value(1).toString();

            QByteArray vecData = q.value(2).toByteArray();
            const float* vecPtr = reinterpret_cast<const float*>(vecData.constData());

            double dotProduct = 0.0;
            if (vecData.size() == vectorSize * sizeof(float)) {
                for(int i=0; i<vectorSize; ++i) {
                    dotProduct += queryVec[i] * vecPtr[i];
                }
            }

            doc["rank"] = -dotProduct; // lower rank value means more similar in this naive implementation
            scoredDocs.insert(-dotProduct, doc);
        }

        int count = 0;
        for (auto it = scoredDocs.begin(); it != scoredDocs.end() && count < limit; ++it, ++count) {
            results.append(it.value());
        }
    }
    return results;
}

void DatabaseManager::deleteDocument(const QString &filename)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("DELETE FROM documents_vec WHERE filename = ?");
    q.addBindValue(filename);
    q.exec();
}

QVariantList DatabaseManager::getIndexedDocuments()
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.exec("SELECT DISTINCT filename, COUNT(*) as chunks FROM documents_vec GROUP BY filename");

    QVariantList docs;
    while (q.next()) {
        QVariantMap doc;
        doc["filename"] = q.value(0).toString();
        doc["chunk_count"] = q.value(1).toInt();
        docs.append(doc);
    }
    return docs;
}

// ============== Settings KV Store ==============

QVariant DatabaseManager::getSetting(const QString &key, const QVariant &defaultValue)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("SELECT value FROM app_settings WHERE key = ?");
    q.addBindValue(key);
    if (q.exec() && q.next()) {
        return q.value(0);
    }
    return defaultValue;
}

void DatabaseManager::setSetting(const QString &key, const QVariant &value)
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO app_settings (key, value) VALUES (?, ?)");
    q.addBindValue(key);
    q.addBindValue(value.toString());
    q.exec();
}

// ============== Data Management ==============

void DatabaseManager::clearChatHistory()
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.exec("DELETE FROM messages");
    q.exec("DELETE FROM sessions");
}

void DatabaseManager::clearAllData()
{
    QSqlDatabase db = getConnection();
    QSqlQuery q(db);
    q.exec("DELETE FROM messages");
    q.exec("DELETE FROM sessions");
    q.exec("DELETE FROM documents_vec");
    q.exec("DELETE FROM app_settings");
}

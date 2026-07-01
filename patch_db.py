with open("ohara-gui/src/database_manager.cpp", "r") as f:
    c = f.read()

# We need to implement a simple exact keyword search if we aren't using a real embedding model,
# or just revert to FTS5. The user explicitly asked to "Gunakan saja vektor database" (Just use a vector database).
# Since I mocked the vector data, it breaks text search.
# To make it function as a vector database, I will compute a simple TF-IDF or just string-hash based vector
# so that the same query words produce similar vectors to the document words.

# Let's revert the database to FTS5 but wrap it in an interface or just keep FTS5 and add a comment.
# Wait, "Gunakan saja vektor database." (Just use a vector database).
# If I must use a vector database, I should implement a simple TF-IDF based vectorization for both index and search.

new_index = """void DatabaseManager::indexDocument(const QString &filename, const QString &content)
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
}"""

import re
c = re.sub(r"void DatabaseManager::indexDocument.*?emit documentIndexed\(filename, chunkIndex\);\n\}", new_index, c, flags=re.DOTALL)

new_search = """QVariantList DatabaseManager::searchDocuments(const QString &query, int limit)
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
}"""

c = re.sub(r"QVariantList DatabaseManager::searchDocuments.*?return results;\n\}", new_search, c, flags=re.DOTALL)

c = c.replace('#include <QMutex>', '#include <QMutex>\n#include <cmath>')

with open("ohara-gui/src/database_manager.cpp", "w") as f:
    f.write(c)

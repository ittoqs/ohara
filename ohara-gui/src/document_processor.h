#ifndef DOCUMENT_PROCESSOR_H
#define DOCUMENT_PROCESSOR_H

#include <QObject>
#include <QString>
#include <QVariantList>

class DatabaseManager;

class DocumentProcessor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool processing READ isProcessing NOTIFY processingChanged)

public:
    explicit DocumentProcessor(DatabaseManager *db, QObject *parent = nullptr);

    bool isProcessing() const;

    /**
     * Process and index a document file for RAG search.
     * Supports TXT and basic text extraction.
     * @param filePath Path to the document file
     */
    Q_INVOKABLE void processFile(const QString &filePath);

    /**
     * Remove a document from the index.
     */
    Q_INVOKABLE void removeDocument(const QString &filename);

    /**
     * Get list of indexed documents.
     */
    Q_INVOKABLE QVariantList getIndexedDocuments();

    /**
     * Search indexed documents for relevant context.
     * @param query Search query
     * @param limit Maximum results
     * @return List of matching text chunks
     */
    Q_INVOKABLE QVariantList searchContext(const QString &query, int limit = 3);

signals:
    void processingChanged();
    void documentProcessed(const QString &filename, int chunkCount);
    void processingError(const QString &filename, const QString &error);

private:
    QString readTextFile(const QString &filePath);
    QStringList chunkText(const QString &text, int chunkSize = 800, int overlap = 100);

    DatabaseManager *m_db;
    bool m_processing = false;
};

#endif // DOCUMENT_PROCESSOR_H

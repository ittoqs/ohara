#include "document_processor.h"
#include "database_manager.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <QThread>

DocumentProcessor::DocumentProcessor(DatabaseManager *db, QObject *parent)
    : QObject(parent), m_db(db)
{
}

bool DocumentProcessor::isProcessing() const
{
    return m_processing;
}

void DocumentProcessor::processFile(const QString &filePath)
{
    QString path = filePath;
    if (path.startsWith("file://")) {
        path = path.mid(7);
    }

    QFileInfo fi(path);
    QString filename = fi.fileName();
    QString extension = fi.suffix().toLower();

    // Validate file size (max 50MB)
    if (fi.size() > 50 * 1024 * 1024) {
        emit processingError(filename, "File too large (max 50MB)");
        return;
    }

    m_processing = true;
    emit processingChanged();

    QString content;

    if (extension == "txt" || extension == "md" || extension == "csv" ||
        extension == "json" || extension == "log" || extension == "xml" ||
        extension == "html" || extension == "htm") {
        content = readTextFile(path);
    } else if (extension == "pdf") {
        // PDF support: basic text extraction (reads raw text streams)
        // For full PDF support, link against a PDF library
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            // Simple PDF text extraction: find text between BT/ET operators
            // This is a basic fallback; for production, use poppler-qt6
            QString raw = QString::fromLatin1(data);
            // Try UTF-8 first
            content = QString::fromUtf8(data);
            if (content.contains(QChar(0))) {
                // Binary PDF - extract readable strings
                content.clear();
                for (int i = 0; i < raw.length(); i++) {
                    QChar c = raw[i];
                    if (c.isPrint() || c == '\n' || c == '\r' || c == '\t') {
                        content += c;
                    }
                }
            }
            file.close();
        }
    } else {
        m_processing = false;
        emit processingChanged();
        emit processingError(filename, "Unsupported file format: " + extension);
        return;
    }

    if (content.trimmed().isEmpty()) {
        m_processing = false;
        emit processingChanged();
        emit processingError(filename, "File is empty or could not be read");
        return;
    }

    // Chunk and index
    QStringList chunks = chunkText(content);
    int chunkCount = 0;

    // Remove existing entries
    m_db->deleteDocument(filename);

    for (const QString &chunk : chunks) {
        if (!chunk.trimmed().isEmpty()) {
            m_db->indexDocument(filename, chunk);
            chunkCount++;
        }
    }

    m_processing = false;
    emit processingChanged();
    emit documentProcessed(filename, chunkCount);
}

void DocumentProcessor::removeDocument(const QString &filename)
{
    m_db->deleteDocument(filename);
}

QVariantList DocumentProcessor::getIndexedDocuments()
{
    return m_db->getIndexedDocuments();
}

QVariantList DocumentProcessor::searchContext(const QString &query, int limit)
{
    return m_db->searchDocuments(query, limit);
}

QString DocumentProcessor::readTextFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return "";
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QString content = stream.readAll();
    file.close();
    return content;
}

QStringList DocumentProcessor::chunkText(const QString &text, int chunkSize, int overlap)
{
    QStringList chunks;
    int pos = 0;

    while (pos < text.length()) {
        int end = qMin(pos + chunkSize, text.length());

        // Try to break at a sentence boundary
        if (end < text.length()) {
            int lastPeriod = text.lastIndexOf('.', end);
            int lastNewline = text.lastIndexOf('\n', end);
            int breakPoint = qMax(lastPeriod, lastNewline);

            // Only use the break point if it's reasonably close
            if (breakPoint > pos + chunkSize / 2) {
                end = breakPoint + 1;
            }
        }

        QString chunk = text.mid(pos, end - pos).trimmed();
        if (!chunk.isEmpty()) {
            chunks.append(chunk);
        }

        pos = end - overlap;
        if (pos < 0) pos = 0;
        if (end >= text.length()) break;
    }

    return chunks;
}

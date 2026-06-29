#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QSignalSpy>
#include "document_processor.h"
#include "database_manager.h"

class TestDocumentProcessor : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testChunking();

private:
    DatabaseManager* m_dbManager;
    DocumentProcessor* m_processor;
    QTemporaryDir m_tempDir;
};

void TestDocumentProcessor::initTestCase()
{
    QString dbPath = m_tempDir.path() + "/test.db";
    m_dbManager = new DatabaseManager(dbPath);
    m_processor = new DocumentProcessor(m_dbManager);
}

void TestDocumentProcessor::cleanupTestCase()
{
    delete m_processor;
    delete m_dbManager;
}

void TestDocumentProcessor::testChunking()
{
    QString testFilePath = m_tempDir.path() + "/test.txt";
    QFile file(testFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        for(int i=0; i<100; i++) {
            out << "Line " << i << " of the test document. This is meant to be long enough to test chunking. ";
        }
        file.close();
    }

    QSignalSpy spyProcessed(m_processor, &DocumentProcessor::documentProcessed);

    m_processor->processFile(testFilePath);

    spyProcessed.wait(2000); // Wait for processing
    QVERIFY(spyProcessed.count() == 1);

    // Verify it was chunked into DB
    QVariantList chunks = m_dbManager->searchDocuments("Line");
    QVERIFY(chunks.size() > 0);
}

QTEST_MAIN(TestDocumentProcessor)
#include "tst_document_processor.moc"

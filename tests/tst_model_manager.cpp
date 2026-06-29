#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QNetworkReply>
#include "model_manager.h"
#include "settings_manager.h"

class TestModelManager : public QObject
{
    Q_OBJECT
private slots:
    void testDownloadErrorHandling();
};

void TestModelManager::testDownloadErrorHandling()
{
    QTemporaryDir tempDir;
    ModelManager manager(tempDir.path());

    QSignalSpy spyError(&manager, &ModelManager::downloadError);

    // Try downloading from an invalid URL
    manager.downloadModel("invalid/repo", "nonexistent.gguf");

    spyError.wait(5000);
    QVERIFY(spyError.count() == 1);
    QList<QVariant> args = spyError.takeFirst();
    QCOMPARE(args.at(0).toString(), QString("nonexistent.gguf"));
    // The error string should be something
    QVERIFY(!args.at(1).toString().isEmpty());
}

QTEST_MAIN(TestModelManager)
#include "tst_model_manager.moc"

#include <QTest>
#include <QSignalSpy>
#include "inference_engine.h"

class TestInferenceEngine : public QObject
{
    Q_OBJECT
private slots:
    void testInitialState();
};

void TestInferenceEngine::testInitialState()
{
    InferenceEngine engine;
    QCOMPARE(engine.isModelLoaded(), false);
    QCOMPARE(engine.isGenerating(), false);
    QCOMPARE(engine.tokensPerSecond(), 0.0);
    QCOMPARE(engine.contextUsed(), 0);
    QCOMPARE(engine.loadedModelName(), QString());
}

QTEST_MAIN(TestInferenceEngine)
#include "tst_inference_engine.moc"

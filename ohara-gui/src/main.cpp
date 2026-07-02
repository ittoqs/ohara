#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QDir>
#include <QStandardPaths>
#include <QSharedMemory>
#include <QClipboard>

#include "hardware_detector.h"
#include "settings_manager.h"
#include "database_manager.h"
#include "model_manager.h"
#include "inference_engine.h"
#include "document_processor.h"
#include "voice_manager.h"
#include "script_engine.h"
#include "js_engine.h"
#include "wasm_engine.h"

class ClipboardHelper : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void setText(const QString &text) {
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(text);
        }
    }
};

int main(int argc, char *argv[])
{
    // Application metadata
    QGuiApplication::setOrganizationName("Ohara");
    QGuiApplication::setOrganizationDomain("ohara-gpt.local");
    QGuiApplication::setApplicationName("Ohara GPT");
    QGuiApplication::setApplicationVersion("1.0.0");

    QGuiApplication app(argc, argv);

    // Single instance check
    QSharedMemory sharedMemory("OharaGPTSingleInstance");
    if (!sharedMemory.create(1)) {
        qWarning() << "Another instance is already running. Exiting.";
        return 0;
    }

    // ---- Initialize Core Components ----

    // Settings manager (must be first — provides paths)
    SettingsManager settingsManager;

    // Hardware detector
    HardwareDetector hwDetector;
    hwDetector.startMonitoring(10000); // Update RAM every 10s

    // Database manager (SQLite with WAL)
    DatabaseManager dbManager(settingsManager.databasePath());

    // Model manager (downloads and file management)
    ModelManager modelManager(settingsManager.modelsDirectory());

    // Inference engine (embedded llama.cpp)
    InferenceEngine inferenceEngine;

    // Document processor (RAG via FTS5)
    DocumentProcessor docProcessor(&dbManager);

    // Voice manager
    VoiceManager voiceManager;

    // Script engine
    ScriptEngine scriptEngine;

    // JS engine
    JsEngine jsEngine;

    // Wasm engine
    WasmEngine wasmEngine;

    // Clipboard helper
    ClipboardHelper clipboardHelper;

    // ---- Setup QML Engine ----

    QQmlApplicationEngine engine;

    // Expose all components to QML
    engine.rootContext()->setContextProperty("HardwareDetector", &hwDetector);
    engine.rootContext()->setContextProperty("Settings", &settingsManager);
    engine.rootContext()->setContextProperty("Database", &dbManager);
    engine.rootContext()->setContextProperty("ModelManager", &modelManager);
    engine.rootContext()->setContextProperty("InferenceEngine", &inferenceEngine);
    engine.rootContext()->setContextProperty("DocProcessor", &docProcessor);
    engine.rootContext()->setContextProperty("VoiceManager", &voiceManager);
    engine.rootContext()->setContextProperty("ScriptEngine", &scriptEngine);
    engine.rootContext()->setContextProperty("JsEngine", &jsEngine);
    engine.rootContext()->setContextProperty("WasmEngine", &wasmEngine);
    engine.rootContext()->setContextProperty("Clipboard", &clipboardHelper);

    // App version for QML
    engine.rootContext()->setContextProperty("AppVersion", app.applicationVersion());

    // ---- Load QML ----

    const QUrl url(u"qrc:/Ohara/qml/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
#include "main.moc"

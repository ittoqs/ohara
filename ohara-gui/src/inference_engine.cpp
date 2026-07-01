#include "inference_engine.h"
#include <QDebug>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMetaObject>
#include <functional>

// llama.cpp headers
#include "llama.h"
#include "common.h"
#include "LlamaBackend.h"
#include "LlamaBackend.h"

InferenceEngine::InferenceEngine(QObject *parent)
    : QObject(parent)
{
    // Initialize llama.cpp backend
    m_backend = new LlamaBackend();
}

InferenceEngine::~InferenceEngine()
{
    stopGeneration();
    unloadModel();
    delete m_backend;
}

bool InferenceEngine::isModelLoaded() const
{
    return m_backend && m_backend->isModelLoaded();
}

bool InferenceEngine::isGenerating() const
{
    return m_generating.load();
}

double InferenceEngine::tokensPerSecond() const
{
    return m_tokensPerSecond;
}

int InferenceEngine::contextUsed() const
{
    return m_contextUsed;
}

int InferenceEngine::contextTotal() const
{
    return m_backend ? m_backend->contextTotal() : m_nCtx;
}

QString InferenceEngine::loadedModelName() const
{
    return m_modelName;
}

void InferenceEngine::loadModel(const QString &modelPath, int nCtx, int nGpuLayers,
                                 const QString &mmprojPath)
{
    Q_UNUSED(mmprojPath) // TODO: multimodal support in future

    if (isGenerating()) {
        emit modelLoadFinished(false, "Cannot load model while generating");
        return;
    }

    m_modelName = QFileInfo(modelPath).baseName();
    emit modelLoadStarted(m_modelName);
    QString path = modelPath;
    QFuture<void> future = QtConcurrent::run([this, path, nCtx, nGpuLayers]() {
        doLoadModel(path, nCtx, nGpuLayers);
    });
}

void InferenceEngine::doLoadModel(const QString &modelPath, int nCtx, int nGpuLayers)
{
    QMutexLocker locker(&m_modelMutex);
    bool success = false;
    if (m_backend) {
        success = m_backend->loadModel(modelPath, nCtx, nGpuLayers);
    }

    if (!success) {
        QMetaObject::invokeMethod(this, [this]() {
            m_modelName.clear();
            emit modelLoadFinished(false, "Failed to load model file");
            emit modelLoadedChanged();
        }, Qt::QueuedConnection);
        return;
    }

    m_modelPath = modelPath;
    m_nCtx = ((LlamaBackend*)m_backend)->contextTotal();

    QMetaObject::invokeMethod(this, [this]() {
        emit modelLoadFinished(true, "");
        emit modelLoadedChanged();
    }, Qt::QueuedConnection);
}

void InferenceEngine::unloadModel()
{
    QMutexLocker locker(&m_modelMutex);

    if (m_backend) {
        m_backend->unloadModel();
    }
    m_modelPath.clear();
    m_modelName.clear();
    m_tokensPerSecond = 0.0;
    m_contextUsed = 0;

    emit modelLoadedChanged();
    emit statsUpdated();
}





void InferenceEngine::generate(const QVariantList &messages, int maxTokens,
                                double temperature, double topP)
{
    if (!isModelLoaded()) {
        emit generationError("No model loaded");
        return;
    }

    if (isGenerating()) {
        emit generationError("Generation already in progress");
        return;
    }

    m_stopRequested.store(false);
    m_generating.store(true);
    emit generatingChanged();

    QVariantList msgsCopy = messages;
    QFuture<void> future = QtConcurrent::run([this, msgsCopy, maxTokens, temperature, topP]() {
        doGenerate(msgsCopy, maxTokens, temperature, topP);
    });
}

void InferenceEngine::doGenerate(QVariantList messages, int maxTokens,
                                  double temperature, double topP)
{
    QMutexLocker locker(&m_modelMutex);

    if (!m_backend || !m_backend->isModelLoaded()) {
        QMetaObject::invokeMethod(this, [this]() {
            m_generating.store(false);
            emit generationError("Model not loaded");
            emit generatingChanged();
        }, Qt::QueuedConnection);
        return;
    }

    // Fully delegated generation to backend
    m_backend->generate(messages, maxTokens, temperature, topP,
        [this](const std::string& token, int generated, int contextUsed, double tps) {
            m_tokensPerSecond = tps;
            m_contextUsed = contextUsed;
            QString qPiece = QString::fromStdString(token);
            if(m_stopRequested.load()) m_backend->stopGeneration();
            QMetaObject::invokeMethod(this, [this, qPiece]() {
                emit tokenGenerated(qPiece);
                emit statsUpdated();
            }, Qt::QueuedConnection);
        },
        [this](const std::string& error) {
            QMetaObject::invokeMethod(this, [this, error]() {
                m_generating.store(false);
                emit generationError(QString::fromStdString(error));
                emit generatingChanged();
            }, Qt::QueuedConnection);
        },
        [this](const std::string& fullResponse) {
            QString qFullResponse = QString::fromStdString(fullResponse);
            QMetaObject::invokeMethod(this, [this, qFullResponse]() {
                m_generating.store(false);
                emit generationFinished(qFullResponse);
                emit generatingChanged();
                emit statsUpdated();
            }, Qt::QueuedConnection);
        }
    );
}

void InferenceEngine::stopGeneration()
{
    m_stopRequested.store(true);
}

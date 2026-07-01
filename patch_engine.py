with open("ohara-gui/src/inference_engine.cpp", "r") as f:
    ic = f.read()

# I previously introduced a cast in buildPrompt etc. Let's fix that.
# The issue complained about InferenceEngine casting back to LlamaBackend and using llama C API directly.
# Wait, let's fix ILlmBackend interface fully to hide llama tokens.

new_gen = """void InferenceEngine::doGenerate(QVariantList messages, int maxTokens,
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
}"""

import re
ic = re.sub(r"void InferenceEngine::doGenerate.*?Qt::QueuedConnection\);\n\}", new_gen, ic, flags=re.DOTALL)

with open("ohara-gui/src/inference_engine.cpp", "w") as f:
    f.write(ic)


with open("ohara-gui/src/ILlmBackend.h", "r") as f:
    ih = f.read()

if "functional" not in ih:
    ih = ih.replace('#include <string>', '#include <string>\n#include <functional>')

if "virtual void generate" not in ih:
    ih = ih.replace('// Additional parameters / accessors might be added as needed.', """
    virtual int contextTotal() const = 0;
    virtual void stopGeneration() = 0;
    virtual void generate(const QVariantList &messages, int maxTokens, double temperature, double topP,
                          std::function<void(const std::string&, int, int, double)> onToken,
                          std::function<void(const std::string&)> onError,
                          std::function<void(const std::string&)> onFinished) = 0;""")

with open("ohara-gui/src/ILlmBackend.h", "w") as f:
    f.write(ih)

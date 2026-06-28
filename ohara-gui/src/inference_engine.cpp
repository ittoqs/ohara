#include "inference_engine.h"
#include <QDebug>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMetaObject>
#include <functional>

// llama.cpp headers
#include "llama.h"
#include "common.h"

InferenceEngine::InferenceEngine(QObject *parent)
    : QObject(parent)
{
    // Initialize llama.cpp backend
    llama_backend_init();
}

InferenceEngine::~InferenceEngine()
{
    stopGeneration();
    unloadModel();
    llama_backend_free();
}

bool InferenceEngine::isModelLoaded() const
{
    return m_model != nullptr && m_ctx != nullptr;
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
    return m_nCtx;
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

    // Run on worker thread to avoid blocking GUI
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
    }

    m_workerThread = new QThread(this);
    auto *worker = new QObject();
    worker->moveToThread(m_workerThread);

    QString path = modelPath;
    connect(m_workerThread, &QThread::started, worker, [this, worker, path, nCtx, nGpuLayers]() {
        doLoadModel(path, nCtx, nGpuLayers);
        worker->deleteLater();
        m_workerThread->quit();
    });

    m_modelName = QFileInfo(modelPath).baseName();
    emit modelLoadStarted(m_modelName);
    m_workerThread->start();
}

void InferenceEngine::doLoadModel(const QString &modelPath, int nCtx, int nGpuLayers)
{
    QMutexLocker locker(&m_modelMutex);

    // Unload previous model
    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_model_free(m_model);
        m_model = nullptr;
    }

    // Model params
    auto model_params = llama_model_default_params();
    model_params.n_gpu_layers = nGpuLayers;

    // Load model
    std::string pathStr = modelPath.toStdString();
    m_model = llama_model_load_from_file(pathStr.c_str(), model_params);

    if (!m_model) {
        QMetaObject::invokeMethod(this, [this]() {
            m_modelName.clear();
            emit modelLoadFinished(false, "Failed to load model file");
            emit modelLoadedChanged();
        }, Qt::QueuedConnection);
        return;
    }

    // Context params
    auto ctx_params = llama_context_default_params();

    // Auto-detect context size if not specified
    if (nCtx <= 0) {
        nCtx = 4096; // Safe default
    }
    m_nCtx = nCtx;
    ctx_params.n_ctx = nCtx;
    ctx_params.n_batch = 512;
    ctx_params.n_threads = 0; // 0 = auto-detect thread count

    m_ctx = llama_init_from_model(m_model, ctx_params);

    if (!m_ctx) {
        llama_model_free(m_model);
        m_model = nullptr;
        QMetaObject::invokeMethod(this, [this]() {
            m_modelName.clear();
            emit modelLoadFinished(false, "Failed to create context");
            emit modelLoadedChanged();
        }, Qt::QueuedConnection);
        return;
    }

    m_modelPath = modelPath;

    QMetaObject::invokeMethod(this, [this]() {
        emit modelLoadFinished(true, "");
        emit modelLoadedChanged();
    }, Qt::QueuedConnection);
}

void InferenceEngine::unloadModel()
{
    QMutexLocker locker(&m_modelMutex);

    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_model_free(m_model);
        m_model = nullptr;
    }
    m_modelPath.clear();
    m_modelName.clear();
    m_tokensPerSecond = 0.0;
    m_contextUsed = 0;

    emit modelLoadedChanged();
    emit statsUpdated();
}

std::string InferenceEngine::buildPrompt(const QVariantList &messages)
{
    // Try to use the model's built-in chat template
    std::vector<llama_chat_message> chat_messages;
    std::vector<std::string> role_storage;
    std::vector<std::string> content_storage;

    for (const QVariant &v : messages) {
        QVariantMap msg = v.toMap();
        role_storage.push_back(msg["role"].toString().toStdString());
        content_storage.push_back(msg["content"].toString().toStdString());

        llama_chat_message cm;
        cm.role = role_storage.back().c_str();
        cm.content = content_storage.back().c_str();
        chat_messages.push_back(cm);
    }

    // First call to get required buffer size
    int buf_size = llama_chat_apply_template(
        m_model, nullptr,
        chat_messages.data(), chat_messages.size(),
        true, nullptr, 0);

    if (buf_size < 0) {
        // Fallback: build ChatML format manually
        std::string prompt;
        for (const QVariant &v : messages) {
            QVariantMap msg = v.toMap();
            std::string role = msg["role"].toString().toStdString();
            std::string content = msg["content"].toString().toStdString();
            prompt += "<|im_start|>" + role + "\n" + content + "<|im_end|>\n";
        }
        prompt += "<|im_start|>assistant\n";
        return prompt;
    }

    std::vector<char> buf(buf_size + 1);
    llama_chat_apply_template(
        m_model, nullptr,
        chat_messages.data(), chat_messages.size(),
        true, buf.data(), buf.size());

    return std::string(buf.data(), buf_size);
}

std::string InferenceEngine::tokenToString(int token)
{
    std::vector<char> buf(32);
    int n = llama_token_to_piece(m_model, token, buf.data(), buf.size(), 0, false);
    if (n < 0) {
        buf.resize(-n);
        n = llama_token_to_piece(m_model, token, buf.data(), buf.size(), 0, false);
    }
    return std::string(buf.data(), n > 0 ? n : 0);
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

    // Run generation on worker thread
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
    }

    m_workerThread = new QThread(this);
    auto *worker = new QObject();
    worker->moveToThread(m_workerThread);

    QVariantList msgsCopy = messages;
    connect(m_workerThread, &QThread::started, worker,
            [this, worker, msgsCopy, maxTokens, temperature, topP]() {
        doGenerate(msgsCopy, maxTokens, temperature, topP);
        worker->deleteLater();
        m_workerThread->quit();
    });

    m_workerThread->start();
}

void InferenceEngine::doGenerate(QVariantList messages, int maxTokens,
                                  double temperature, double topP)
{
    QMutexLocker locker(&m_modelMutex);

    if (!m_model || !m_ctx) {
        QMetaObject::invokeMethod(this, [this]() {
            m_generating.store(false);
            emit generationError("Model not loaded");
            emit generatingChanged();
        }, Qt::QueuedConnection);
        return;
    }

    // Build prompt from messages
    std::string prompt = buildPrompt(messages);

    // Tokenize
    const int n_prompt_max = m_nCtx - maxTokens;
    std::vector<llama_token> tokens(n_prompt_max);
    int n_tokens = llama_tokenize(m_model, prompt.c_str(), prompt.length(),
                                   tokens.data(), tokens.size(), true, true);

    if (n_tokens < 0) {
        QMetaObject::invokeMethod(this, [this]() {
            m_generating.store(false);
            emit generationError("Tokenization failed — prompt may be too long");
            emit generatingChanged();
        }, Qt::QueuedConnection);
        return;
    }
    tokens.resize(n_tokens);

    // Clear KV cache
    llama_kv_cache_clear(m_ctx);

    // Process prompt
    llama_batch batch = llama_batch_get_one(tokens.data(), n_tokens);
    if (llama_decode(m_ctx, batch) != 0) {
        QMetaObject::invokeMethod(this, [this]() {
            m_generating.store(false);
            emit generationError("Failed to process prompt");
            emit generatingChanged();
        }, Qt::QueuedConnection);
        return;
    }

    // Setup sampler chain
    auto sparams = llama_sampler_chain_default_params();
    llama_sampler *sampler = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(static_cast<float>(temperature)));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(static_cast<float>(topP), 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    // Generate tokens
    QElapsedTimer timer;
    timer.start();
    std::string fullResponse;
    int generated = 0;
    llama_token eosToken = llama_token_eos(m_model);

    int n_cur = n_tokens;

    for (int i = 0; i < maxTokens; i++) {
        if (m_stopRequested.load()) break;

        llama_token new_token = llama_sampler_sample(sampler, m_ctx, -1);

        // Check for end of sequence
        if (llama_token_is_eog(m_model, new_token)) break;

        std::string piece = tokenToString(new_token);
        fullResponse += piece;
        generated++;

        // Update stats
        double elapsed = timer.elapsed() / 1000.0;
        if (elapsed > 0) {
            m_tokensPerSecond = generated / elapsed;
        }
        m_contextUsed = n_cur + generated;

        // Emit token on main thread
        QString qPiece = QString::fromStdString(piece);
        QMetaObject::invokeMethod(this, [this, qPiece]() {
            emit tokenGenerated(qPiece);
            emit statsUpdated();
        }, Qt::QueuedConnection);

        // Prepare for next token
        llama_batch next_batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(m_ctx, next_batch) != 0) {
            QMetaObject::invokeMethod(this, [this]() {
                emit generationError("Decode error during generation");
            }, Qt::QueuedConnection);
            break;
        }
    }

    llama_sampler_free(sampler);

    QString qFullResponse = QString::fromStdString(fullResponse);
    QMetaObject::invokeMethod(this, [this, qFullResponse]() {
        m_generating.store(false);
        emit generationFinished(qFullResponse);
        emit generatingChanged();
        emit statsUpdated();
    }, Qt::QueuedConnection);
}

void InferenceEngine::stopGeneration()
{
    m_stopRequested.store(true);
}

#include "LlamaBackend.h"
#include "llama.h"

LlamaBackend::LlamaBackend() {
    llama_backend_init();
}

LlamaBackend::~LlamaBackend() {
    unloadModel();
    llama_backend_free();
}

bool LlamaBackend::loadModel(const QString &modelPath, int nCtx, int nGpuLayers) {
    unloadModel();

    auto model_params = llama_model_default_params();
    model_params.n_gpu_layers = nGpuLayers;

    std::string pathStr = modelPath.toStdString();
    m_model = llama_model_load_from_file(pathStr.c_str(), model_params);
    if (!m_model) return false;

    auto ctx_params = llama_context_default_params();
    if (nCtx <= 0) nCtx = 4096;
    m_nCtx = nCtx;
    ctx_params.n_ctx = nCtx;
    ctx_params.n_batch = 512;
    ctx_params.n_threads = 0;

    m_ctx = llama_init_from_model(m_model, ctx_params);
    if (!m_ctx) {
        unloadModel();
        return false;
    }
    return true;
}

void LlamaBackend::unloadModel() {
    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_model_free(m_model);
        m_model = nullptr;
    }
}

bool LlamaBackend::isModelLoaded() const {
    return m_model != nullptr && m_ctx != nullptr;
}

std::string LlamaBackend::buildPrompt(const QVariantList &messages) {
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

    int buf_size = llama_chat_apply_template(
        m_model ? llama_model_chat_template(m_model, nullptr) : nullptr,
        chat_messages.data(), chat_messages.size(),
        true, nullptr, 0);

    if (buf_size < 0) {
        std::string prompt;
        for (const QVariant &v : messages) {
            QVariantMap msg = v.toMap();
            prompt += "<|im_start|>" + msg["role"].toString().toStdString() + "\n" + msg["content"].toString().toStdString() + "<|im_end|>\n";
        }
        prompt += "<|im_start|>assistant\n";
        return prompt;
    }

    std::vector<char> buf(buf_size + 1);
    llama_chat_apply_template(
        m_model ? llama_model_chat_template(m_model, nullptr) : nullptr,
        chat_messages.data(), chat_messages.size(),
        true, buf.data(), buf.size());

    return std::string(buf.data(), buf_size);
}

std::string LlamaBackend::tokenToString(int token) {
    if (!m_model) return "";
    std::vector<char> buf(32);
    int n = llama_token_to_piece(llama_model_get_vocab(m_model), token, buf.data(), buf.size(), 0, false);
    if (n < 0) {
        buf.resize(-n);
        n = llama_token_to_piece(llama_model_get_vocab(m_model), token, buf.data(), buf.size(), 0, false);
    }
    return std::string(buf.data(), n > 0 ? n : 0);
}

#include <QElapsedTimer>
#include <QVariantMap>

void LlamaBackend::generate(const QVariantList &messages, int maxTokens, double temperature, double topP,
                  std::function<void(const std::string&, int, int, double)> onToken,
                  std::function<void(const std::string&)> onError,
                  std::function<void(const std::string&)> onFinished)
{
    m_stop = false;
    std::string prompt = buildPrompt(messages);
    const int n_prompt_max = m_nCtx - maxTokens;
    std::vector<llama_token> tokens(n_prompt_max);
    int n_tokens = llama_tokenize(llama_model_get_vocab(m_model), prompt.c_str(), prompt.length(),
                                   tokens.data(), tokens.size(), true, true);

    if (n_tokens < 0) {
        onError("Tokenization failed — prompt may be too long");
        return;
    }
    tokens.resize(n_tokens);

    // KV Cache shifting: remove from m_prev_n_tokens to end
    llama_kv_self_seq_rm(m_ctx, 0, m_prev_n_tokens, -1);
    m_prev_n_tokens = n_tokens;

    llama_batch batch = llama_batch_get_one(tokens.data(), n_tokens);
    if (llama_decode(m_ctx, batch) != 0) {
        onError("Failed to process prompt");
        return;
    }

    auto sparams = llama_sampler_chain_default_params();
    llama_sampler *sampler = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(static_cast<float>(temperature)));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(static_cast<float>(topP), 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    QElapsedTimer timer;
    timer.start();
    std::string fullResponse;
    int generated = 0;

    int n_cur = n_tokens;

    for (int i = 0; i < maxTokens; i++) {
        if (m_stop.load()) break;

        llama_token new_token = llama_sampler_sample(sampler, m_ctx, -1);

        if (llama_vocab_is_eog(llama_model_get_vocab(m_model), new_token)) break;

        std::string piece = tokenToString(new_token);
        fullResponse += piece;
        generated++;

        double elapsed = timer.elapsed() / 1000.0;
        double tps = (elapsed > 0) ? (generated / elapsed) : 0;

        onToken(piece, generated, n_cur + generated, tps);

        llama_batch next_batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(m_ctx, next_batch) != 0) {
            onError("Decode error during generation");
            break;
        }
    }

    llama_sampler_free(sampler);
    onFinished(fullResponse);
}

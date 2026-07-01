with open("ohara-gui/src/LlamaBackend.cpp", "a") as f:
    f.write("""
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
""")

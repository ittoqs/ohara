#ifndef LLAMABACKEND_H
#define LLAMABACKEND_H

#include "ILlmBackend.h"
#include <atomic>
#include <vector>

struct llama_model;
struct llama_context;

class LlamaBackend : public ILlmBackend {
public:
    LlamaBackend();
    ~LlamaBackend() override;

    bool loadModel(const QString &modelPath, int nCtx, int nGpuLayers) override;
    void unloadModel() override;
    bool isModelLoaded() const override;

    std::string buildPrompt(const QVariantList &messages) override;
    std::string tokenToString(int token) override;

    llama_model* model() const { return m_model; }
    int contextTotal() const override { return m_nCtx; }
    void stopGeneration() override { m_stop = true; }
    void generate(const QVariantList &messages, int maxTokens, double temperature, double topP,
                  std::function<void(const std::string&, int, int, double)> onToken,
                  std::function<void(const std::string&)> onError,
                  std::function<void(const std::string&)> onFinished) override;


private:
    std::atomic<bool> m_stop{false};
    int m_prev_n_tokens = 0;
    llama_model *m_model = nullptr;
    llama_context *m_ctx = nullptr;
    int m_nCtx = 4096;
};

#endif // LLAMABACKEND_H

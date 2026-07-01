with open("ohara-gui/src/LlamaBackend.h", "r") as f:
    ih = f.read()

if "virtual void generate" not in ih:
    ih = ih.replace('    llama_context* ctx() const { return m_ctx; }\n    int contextTotal() const { return m_nCtx; }', """    int contextTotal() const override { return m_nCtx; }
    void stopGeneration() override { m_stop = true; }
    void generate(const QVariantList &messages, int maxTokens, double temperature, double topP,
                  std::function<void(const std::string&, int, int, double)> onToken,
                  std::function<void(const std::string&)> onError,
                  std::function<void(const std::string&)> onFinished) override;
""")
    ih = ih.replace('private:', 'private:\n    std::atomic<bool> m_stop{false};\n    int m_prev_n_tokens = 0;')

with open("ohara-gui/src/LlamaBackend.h", "w") as f:
    f.write(ih)

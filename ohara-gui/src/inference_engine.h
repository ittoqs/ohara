#ifndef INFERENCE_ENGINE_H
#define INFERENCE_ENGINE_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QMutex>
#include <QThread>
#include <atomic>

// Forward declarations for llama.cpp types
struct llama_model;
struct llama_context;
struct llama_sampler;

class InferenceEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool modelLoaded READ isModelLoaded NOTIFY modelLoadedChanged)
    Q_PROPERTY(bool generating READ isGenerating NOTIFY generatingChanged)
    Q_PROPERTY(double tokensPerSecond READ tokensPerSecond NOTIFY statsUpdated)
    Q_PROPERTY(int contextUsed READ contextUsed NOTIFY statsUpdated)
    Q_PROPERTY(int contextTotal READ contextTotal NOTIFY modelLoadedChanged)
    Q_PROPERTY(QString loadedModelName READ loadedModelName NOTIFY modelLoadedChanged)

public:
    explicit InferenceEngine(QObject *parent = nullptr);
    ~InferenceEngine();

    bool isModelLoaded() const;
    bool isGenerating() const;
    double tokensPerSecond() const;
    int contextUsed() const;
    int contextTotal() const;
    QString loadedModelName() const;

    /**
     * Load a GGUF model from disk.
     * @param modelPath Full path to the .gguf file
     * @param nCtx Context window size (0 = auto based on RAM)
     * @param nGpuLayers Number of GPU layers (-1 = auto, 0 = CPU only)
     * @param mmprojPath Optional: path to mmproj file for multimodal models
     */
    Q_INVOKABLE void loadModel(const QString &modelPath, int nCtx = 0,
                                int nGpuLayers = 0, const QString &mmprojPath = "");
    Q_INVOKABLE void unloadModel();

    /**
     * Generate a response from the model.
     * @param messages Array of {role: "system"|"user"|"assistant", content: "..."}
     * @param maxTokens Maximum tokens to generate
     * @param temperature Sampling temperature (0.0-2.0)
     * @param topP Top-p sampling (0.0-1.0)
     */
    Q_INVOKABLE void generate(const QVariantList &messages, int maxTokens = 2048,
                               double temperature = 0.7, double topP = 0.9);

    Q_INVOKABLE void stopGeneration();

signals:
    void modelLoadedChanged();
    void generatingChanged();
    void statsUpdated();
    void modelLoadStarted(const QString &modelName);
    void modelLoadFinished(bool success, const QString &error);
    void tokenGenerated(const QString &token);
    void generationFinished(const QString &fullResponse);
    void generationError(const QString &error);

private:
    void doLoadModel(const QString &modelPath, int nCtx, int nGpuLayers);
    void doGenerate(QVariantList messages, int maxTokens, double temperature, double topP);
    std::string buildPrompt(const QVariantList &messages);
    std::string tokenToString(int token);

    llama_model *m_model = nullptr;
    llama_context *m_ctx = nullptr;
    QString m_modelPath;
    QString m_modelName;
    int m_nCtx = 4096;

    std::atomic<bool> m_generating{false};
    std::atomic<bool> m_stopRequested{false};
    double m_tokensPerSecond = 0.0;
    int m_contextUsed = 0;

    QMutex m_modelMutex;
    QThread *m_workerThread = nullptr;
};

#endif // INFERENCE_ENGINE_H

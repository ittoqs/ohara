#ifndef ILLMBACKEND_H
#define ILLMBACKEND_H

#include <QString>
#include <QVariantList>
#include <string>
#include <functional>

// Abstract backend interface
class ILlmBackend {
public:
    virtual ~ILlmBackend() = default;

    virtual bool loadModel(const QString &modelPath, int nCtx, int nGpuLayers) = 0;
    virtual void unloadModel() = 0;
    virtual bool isModelLoaded() const = 0;

    virtual std::string buildPrompt(const QVariantList &messages) = 0;
    virtual std::string tokenToString(int token) = 0;


    virtual int contextTotal() const = 0;
    virtual void stopGeneration() = 0;
    virtual void generate(const QVariantList &messages, int maxTokens, double temperature, double topP,
                          std::function<void(const std::string&, int, int, double)> onToken,
                          std::function<void(const std::string&)> onError,
                          std::function<void(const std::string&)> onFinished) = 0;
};

#endif // ILLMBACKEND_H

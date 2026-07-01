#include "model_manager.h"
#include <QNetworkRequest>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QDebug>
#include "settings_manager.h"

ModelManager::ModelManager(const QString &modelsDir, QObject *parent)
    : QObject(parent), m_modelsDir(modelsDir),
      m_networkManager(new QNetworkAccessManager(this))
{
    QDir dir;
    dir.mkpath(m_modelsDir);
}

ModelManager::~ModelManager()
{
    cancelDownload();
}

QString ModelManager::buildDownloadUrl(const QString &repoId, const QString &filename) const
{
    SettingsManager settings;
    QString urlTemplate = settings.hfBaseUrl();
    return urlTemplate.arg(repoId, filename);
}

// ============== Model Catalog ==============

QVariantList ModelManager::getModelCatalog(double ramGB) const
{
    QVariantList models;

    auto addModel = [&](const QString &name, const QString &type, const QString &desc,
                        int minRam, const QString &repoId, const QString &filename,
                        const QString &mmproj = "", qint64 sizeBytes = 0) {
        QVariantMap m;
        m["name"] = name;
        m["type"] = type;
        m["description"] = desc;
        m["minRamGB"] = minRam;
        m["repoId"] = repoId;
        m["filename"] = filename;
        m["mmprojFilename"] = mmproj;
        m["sizeBytes"] = sizeBytes;
        m["downloaded"] = isModelDownloaded(filename);
        m["compatible"] = (ramGB >= minRam);
        models.append(m);
    };

    addModel("TinyLlama-1.1B-Chat",
             "Testing", "Ultra-lightweight model for testing inference speed.",
             2, "TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF",
             "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf", "", 670000000LL);

    addModel("Phi-3-Mini-4k-Instruct",
             "Text (Light)", "Fast, efficient model for low-spec devices.",
             4, "microsoft/Phi-3-mini-4k-instruct-gguf",
             "Phi-3-mini-4k-instruct-q4.gguf", "", 2400000000LL);

    addModel("Llama-3-8B-Instruct",
             "General & Assistant", "Robust model for general conversation and complex tasks.",
             8, "QuantFactory/Meta-Llama-3-8B-Instruct-GGUF",
             "Meta-Llama-3-8B-Instruct.Q4_K_M.gguf", "", 4920000000LL);

    addModel("Llava-1.5-7B",
             "Vision & Text", "Multimodal model for image and text understanding.",
             8, "mys/ggml_llava-v1.5-7b",
             "ggml-model-q4_k.gguf", "mmproj-model-f16.gguf", 4080000000LL);

    addModel("Qwen-2.5-Coder-7B",
             "Coding", "Expert model for code writing, review, and software development.",
             8, "Qwen/Qwen2.5-Coder-7B-Instruct-GGUF",
             "qwen2.5-coder-7b-instruct-q4_k_m.gguf", "", 4680000000LL);

    addModel("DeepSeek-R1-Distill-Qwen-7B",
             "Reasoning", "Advanced reasoning model with chain-of-thought capabilities.",
             8, "bartowski/DeepSeek-R1-Distill-Qwen-7B-GGUF",
             "DeepSeek-R1-Distill-Qwen-7B-Q4_K_M.gguf", "", 4680000000LL);

    addModel("Qwen-2.5-Coder-14B",
             "Coding (Large)", "Highly capable coding model for complex software tasks.",
             16, "Qwen/Qwen2.5-Coder-14B-Instruct-GGUF",
             "qwen2.5-coder-14b-instruct-q4_k_m.gguf", "", 8990000000LL);

    return models;
}

QVariantList ModelManager::getDownloadedModels() const
{
    QVariantList downloaded;
    QDir dir(m_modelsDir);
    QFileInfoList files = dir.entryInfoList(QStringList() << "*.gguf", QDir::Files);

    for (const QFileInfo &fi : files) {
        // Skip mmproj files
        if (fi.fileName().startsWith("mmproj")) continue;

        QVariantMap m;
        m["filename"] = fi.fileName();
        m["path"] = fi.absoluteFilePath();
        m["sizeBytes"] = fi.size();
        m["sizeMB"] = fi.size() / (1024.0 * 1024.0);
        downloaded.append(m);
    }
    return downloaded;
}

bool ModelManager::isModelDownloaded(const QString &filename) const
{
    return QFile::exists(m_modelsDir + "/" + filename);
}

qint64 ModelManager::getModelFileSize(const QString &filename) const
{
    QFileInfo fi(m_modelsDir + "/" + filename);
    return fi.exists() ? fi.size() : 0;
}

QString ModelManager::getModelPath(const QString &filename) const
{
    return m_modelsDir + "/" + filename;
}

// ============== Download Management ==============

void ModelManager::downloadModel(const QString &repoId, const QString &filename,
                                  const QString &mmprojFilename)
{
    if (m_currentDownload) {
        qWarning() << "Download already in progress";
        return;
    }

    m_pendingMmproj = mmprojFilename;
    m_pendingRepoId = repoId;
    startSingleDownload(repoId, filename);
}

void ModelManager::startSingleDownload(const QString &repoId, const QString &filename)
{
    QString url = buildDownloadUrl(repoId, filename);
    m_currentFilename = filename;
    m_downloadProgressValue = 0.0;
    m_downloadSpeed = 0.0;
    m_lastBytesReceived = 0;

    QString filePath = m_modelsDir + "/" + filename;
    m_downloadFile = new QFile(filePath + ".part", this);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        emit downloadError(filename, "Cannot create file: " + filePath);
        delete m_downloadFile;
        m_downloadFile = nullptr;
        return;
    }

    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "OharaGPT/1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);
    // Set timeout for download
    request.setTransferTimeout(0); // No timeout for large files

    m_currentDownload = m_networkManager->get(request);
    m_speedTimer.start();

    connect(m_currentDownload, &QNetworkReply::downloadProgress,
            this, &ModelManager::onDownloadProgress);
    connect(m_currentDownload, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadFile && m_currentDownload) {
            m_downloadFile->write(m_currentDownload->readAll());
        }
    });
    connect(m_currentDownload, &QNetworkReply::finished,
            this, &ModelManager::onDownloadFinished);
    connect(m_currentDownload, &QNetworkReply::errorOccurred,
            this, &ModelManager::onDownloadError);

    emit downloadStarted(filename);
    emit downloadingChanged();
}

void ModelManager::cancelDownload()
{
    if (m_currentDownload) {
        m_currentDownload->abort();
        m_currentDownload->deleteLater();
        m_currentDownload = nullptr;
    }
    if (m_downloadFile) {
        m_downloadFile->close();
        m_downloadFile->remove(); // Remove partial file
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    m_currentFilename.clear();
    m_pendingMmproj.clear();
    m_downloadProgressValue = 0.0;
    emit downloadingChanged();
    emit downloadProgressChanged();
}

void ModelManager::deleteModel(const QString &filename)
{
    QString path = m_modelsDir + "/" + filename;
    if (QFile::exists(path)) {
        QFile::remove(path);
        emit modelDeleted(filename);
    }
}

void ModelManager::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal > 0) {
        m_downloadProgressValue = static_cast<double>(bytesReceived) / bytesTotal;
    }

    // Calculate speed every 500ms
    qint64 elapsed = m_speedTimer.elapsed();
    if (elapsed >= 500) {
        qint64 bytesDelta = bytesReceived - m_lastBytesReceived;
        m_downloadSpeed = (bytesDelta / (elapsed / 1000.0)) / (1024.0 * 1024.0); // MB/s
        m_lastBytesReceived = bytesReceived;
        m_speedTimer.restart();
    }

    emit downloadProgressChanged();
}

void ModelManager::onDownloadFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    QString filename = m_currentFilename;

    if (reply->error() == QNetworkReply::NoError) {
        // Flush and close file
        if (m_downloadFile) {
            m_downloadFile->flush();
            m_downloadFile->close();

            // Rename .part to final filename
            QString partPath = m_downloadFile->fileName();
            QString finalPath = m_modelsDir + "/" + filename;
            QFile::remove(finalPath); // Remove old if exists
            QFile::rename(partPath, finalPath);

            delete m_downloadFile;
            m_downloadFile = nullptr;

            // Integrity verification
            QFile finalFile(finalPath);
            if (finalFile.open(QIODevice::ReadOnly)) {
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if (hash.addData(&finalFile)) {
                    qDebug() << "ModelManager: SHA-256 for" << filename << "is" << hash.result().toHex();
                } else {
                    qWarning() << "ModelManager: Failed to calculate SHA-256 for" << filename;
                }
                finalFile.close();
            }
        }

        reply->deleteLater();
        m_currentDownload = nullptr;

        // Check if we need to download mmproj
        if (!m_pendingMmproj.isEmpty()) {
            QString mmproj = m_pendingMmproj;
            QString repoId = m_pendingRepoId;
            m_pendingMmproj.clear();
            startSingleDownload(repoId, mmproj);
            return;
        }

        m_currentFilename.clear();
        m_downloadProgressValue = 0.0;
        emit downloadFinished(filename, m_modelsDir + "/" + filename);
    } else {
        if (m_downloadFile) {
            m_downloadFile->close();
            m_downloadFile->remove();
            delete m_downloadFile;
            m_downloadFile = nullptr;
        }
        m_currentFilename.clear();
        emit downloadError(filename, reply->errorString());
        reply->deleteLater();
        m_currentDownload = nullptr;
    }

    emit downloadingChanged();
    emit downloadProgressChanged();
}

void ModelManager::onDownloadError(QNetworkReply::NetworkError code)
{
    Q_UNUSED(code)
    // Error handling is done in onDownloadFinished
}

bool ModelManager::isDownloading() const
{
    return m_currentDownload != nullptr;
}

double ModelManager::downloadProgress() const
{
    return m_downloadProgressValue;
}

QString ModelManager::downloadingModel() const
{
    return m_currentFilename;
}

double ModelManager::downloadSpeedMBps() const
{
    return m_downloadSpeed;
}

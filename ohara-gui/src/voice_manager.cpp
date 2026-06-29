#include "voice_manager.h"
#include "settings_manager.h"
#include <QDebug>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QThread>
#include "whisper.h"
#include <vector>

// Helper to read wav PCM data
bool read_wav(const std::string & fname, std::vector<float>& pcmf32, std::vector<std::vector<float>>& pcmf32s, bool stereo) {
    // A highly simplified WAV reader for 16-bit PCM, 16000 Hz
    QFile file(QString::fromStdString(fname));
    if (!file.open(QIODevice::ReadOnly)) return false;

    // Skip WAV header (44 bytes for standard RIFF/WAV)
    file.seek(44);

    QByteArray data = file.readAll();
    const int16_t* pcm16 = reinterpret_cast<const int16_t*>(data.constData());
    int frames = data.size() / sizeof(int16_t);

    pcmf32.resize(frames);
    for (int i = 0; i < frames; i++) {
        pcmf32[i] = static_cast<float>(pcm16[i]) / 32768.0f;
    }
    return true;
}

VoiceManager::VoiceManager(QObject *parent) : QObject(parent) {
    m_audioInput = new QAudioInput(this);
    m_audioRecorder = new QMediaRecorder(this);

    // Whisper requires 16000Hz, 1 channel, 16-bit PCM.
    QMediaFormat format;
    format.setAudioCodec(QMediaFormat::AudioCodec::Wave);
    m_audioRecorder->setMediaFormat(format);
    m_audioRecorder->setAudioSampleRate(16000);
    m_audioRecorder->setAudioChannelCount(1);

    m_captureSession.setAudioInput(m_audioInput);
    m_captureSession.setRecorder(m_audioRecorder);

    m_lastAudioPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/ohara_voice.wav";

    connect(m_audioRecorder, &QMediaRecorder::recorderStateChanged, this, &VoiceManager::onRecorderStateChanged);
}

VoiceManager::~VoiceManager() {
    if (m_audioRecorder->recorderState() == QMediaRecorder::RecordingState) {
        m_audioRecorder->stop();
    }
}

void VoiceManager::startRecording() {
    QFile::remove(m_lastAudioPath);
    m_audioRecorder->setOutputLocation(QUrl::fromLocalFile(m_lastAudioPath));
    m_audioRecorder->record();
    m_isRecording = true;
    emit recordingStatusChanged();
    qDebug() << "VoiceManager: Started recording to" << m_lastAudioPath;
}

void VoiceManager::stopRecording() {
    m_audioRecorder->stop();
    m_isRecording = false;
    emit recordingStatusChanged();
    qDebug() << "VoiceManager: Stopped recording";
}

void VoiceManager::onRecorderStateChanged(QMediaRecorder::RecorderState state) {
    if (state == QMediaRecorder::StoppedState && !m_isRecording) {
        // Run processing in background to not block UI
        QThread *thread = QThread::create([this]() {
            this->processVoice(m_lastAudioPath);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
}

void VoiceManager::processVoice(const QString &audioPath) {
    QString path = audioPath.isEmpty() ? m_lastAudioPath : audioPath;
    qDebug() << "VoiceManager: Processing voice from" << path;

    SettingsManager settings;
    QString modelPath = settings.dataDirectory() + "/models/ggml-base.en.bin";

    if (!QFile::exists(modelPath)) {
        emit processingError("Failed to initialize whisper context: Missing model at " + modelPath);
        return;
    }

    struct whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context * ctx = whisper_init_from_file_with_params(modelPath.toStdString().c_str(), cparams);

    if (ctx != nullptr) {
        std::vector<float> pcmf32;
        std::vector<std::vector<float>> pcmf32s;

        if (!read_wav(path.toStdString(), pcmf32, pcmf32s, false)) {
            whisper_free(ctx);
            emit processingError("Failed to read audio file format");
            return;
        }

        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.print_progress   = false;
        wparams.print_special    = false;
        wparams.print_realtime   = false;
        wparams.print_timestamps = false;
        wparams.translate        = false;
        wparams.no_context       = true;
        wparams.single_segment   = true;
        wparams.max_tokens       = 0;
        wparams.max_len          = 0;
        wparams.split_on_word    = false;
        wparams.audio_ctx        = 0;
        wparams.language         = "en";

        if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
            whisper_free(ctx);
            emit processingError("Whisper processing failed");
            return;
        }

        const int n_segments = whisper_full_n_segments(ctx);
        QString transcription = "";
        for (int i = 0; i < n_segments; ++i) {
            const char * text = whisper_full_get_segment_text(ctx, i);
            transcription += QString::fromUtf8(text).trimmed() + " ";
        }

        whisper_free(ctx);
        emit voiceProcessed(transcription.trimmed());
    } else {
        emit processingError("Failed to initialize whisper context.");
    }
}

bool VoiceManager::isRecording() const {
    return m_isRecording;
}

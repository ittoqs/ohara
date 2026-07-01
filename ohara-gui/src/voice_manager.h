#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include <QObject>
#include <QString>
#include <QMediaCaptureSession>
#include <QAudioInput>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QUrl>

struct whisper_context;

class VoiceManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY recordingStatusChanged)

public:
    explicit VoiceManager(QObject *parent = nullptr);
    ~VoiceManager();

    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void processVoice(const QString &audioPath = "");

    bool isRecording() const;

signals:
    void recordingStatusChanged();
    void voiceProcessed(const QString &transcription);
    void processingError(const QString &errorMsg);

private slots:
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);

private:
    bool m_isRecording = false;
    QMediaCaptureSession m_captureSession;
    QAudioInput *m_audioInput = nullptr;
    QMediaRecorder *m_audioRecorder = nullptr;
    QString m_lastAudioPath;
};

#endif // VOICE_MANAGER_H

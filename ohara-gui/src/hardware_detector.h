#ifndef HARDWARE_DETECTOR_H
#define HARDWARE_DETECTOR_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QTimer>
#include <QRegularExpression>

class HardwareDetector : public QObject {
    Q_OBJECT
    Q_PROPERTY(long long totalRamBytes READ totalRamBytes NOTIFY hardwareDetected)
    Q_PROPERTY(long long availableRamBytes READ availableRamBytes NOTIFY hardwareDetected)
    Q_PROPERTY(int cpuCores READ cpuCores NOTIFY hardwareDetected)
    Q_PROPERTY(QString cpuModel READ cpuModel NOTIFY hardwareDetected)
    Q_PROPERTY(QString recommendedModel READ recommendedModel NOTIFY hardwareDetected)
    Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY hardwareDetected)
    Q_PROPERTY(QString gpuName READ gpuName NOTIFY hardwareDetected)
    Q_PROPERTY(long long diskFreeBytes READ diskFreeBytes NOTIFY hardwareDetected)
    Q_PROPERTY(int recommendedGpuLayers READ recommendedGpuLayers NOTIFY hardwareDetected)
    Q_PROPERTY(int recommendedContextSize READ recommendedContextSize NOTIFY hardwareDetected)

public:
    explicit HardwareDetector(QObject *parent = nullptr);

    long long totalRamBytes() const;
    long long availableRamBytes() const;
    int cpuCores() const;
    QString cpuModel() const;
    QString recommendedModel() const;
    bool gpuAvailable() const;
    QString gpuName() const;
    long long diskFreeBytes() const;
    int recommendedGpuLayers() const;
    int recommendedContextSize() const;

    Q_INVOKABLE void detect();
    Q_INVOKABLE void startMonitoring(int intervalMs = 5000);
    Q_INVOKABLE void stopMonitoring();

    Q_INVOKABLE QVariantMap getHardwareSummary() const;

signals:
    void hardwareDetected();

private:
    void detectCpu();
    void detectRam();
    void detectGpu();
    void detectDisk();

    long long m_totalRamBytes = 0;
    long long m_availableRamBytes = 0;
    int m_cpuCores = 0;
    QString m_cpuModel;
    bool m_gpuAvailable = false;
    QString m_gpuName;
    long long m_diskFreeBytes = 0;

    QTimer *m_monitorTimer = nullptr;
};

#endif // HARDWARE_DETECTOR_H

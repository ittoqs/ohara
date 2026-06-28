#include "hardware_detector.h"
#include <QThread>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QDebug>

#ifdef Q_OS_LINUX
#include <unistd.h>
#include <fstream>
#include <string>
#elif defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

HardwareDetector::HardwareDetector(QObject *parent) : QObject(parent)
{
    detect();
}

void HardwareDetector::detect()
{
    detectCpu();
    detectRam();
    detectGpu();
    detectDisk();
    emit hardwareDetected();
}

void HardwareDetector::startMonitoring(int intervalMs)
{
    if (!m_monitorTimer) {
        m_monitorTimer = new QTimer(this);
        connect(m_monitorTimer, &QTimer::timeout, this, [this]() {
            detectRam(); // Only update RAM (dynamic metric)
            emit hardwareDetected();
        });
    }
    m_monitorTimer->start(intervalMs);
}

void HardwareDetector::stopMonitoring()
{
    if (m_monitorTimer) {
        m_monitorTimer->stop();
    }
}

// ============== CPU Detection ==============

void HardwareDetector::detectCpu()
{
    m_cpuCores = QThread::idealThreadCount();

#ifdef Q_OS_LINUX
    QFile cpuinfo("/proc/cpuinfo");
    if (cpuinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&cpuinfo);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            if (line.startsWith("model name")) {
                int colonPos = line.indexOf(':');
                if (colonPos >= 0) {
                    m_cpuModel = line.mid(colonPos + 1).trimmed();
                    break;
                }
            }
        }
        cpuinfo.close();
    }
    if (m_cpuModel.isEmpty()) {
        m_cpuModel = QString("Linux CPU (%1 cores)").arg(m_cpuCores);
    }
#elif defined(Q_OS_WIN)
    // Read from registry or use WMI
    m_cpuModel = qgetenv("PROCESSOR_IDENTIFIER");
    if (m_cpuModel.isEmpty()) {
        m_cpuModel = QString("CPU (%1 cores)").arg(m_cpuCores);
    }
#elif defined(Q_OS_MAC)
    char buf[256];
    size_t bufLen = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &bufLen, nullptr, 0) == 0) {
        m_cpuModel = QString::fromUtf8(buf);
    } else {
        m_cpuModel = QString("Apple CPU (%1 cores)").arg(m_cpuCores);
    }
#else
    m_cpuModel = QString("CPU (%1 cores)").arg(m_cpuCores);
#endif
}

// ============== RAM Detection ==============

void HardwareDetector::detectRam()
{
#ifdef Q_OS_LINUX
    // Total RAM
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    m_totalRamBytes = static_cast<long long>(pages) * page_size;

    // Available RAM from /proc/meminfo
    QFile meminfo("/proc/meminfo");
    if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&meminfo);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            if (line.startsWith("MemAvailable:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    m_availableRamBytes = parts[1].toLongLong() * 1024; // Convert kB to bytes
                }
                break;
            }
        }
        meminfo.close();
    }
    if (m_availableRamBytes == 0) {
        m_availableRamBytes = m_totalRamBytes; // Fallback
    }
#elif defined(Q_OS_WIN)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    m_totalRamBytes = status.ullTotalPhys;
    m_availableRamBytes = status.ullAvailPhys;
#elif defined(Q_OS_MAC)
    int mib[2] = { CTL_HW, HW_MEMSIZE };
    int64_t physical_memory = 0;
    size_t length = sizeof(physical_memory);
    sysctl(mib, 2, &physical_memory, &length, NULL, 0);
    m_totalRamBytes = physical_memory;

    // macOS available memory via vm_statistics
    // Simplified: use ~70% of total as estimate for available
    m_availableRamBytes = m_totalRamBytes * 7 / 10;
#else
    m_totalRamBytes = 8LL * 1024 * 1024 * 1024; // Fallback 8GB
    m_availableRamBytes = m_totalRamBytes;
#endif
}

// ============== GPU Detection ==============

void HardwareDetector::detectGpu()
{
    m_gpuAvailable = false;
    m_gpuName = "None";

#ifdef Q_OS_LINUX
    // Check for NVIDIA GPU
    QProcess nvidia;
    nvidia.start("nvidia-smi", QStringList() << "--query-gpu=name" << "--format=csv,noheader,nounits");
    nvidia.waitForFinished(3000);
    if (nvidia.exitCode() == 0) {
        QString output = nvidia.readAllStandardOutput().trimmed();
        if (!output.isEmpty()) {
            m_gpuAvailable = true;
            m_gpuName = "NVIDIA " + output.split('\n').first().trimmed();
            return;
        }
    }

    // Check for AMD GPU via /sys
    QDir sysGpu("/sys/class/drm");
    if (sysGpu.exists()) {
        QStringList cards = sysGpu.entryList(QStringList() << "card*", QDir::Dirs);
        for (const QString &card : cards) {
            QFile vendorFile(sysGpu.filePath(card + "/device/vendor"));
            if (vendorFile.open(QIODevice::ReadOnly)) {
                QString vendor = vendorFile.readAll().trimmed();
                if (vendor == "0x1002") { // AMD
                    m_gpuAvailable = true;
                    // Try to get GPU name
                    QFile nameFile(sysGpu.filePath(card + "/device/product_name"));
                    if (nameFile.open(QIODevice::ReadOnly)) {
                        m_gpuName = "AMD " + nameFile.readAll().trimmed();
                    } else {
                        m_gpuName = "AMD GPU (ROCm)";
                    }
                    return;
                }
                vendorFile.close();
            }
        }
    }

    // Check for Vulkan support
    QProcess vulkan;
    vulkan.start("vulkaninfo", QStringList() << "--summary");
    vulkan.waitForFinished(3000);
    if (vulkan.exitCode() == 0) {
        QString output = vulkan.readAllStandardOutput();
        if (output.contains("deviceName")) {
            m_gpuAvailable = true;
            // Extract device name
            QRegularExpression rx("deviceName\\s*=\\s*(.+)");
            auto match = rx.match(output);
            if (match.hasMatch()) {
                m_gpuName = match.captured(1).trimmed();
            } else {
                m_gpuName = "Vulkan GPU";
            }
        }
    }
#elif defined(Q_OS_WIN)
    // Check for NVIDIA on Windows
    QProcess nvidia;
    nvidia.start("nvidia-smi", QStringList() << "--query-gpu=name" << "--format=csv,noheader");
    nvidia.waitForFinished(3000);
    if (nvidia.exitCode() == 0) {
        QString output = nvidia.readAllStandardOutput().trimmed();
        if (!output.isEmpty()) {
            m_gpuAvailable = true;
            m_gpuName = "NVIDIA " + output.split('\n').first().trimmed();
        }
    }
#elif defined(Q_OS_MAC)
    // macOS: Check for Metal support (Apple Silicon or discrete GPU)
    QProcess sysProfiler;
    sysProfiler.start("system_profiler", QStringList() << "SPDisplaysDataType" << "-detailLevel" << "mini");
    sysProfiler.waitForFinished(5000);
    if (sysProfiler.exitCode() == 0) {
        QString output = sysProfiler.readAllStandardOutput();
        if (output.contains("Apple") || output.contains("Metal")) {
            m_gpuAvailable = true;
            QRegularExpression rx("Chipset Model:\\s*(.+)");
            auto match = rx.match(output);
            if (match.hasMatch()) {
                m_gpuName = match.captured(1).trimmed() + " (Metal)";
            } else {
                m_gpuName = "Apple GPU (Metal)";
            }
        }
    }
#endif
}

// ============== Disk Detection ==============

void HardwareDetector::detectDisk()
{
    // Check disk space at the data directory location
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataPath.isEmpty()) {
        dataPath = QDir::homePath();
    }

    QStorageInfo storage(dataPath);
    m_diskFreeBytes = storage.bytesAvailable();
}

// ============== Getters ==============

long long HardwareDetector::totalRamBytes() const { return m_totalRamBytes; }
long long HardwareDetector::availableRamBytes() const { return m_availableRamBytes; }
int HardwareDetector::cpuCores() const { return m_cpuCores; }
QString HardwareDetector::cpuModel() const { return m_cpuModel; }
bool HardwareDetector::gpuAvailable() const { return m_gpuAvailable; }
QString HardwareDetector::gpuName() const { return m_gpuName; }
long long HardwareDetector::diskFreeBytes() const { return m_diskFreeBytes; }

QString HardwareDetector::recommendedModel() const
{
    double ramGB = m_totalRamBytes / (1024.0 * 1024.0 * 1024.0);
    if (ramGB >= 16.0) {
        return "Qwen-2.5-Coder-14B (Optimal 16GB+)";
    } else if (ramGB >= 8.0) {
        return "Llama-3-8B-Instruct (Optimal 8GB+)";
    } else if (ramGB >= 4.0) {
        return "Phi-3-Mini (Optimal 4GB+)";
    } else {
        return "TinyLlama-1.1B (Minimal 2GB)";
    }
}

int HardwareDetector::recommendedGpuLayers() const
{
    if (!m_gpuAvailable) return 0;
    // If GPU is available, offload all layers
    return -1; // -1 means auto/all layers
}

int HardwareDetector::recommendedContextSize() const
{
    double ramGB = m_totalRamBytes / (1024.0 * 1024.0 * 1024.0);
    if (ramGB >= 32.0) return 16384;
    if (ramGB >= 16.0) return 8192;
    if (ramGB >= 8.0) return 4096;
    return 2048;
}

QVariantMap HardwareDetector::getHardwareSummary() const
{
    QVariantMap summary;
    summary["totalRamGB"] = m_totalRamBytes / (1024.0 * 1024.0 * 1024.0);
    summary["availableRamGB"] = m_availableRamBytes / (1024.0 * 1024.0 * 1024.0);
    summary["cpuCores"] = m_cpuCores;
    summary["cpuModel"] = m_cpuModel;
    summary["gpuAvailable"] = m_gpuAvailable;
    summary["gpuName"] = m_gpuName;
    summary["diskFreeGB"] = m_diskFreeBytes / (1024.0 * 1024.0 * 1024.0);
    summary["recommendedModel"] = recommendedModel();
    summary["recommendedGpuLayers"] = recommendedGpuLayers();
    summary["recommendedContextSize"] = recommendedContextSize();
    return summary;
}

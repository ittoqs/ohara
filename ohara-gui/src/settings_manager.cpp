#include "settings_manager.h"
#include <QCoreApplication>

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    ensureDirectories();

    QString configPath = dataDirectory() + "/config.ini";
    m_settings = new QSettings(configPath, QSettings::IniFormat, this);

    initTranslations();
}

void SettingsManager::ensureDirectories()
{
    QDir dir;
    dir.mkpath(dataDirectory());
    dir.mkpath(modelsDirectory());
}

QString SettingsManager::dataDirectory() const
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (path.isEmpty()) {
        path = QDir::homePath() + "/.ohara-gpt";
    }
    return path;
}

QString SettingsManager::modelsDirectory() const
{
    return dataDirectory() + "/models";
}

QString SettingsManager::databasePath() const
{
    return dataDirectory() + "/ohara.db";
}

// --- Language ---
QString SettingsManager::language() const
{
    return m_settings->value("language", "id").toString();
}

void SettingsManager::setLanguage(const QString &lang)
{
    if (language() != lang) {
        m_settings->setValue("language", lang);
        emit languageChanged();
    }
}

QString SettingsManager::tr(const QString &key) const
{
    if (language() == "id") {
        return m_translationsId.value(key, key).toString();
    }
    return m_translationsEn.value(key, key).toString();
}

// --- Theme ---
QString SettingsManager::theme() const
{
    return m_settings->value("theme", "dark").toString();
}

void SettingsManager::setTheme(const QString &theme)
{
    if (this->theme() != theme) {
        m_settings->setValue("theme", theme);
        emit themeChanged();
    }
}

// --- Font ---
int SettingsManager::fontSize() const
{
    return m_settings->value("fontSize", 14).toInt();
}

void SettingsManager::setFontSize(int size)
{
    size = qBound(10, size, 24);
    if (fontSize() != size) {
        m_settings->setValue("fontSize", size);
        emit fontSizeChanged();
    }
}

// --- System Prompt ---
QString SettingsManager::defaultSystemPrompt() const
{
    return m_settings->value("defaultSystemPrompt",
        "Kamu adalah Ohara, asisten AI yang cerdas, ramah, dan membantu. "
        "Jawab dengan jelas dan terstruktur.").toString();
}

void SettingsManager::setDefaultSystemPrompt(const QString &prompt)
{
    if (defaultSystemPrompt() != prompt) {
        m_settings->setValue("defaultSystemPrompt", prompt);
        emit defaultSystemPromptChanged();
    }
}

// --- First Run ---
bool SettingsManager::isFirstRun() const
{
    return m_settings->value("firstRunCompleted", false).toBool() == false;
}

void SettingsManager::completeFirstRun()
{
    m_settings->setValue("firstRunCompleted", true);
    emit firstRunChanged();
}

// --- Inference Settings ---
int SettingsManager::maxContextTokens() const
{
    return m_settings->value("maxContextTokens", 4096).toInt();
}

void SettingsManager::setMaxContextTokens(int tokens)
{
    tokens = qBound(512, tokens, 32768);
    if (maxContextTokens() != tokens) {
        m_settings->setValue("maxContextTokens", tokens);
        emit maxContextTokensChanged();
    }
}

int SettingsManager::maxResponseTokens() const
{
    return m_settings->value("maxResponseTokens", 2048).toInt();
}

void SettingsManager::setMaxResponseTokens(int tokens)
{
    tokens = qBound(64, tokens, 16384);
    if (maxResponseTokens() != tokens) {
        m_settings->setValue("maxResponseTokens", tokens);
        emit maxResponseTokensChanged();
    }
}

double SettingsManager::temperature() const
{
    return m_settings->value("temperature", 0.7).toDouble();
}

void SettingsManager::setTemperature(double temp)
{
    temp = qBound(0.0, temp, 2.0);
    if (qAbs(temperature() - temp) > 0.001) {
        m_settings->setValue("temperature", temp);
        emit temperatureChanged();
    }
}

double SettingsManager::topP() const
{
    return m_settings->value("topP", 0.9).toDouble();
}

void SettingsManager::setTopP(double p)
{
    p = qBound(0.0, p, 1.0);
    if (qAbs(topP() - p) > 0.001) {
        m_settings->setValue("topP", p);
        emit topPChanged();
    }
}

int SettingsManager::chatHistoryLimit() const
{
    return m_settings->value("chatHistoryLimit", 20).toInt();
}

void SettingsManager::setChatHistoryLimit(int limit)
{
    limit = qBound(4, limit, 100);
    if (chatHistoryLimit() != limit) {
        m_settings->setValue("chatHistoryLimit", limit);
        emit chatHistoryLimitChanged();
    }
}

// --- Personality Presets ---
QVariantList SettingsManager::getPersonalityPresets() const
{
    QVariantList presets;
    bool isId = (language() == "id");

    QVariantMap general;
    general["id"] = "general";
    general["name"] = isId ? "Asisten Umum" : "General Assistant";
    general["icon"] = "💬";
    general["prompt"] = isId
        ? "Kamu adalah asisten AI yang cerdas dan ramah. Jawab pertanyaan dengan jelas, ringkas, dan membantu."
        : "You are a smart and friendly AI assistant. Answer questions clearly, concisely, and helpfully.";

    QVariantMap coder;
    coder["id"] = "coder";
    coder["name"] = isId ? "Programmer" : "Programmer";
    coder["icon"] = "💻";
    coder["prompt"] = isId
        ? "Kamu adalah programmer ahli. Berikan kode yang bersih, efisien, dan terdokumentasi. Jelaskan logika secara singkat."
        : "You are an expert programmer. Provide clean, efficient, well-documented code. Explain logic briefly.";

    QVariantMap writer;
    writer["id"] = "writer";
    writer["name"] = isId ? "Penulis" : "Writer";
    writer["icon"] = "✍️";
    writer["prompt"] = isId
        ? "Kamu adalah penulis profesional. Bantu menulis, mengedit, dan menyempurnakan teks dengan gaya yang menarik dan tepat."
        : "You are a professional writer. Help write, edit, and polish text with engaging and precise style.";

    QVariantMap translator;
    translator["id"] = "translator";
    translator["name"] = isId ? "Penerjemah" : "Translator";
    translator["icon"] = "🌐";
    translator["prompt"] = isId
        ? "Kamu adalah penerjemah profesional. Terjemahkan teks dengan akurat sambil menjaga nuansa dan konteks aslinya."
        : "You are a professional translator. Translate text accurately while preserving the original nuance and context.";

    QVariantMap analyst;
    analyst["id"] = "analyst";
    analyst["name"] = isId ? "Analis Data" : "Data Analyst";
    analyst["icon"] = "📊";
    analyst["prompt"] = isId
        ? "Kamu adalah analis data senior. Bantu menganalisis data, membuat insight, dan menyajikan temuan secara terstruktur."
        : "You are a senior data analyst. Help analyze data, generate insights, and present findings in a structured way.";

    QVariantMap creative;
    creative["id"] = "creative";
    creative["name"] = isId ? "Kreatif" : "Creative";
    creative["icon"] = "🎨";
    creative["prompt"] = isId
        ? "Kamu adalah AI kreatif yang imajinatif. Bantu brainstorming, generate ide, dan berpikir di luar kotak."
        : "You are an imaginative creative AI. Help brainstorm, generate ideas, and think outside the box.";

    presets << general << coder << writer << translator << analyst << creative;
    return presets;
}

// --- Translation Strings ---
void SettingsManager::initTranslations()
{
    // Indonesian
    m_translationsId["app_name"] = "Ohara GPT";
    m_translationsId["welcome_title"] = "Selamat Datang di Ohara GPT";
    m_translationsId["welcome_subtitle"] = "AI Desktop Pribadi Anda — Sepenuhnya Offline & Privat";
    m_translationsId["next"] = "Lanjut";
    m_translationsId["back"] = "Kembali";
    m_translationsId["finish"] = "Mulai";
    m_translationsId["skip"] = "Lewati";
    m_translationsId["new_chat"] = "Obrolan Baru";
    m_translationsId["chat_history"] = "Riwayat Obrolan";
    m_translationsId["models"] = "Model AI";
    m_translationsId["settings"] = "Pengaturan";
    m_translationsId["select_model"] = "Pilih Model";
    m_translationsId["download"] = "Unduh";
    m_translationsId["downloading"] = "Mengunduh...";
    m_translationsId["downloaded"] = "Terunduh";
    m_translationsId["delete"] = "Hapus";
    m_translationsId["rename"] = "Ubah Nama";
    m_translationsId["cancel"] = "Batal";
    m_translationsId["confirm"] = "Konfirmasi";
    m_translationsId["send"] = "Kirim";
    m_translationsId["type_message"] = "Ketik pesan...";
    m_translationsId["create_session_first"] = "Buat sesi obrolan terlebih dahulu";
    m_translationsId["select_model_first"] = "Pilih model AI terlebih dahulu";
    m_translationsId["thinking"] = "Ohara sedang berpikir...";
    m_translationsId["hardware_detected"] = "Perangkat Terdeteksi";
    m_translationsId["ram"] = "RAM";
    m_translationsId["cpu"] = "CPU";
    m_translationsId["gpu"] = "GPU";
    m_translationsId["cores"] = "Inti";
    m_translationsId["recommended"] = "Direkomendasikan";
    m_translationsId["language"] = "Bahasa";
    m_translationsId["theme"] = "Tema";
    m_translationsId["font_size"] = "Ukuran Font";
    m_translationsId["system_prompt"] = "System Prompt";
    m_translationsId["personality"] = "Kepribadian";
    m_translationsId["temperature_label"] = "Temperatur";
    m_translationsId["max_tokens"] = "Maks Token";
    m_translationsId["context_window"] = "Jendela Konteks";
    m_translationsId["scanning_hardware"] = "Memindai Perangkat Keras...";
    m_translationsId["model_recommendation"] = "Rekomendasi Model";
    m_translationsId["ready"] = "Siap Digunakan!";
    m_translationsId["ready_subtitle"] = "Ohara GPT siap membantu Anda. Mulai percakapan pertama Anda!";
    m_translationsId["upload_document"] = "Unggah Dokumen";
    m_translationsId["attach_image"] = "Lampirkan Gambar";
    m_translationsId["copy"] = "Salin";
    m_translationsId["regenerate"] = "Regenerasi";
    m_translationsId["search"] = "Cari";
    m_translationsId["no_sessions"] = "Belum ada obrolan";
    m_translationsId["session_deleted"] = "Sesi dihapus";
    m_translationsId["model_loaded"] = "Model dimuat";
    m_translationsId["model_unloaded"] = "Model dilepas";
    m_translationsId["download_complete"] = "Unduhan selesai";
    m_translationsId["download_failed"] = "Unduhan gagal";
    m_translationsId["error"] = "Kesalahan";
    m_translationsId["connected"] = "Terhubung";
    m_translationsId["disconnected"] = "Terputus";
    m_translationsId["tokens_per_sec"] = "token/detik";
    m_translationsId["export_data"] = "Ekspor Data";
    m_translationsId["clear_all_data"] = "Hapus Semua Data";
    m_translationsId["confirm_clear"] = "Yakin ingin menghapus semua data?";
    m_translationsId["about"] = "Tentang";
    m_translationsId["version"] = "Versi";
    m_translationsId["loading_model"] = "Memuat model...";
    m_translationsId["stop"] = "Berhenti";
    m_translationsId["dark"] = "Gelap";
    m_translationsId["light"] = "Terang";
    m_translationsId["general_assistant"] = "Asisten Umum";
    m_translationsId["not_downloaded"] = "Belum Diunduh";
    m_translationsId["free_space"] = "Ruang Kosong";
    m_translationsId["min_ram"] = "RAM Minimal";
    m_translationsId["session"] = "Sesi";
    m_translationsId["delete_model_confirm"] = "Yakin ingin menghapus model ini?";
    m_translationsId["delete_session_confirm"] = "Yakin ingin menghapus sesi ini?";

    // English
    m_translationsEn["app_name"] = "Ohara GPT";
    m_translationsEn["welcome_title"] = "Welcome to Ohara GPT";
    m_translationsEn["welcome_subtitle"] = "Your Personal AI Desktop — Fully Offline & Private";
    m_translationsEn["next"] = "Next";
    m_translationsEn["back"] = "Back";
    m_translationsEn["finish"] = "Get Started";
    m_translationsEn["skip"] = "Skip";
    m_translationsEn["new_chat"] = "New Chat";
    m_translationsEn["chat_history"] = "Chat History";
    m_translationsEn["models"] = "AI Models";
    m_translationsEn["settings"] = "Settings";
    m_translationsEn["select_model"] = "Select Model";
    m_translationsEn["download"] = "Download";
    m_translationsEn["downloading"] = "Downloading...";
    m_translationsEn["downloaded"] = "Downloaded";
    m_translationsEn["delete"] = "Delete";
    m_translationsEn["rename"] = "Rename";
    m_translationsEn["cancel"] = "Cancel";
    m_translationsEn["confirm"] = "Confirm";
    m_translationsEn["send"] = "Send";
    m_translationsEn["type_message"] = "Type a message...";
    m_translationsEn["create_session_first"] = "Create a chat session first";
    m_translationsEn["select_model_first"] = "Select an AI model first";
    m_translationsEn["thinking"] = "Ohara is thinking...";
    m_translationsEn["hardware_detected"] = "Hardware Detected";
    m_translationsEn["ram"] = "RAM";
    m_translationsEn["cpu"] = "CPU";
    m_translationsEn["gpu"] = "GPU";
    m_translationsEn["cores"] = "Cores";
    m_translationsEn["recommended"] = "Recommended";
    m_translationsEn["language"] = "Language";
    m_translationsEn["theme"] = "Theme";
    m_translationsEn["font_size"] = "Font Size";
    m_translationsEn["system_prompt"] = "System Prompt";
    m_translationsEn["personality"] = "Personality";
    m_translationsEn["temperature_label"] = "Temperature";
    m_translationsEn["max_tokens"] = "Max Tokens";
    m_translationsEn["context_window"] = "Context Window";
    m_translationsEn["scanning_hardware"] = "Scanning Hardware...";
    m_translationsEn["model_recommendation"] = "Model Recommendation";
    m_translationsEn["ready"] = "Ready to Go!";
    m_translationsEn["ready_subtitle"] = "Ohara GPT is ready to assist you. Start your first conversation!";
    m_translationsEn["upload_document"] = "Upload Document";
    m_translationsEn["attach_image"] = "Attach Image";
    m_translationsEn["copy"] = "Copy";
    m_translationsEn["regenerate"] = "Regenerate";
    m_translationsEn["search"] = "Search";
    m_translationsEn["no_sessions"] = "No chats yet";
    m_translationsEn["session_deleted"] = "Session deleted";
    m_translationsEn["model_loaded"] = "Model loaded";
    m_translationsEn["model_unloaded"] = "Model unloaded";
    m_translationsEn["download_complete"] = "Download complete";
    m_translationsEn["download_failed"] = "Download failed";
    m_translationsEn["error"] = "Error";
    m_translationsEn["connected"] = "Connected";
    m_translationsEn["disconnected"] = "Disconnected";
    m_translationsEn["tokens_per_sec"] = "tokens/sec";
    m_translationsEn["export_data"] = "Export Data";
    m_translationsEn["clear_all_data"] = "Clear All Data";
    m_translationsEn["confirm_clear"] = "Are you sure you want to clear all data?";
    m_translationsEn["about"] = "About";
    m_translationsEn["version"] = "Version";
    m_translationsEn["loading_model"] = "Loading model...";
    m_translationsEn["stop"] = "Stop";
    m_translationsEn["dark"] = "Dark";
    m_translationsEn["light"] = "Light";
    m_translationsEn["general_assistant"] = "General Assistant";
    m_translationsEn["not_downloaded"] = "Not Downloaded";
    m_translationsEn["free_space"] = "Free Space";
    m_translationsEn["min_ram"] = "Min RAM";
    m_translationsEn["session"] = "Session";
    m_translationsEn["delete_model_confirm"] = "Are you sure you want to delete this model?";
    m_translationsEn["delete_session_confirm"] = "Are you sure you want to delete this session?";
}

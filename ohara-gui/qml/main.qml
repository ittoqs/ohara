import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQuick.Dialogs

Window {
    id: root
    width: 1280
    height: 860
    minimumWidth: 800
    minimumHeight: 600
    visible: true
    title: "Ohara GPT"
    color: theme.bgMain

    // ================ STATE ================
    property double ramGB: HardwareDetector.totalRamBytes / (1024.0 * 1024.0 * 1024.0)
    property double availRamGB: HardwareDetector.availableRamBytes / (1024.0 * 1024.0 * 1024.0)
    property double diskGB: HardwareDetector.diskFreeBytes / (1024.0 * 1024.0 * 1024.0)

    property string activeModelName: ""
    property string activeFilename: ""
    property bool isThinking: false
    property int currentSessionId: 0
    property string currentResponseBuffer: ""
    property string selectedImagePath: ""
    property string selectedImageBase64: ""
    property string currentSystemPrompt: Settings.defaultSystemPrompt

    // ================ THEME ================
    property bool isDark: Settings.theme === "dark" || Settings.theme === "" // default to dark
    QtObject {
        id: theme
        property color bgMain: isDark ? "#06080f" : "#f8fafc"
        property color bgPanel: isDark ? "#0a0e1a" : "#ffffff"
        property color bgPanelElevated: isDark ? "#0f1629" : "#f1f5f9"
        property color bgItem: isDark ? "#111827" : "#e2e8f0"
        property color bgItemHover: isDark ? "#1e293b" : "#cbd5e1"
        property color bgActive: isDark ? "#1e3a5f" : "#bfdbfe"

        property color border: isDark ? "#1e293b" : "#cbd5e1"
        property color borderFocus: isDark ? "#3b82f6" : "#2563eb"

        property color textMain: isDark ? "#f1f5f9" : "#0f172a"
        property color textSecondary: isDark ? "#94a3b8" : "#475569"
        property color textMuted: isDark ? "#64748b" : "#94a3b8"
        property color textAccent: isDark ? "#60a5fa" : "#3b82f6"

        property color primary: isDark ? "#3b82f6" : "#2563eb"
        property color primaryHover: isDark ? "#2563eb" : "#1d4ed8"

        property color success: isDark ? "#10b981" : "#059669"
        property color successBg: isDark ? "#065f46" : "#d1fae5"
        property color warning: isDark ? "#f59e0b" : "#d97706"
        property color warningBg: isDark ? "#92400e" : "#fef3c7"
        property color danger: isDark ? "#ef4444" : "#dc2626"
        property color dangerBg: isDark ? "#7f1d1d" : "#fee2e2"
        property color dangerHover: isDark ? "#991b1b" : "#b91c1c"
    }
    property string currentPersonality: "general"

    property bool sidebarVisible: root.width >= 900
    property int sidebarWidth: sidebarVisible ? 300 : 0
    property int onboardingStep: 0

    // Convenience i18n
    function t(key) { return Settings.tr(key); }

    // ================ FONTS ================
    FontLoader { id: interFont; source: "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700" }

    // ================ MODELS ================
    ListModel { id: chatModel }
    ListModel { id: sessionsModel }

    // ================ STARTUP ================
    Component.onCompleted: {
        if (Settings.firstRun) {
            stackView.push(onboardingComponent);
        } else {
            loadSessions();
        }
    }

    function loadSessions() {
        var sessions = Database.getSessions();
        sessionsModel.clear();
        for (var i = 0; i < sessions.length; i++) {
            sessionsModel.append(sessions[i]);
        }
        if (sessionsModel.count > 0 && currentSessionId === 0) {
            switchToSession(sessionsModel.get(0).id);
        }
    }

    function switchToSession(sessionId) {
        currentSessionId = sessionId;
        var session = Database.getSession(sessionId);
        currentSystemPrompt = session.system_prompt || Settings.defaultSystemPrompt;
        currentPersonality = session.personality || "general";

        var msgs = Database.getMessages(sessionId, 200);
        chatModel.clear();
        for (var i = 0; i < msgs.length; i++) {
            chatModel.append({
                "sender": msgs[i].sender,
                "text": msgs[i].text,
                "imageBase64": msgs[i].image_base64 || "",
                "msgId": msgs[i].id
            });
        }
        chatList.positionViewAtEnd();
    }

    function createNewSession() {
        var num = sessionsModel.count + 1;
        var title = t("session") + " " + num;
        var id = Database.createSession(title, activeModelName, currentSystemPrompt);
        if (id > 0) {
            loadSessions();
            switchToSession(id);
            chatModel.clear();
        }
    }

    function sendMessage() {
        var msg = inputField.text.trim();
        if (msg === "" || !InferenceEngine.modelLoaded || currentSessionId === 0) return;

        chatModel.append({"sender": "User", "text": msg, "imageBase64": selectedImageBase64, "msgId": -1});
        Database.addMessage(currentSessionId, "User", msg, selectedImageBase64);
        inputField.text = "";
        selectedImagePath = "";
        selectedImageBase64 = "";

        chatList.positionViewAtEnd();
        isThinking = true;
        currentResponseBuffer = "";

        // Build messages for inference
        var messages = [];
        if (currentSystemPrompt) {
            var sysPrompt = currentSystemPrompt;
            // Append RAG context if documents indexed
            var context = DocProcessor.searchContext(msg, 2);
            if (context.length > 0) {
                sysPrompt += "\n\nRelevant context:\n";
                for (var c = 0; c < context.length; c++) {
                    sysPrompt += context[c].content + "\n";
                }
            }
            messages.push({"role": "system", "content": sysPrompt});
        }

        // Add recent history
        var history = Database.getRecentMessages(currentSessionId, Settings.chatHistoryLimit);
        for (var i = 0; i < history.length; i++) {
            var h = history[i];
            // Skip the message we just added
            if (h.sender === "User" && h.text === msg && i === history.length - 1) continue;
            messages.push({
                "role": h.sender === "User" ? "user" : "assistant",
                "content": h.text
            });
        }
        messages.push({"role": "user", "content": msg});

        InferenceEngine.generate(messages, Settings.maxResponseTokens,
                                  Settings.temperature, Settings.topP);
    }

    // ================ SIGNAL HANDLERS ================
    Connections {

        target: VoiceManager
        function onVoiceProcessed(transcription) {
            inputField.text = transcription;
        }
    }
    Connections {
        target: InferenceEngine

        function onTokenGenerated(token) {
            isThinking = false;
            currentResponseBuffer += token;
            if (chatModel.count > 0 && chatModel.get(chatModel.count - 1).sender === "Ohara") {
                chatModel.setProperty(chatModel.count - 1, "text", currentResponseBuffer);
            } else {
                chatModel.append({"sender": "Ohara", "text": currentResponseBuffer, "imageBase64": "", "msgId": -1});
            }
            chatList.positionViewAtEnd();
        }

        function onGenerationFinished(fullResponse) {
            isThinking = false;
            Database.addMessage(currentSessionId, "Ohara", fullResponse, "");
            currentResponseBuffer = "";
        }

        function onGenerationError(error) {
            isThinking = false;
            currentResponseBuffer = "";
            chatModel.append({"sender": "Ohara", "text": "❌ " + error, "imageBase64": "", "msgId": -1});
            chatList.positionViewAtEnd();
        }

        function onModelLoadStarted(modelName) {
            chatModel.append({"sender": "Ohara", "text": "⏳ " + t("loading_model") + " " + modelName + "...", "imageBase64": "", "msgId": -1});
            chatList.positionViewAtEnd();
        }

        function onModelLoadFinished(success, error) {
            if (success) {
                chatModel.append({"sender": "Ohara", "text": "✅ " + t("model_loaded") + ": **" + activeModelName + "**", "imageBase64": "", "msgId": -1});
            } else {
                chatModel.append({"sender": "Ohara", "text": "❌ " + error, "imageBase64": "", "msgId": -1});
                activeModelName = "";
                activeFilename = "";
            }
            chatList.positionViewAtEnd();
        }
    }

    Connections {
        target: ModelManager

        function onDownloadStarted(filename) {
            chatModel.append({"sender": "Ohara", "text": "📥 " + t("downloading") + " `" + filename + "`", "imageBase64": "", "msgId": -1});
            chatList.positionViewAtEnd();
        }

        function onDownloadFinished(filename, path) {
            chatModel.append({"sender": "Ohara", "text": "✅ " + t("download_complete") + ": `" + filename + "`", "imageBase64": "", "msgId": -1});
            chatList.positionViewAtEnd();
        }

        function onDownloadError(filename, error) {
            chatModel.append({"sender": "Ohara", "text": "❌ " + t("download_failed") + ": " + error, "imageBase64": "", "msgId": -1});
            chatList.positionViewAtEnd();
        }
    }

    // ================ FILE DIALOG ================
    FileDialog {
        id: fileDialog
        title: t("upload_document")
        nameFilters: ["All Supported (*.pdf *.txt *.md *.csv *.json)", "Documents (*.pdf *.txt *.md)", "Images (*.png *.jpg *.jpeg)"]
        onAccepted: {
            var urlStr = fileDialog.fileUrl.toString();
            var isImage = urlStr.endsWith(".png") || urlStr.endsWith(".jpg") || urlStr.endsWith(".jpeg");
            if (isImage) {
                selectedImagePath = urlStr;
                // Basic image to base64 via file reading
            } else {
                chatModel.append({"sender": "Ohara", "text": "📄 " + t("upload_document") + "...", "imageBase64": "", "msgId": -1});
                chatList.positionViewAtEnd();
                DocProcessor.processFile(urlStr);
            }
        }
    }

    Connections {
        target: DocProcessor
        function onDocumentProcessed(filename, chunkCount) {
            chatModel.append({"sender": "Ohara", "text": "✅ **" + filename + "** — " + chunkCount + " chunks indexed", "imageBase64": "", "msgId": -1});
            chatList.positionViewAtEnd();
        }
        function onProcessingError(filename, error) {
            chatModel.append({"sender": "Ohara", "text": "❌ " + filename + ": " + error, "imageBase64": "", "msgId": -1});
            chatList.positionViewAtEnd();
        }
    }

    // ================ MAIN LAYOUT ================
    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: mainPageComponent
    }

    // ================================================================
    //  ONBOARDING WIZARD
    // ================================================================
    Component {
        id: onboardingComponent

        Rectangle {
            color: theme.bgMain

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.85, 560)
                spacing: 32

                // Step indicators
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12
                    Repeater {
                        model: 4
                        Rectangle {
                            width: onboardingStep >= index ? 32 : 10
                            height: 10
                            radius: 5
                            color: onboardingStep >= index ? theme.primary : theme.bgItemHover
                            Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutQuad } }
                            Behavior on color { ColorAnimation { duration: 250 } }
                        }
                    }
                }

                // Step content
                Loader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 400
                    sourceComponent: {
                        switch (onboardingStep) {
                            case 0: return welcomeStep;
                            case 1: return hardwareScanStep;
                            case 2: return modelSelectStep;
                            case 3: return readyStep;
                            default: return welcomeStep;
                        }
                    }
                }

                // Navigation
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: t("skip")
                        color: theme.textMuted
                        font.pixelSize: 14
                        visible: onboardingStep < 3
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Settings.completeFirstRun();
                                stackView.pop();
                                loadSessions();
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        width: 120
                        height: 44
                        radius: 22
                        color: theme.primary
                        visible: onboardingStep > 0

                        Text {
                            text: t("back")
                            color: "white"
                            font.pixelSize: 14
                            font.bold: true
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: onboardingStep--
                        }

                        Behavior on opacity { NumberAnimation { duration: 200 } }
                    }

                    Rectangle {
                        width: 140
                        height: 44
                        radius: 22
                        gradient: Gradient {
                            GradientStop { position: 0; color: theme.primary }
                            GradientStop { position: 1; color: theme.primaryHover }
                        }

                        Text {
                            text: onboardingStep < 3 ? t("next") : t("finish")
                            color: "white"
                            font.pixelSize: 14
                            font.bold: true
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (onboardingStep < 3) {
                                    onboardingStep++;
                                } else {
                                    Settings.completeFirstRun();
                                    stackView.pop();
                                    loadSessions();
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Onboarding Steps
    Component {
        id: welcomeStep
        ColumnLayout {
            spacing: 20
            anchors.centerIn: parent

            Text {
                text: "🌊"
                font.pixelSize: 72
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: t("welcome_title")
                color: theme.textMain
                font.pixelSize: 28
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: t("welcome_subtitle")
                color: theme.textSecondary
                font.pixelSize: 16
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 400
            }
        }
    }

    Component {
        id: hardwareScanStep
        ColumnLayout {
            spacing: 16
            anchors.centerIn: parent

            Text {
                text: "🔍 " + t("hardware_detected")
                color: theme.textMain
                font.pixelSize: 22
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            // Hardware cards
            Repeater {
                model: [
                    { icon: "🧠", label: t("ram"), value: ramGB.toFixed(1) + " GB" },
                    { icon: "⚡", label: t("cpu"), value: HardwareDetector.cpuCores + " " + t("cores") },
                    { icon: "🎮", label: t("gpu"), value: HardwareDetector.gpuName },
                    { icon: "💾", label: t("free_space"), value: diskGB.toFixed(1) + " GB" }
                ]

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    radius: 12
                    color: theme.bgPanelElevated
                    border.color: theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        Text { text: modelData.icon; font.pixelSize: 20 }
                        Text { text: modelData.label; color: theme.textSecondary; font.pixelSize: 14; Layout.fillWidth: true }
                        Text { text: modelData.value; color: theme.textMain; font.pixelSize: 14; font.bold: true }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 48
                radius: 12
                color: theme.bgPanel
                border.color: theme.success

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    Text { text: "✨"; font.pixelSize: 18 }
                    Text { text: t("recommended") + ": " + HardwareDetector.recommendedModel; color: theme.success; font.pixelSize: 13; font.bold: true }
                }
            }
        }
    }

    Component {
        id: modelSelectStep
        ColumnLayout {
            spacing: 12
            anchors.fill: parent

            Text {
                text: "📦 " + t("select_model")
                color: theme.textMain
                font.pixelSize: 22
                font.bold: true
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: ModelManager.getModelCatalog(ramGB)

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 72
                    radius: 12
                    color: modelData.compatible ? theme.bgPanelElevated : theme.bgPanel
                    border.color: modelData.downloaded ? theme.success : (modelData.compatible ? theme.bgItemHover : theme.bgItem)
                    opacity: modelData.compatible ? 1.0 : 0.4

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: modelData.name; color: theme.textMain; font.pixelSize: 14; font.bold: true }
                            Text {
                                text: modelData.type + " · " + t("min_ram") + ": " + modelData.minRamGB + "GB"
                                color: theme.textMuted; font.pixelSize: 11
                            }
                        }

                        Rectangle {
                            width: 80; height: 32; radius: 16
                            color: modelData.downloaded ? theme.successBg : theme.primary
                            visible: modelData.compatible

                            Text {
                                text: modelData.downloaded ? "✓" : t("download")
                                color: "white"
                                font.pixelSize: 12
                                font.bold: true
                                anchors.centerIn: parent
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: !modelData.downloaded && !ModelManager.downloading
                                onClicked: {
                                    var mmproj = modelData.mmprojFilename || "";
                                    ModelManager.downloadModel(modelData.repoId, modelData.filename, mmproj);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: readyStep
        ColumnLayout {
            spacing: 20
            anchors.centerIn: parent

            Text { text: "🚀"; font.pixelSize: 72; Layout.alignment: Qt.AlignHCenter }
            Text {
                text: t("ready")
                color: theme.textMain
                font.pixelSize: 28
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: t("ready_subtitle")
                color: theme.textSecondary
                font.pixelSize: 16
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 400
            }
        }
    }

    // ================================================================
    //  MAIN PAGE
    // ================================================================
    Component {
        id: mainPageComponent

        Rectangle {
            color: theme.bgMain

            // ---- SIDEBAR ----
            Rectangle {
                id: sidebar
                width: sidebarWidth
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: theme.bgPanel
                clip: true
                visible: sidebarVisible

                Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutQuad } }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    // Logo + Toggle
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "🌊 Ohara GPT"
                            color: theme.textMain
                            font.pixelSize: 20
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        // Settings gear
                        Rectangle {
                            width: 32; height: 32; radius: 8
                            color: settingsArea.hovered ? theme.bgItemHover : "transparent"
                            property bool hovered: false
                            Text { text: "⚙"; font.pixelSize: 16; anchors.centerIn: parent; color: theme.textSecondary }
                            MouseArea {
                                id: settingsArea
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                property bool hovered: false
                                onEntered: parent.hovered = true
                                onExited: parent.hovered = false
                                onClicked: settingsDrawer.open()
                            }
                        }
                    }

                    // New Chat button
                    Rectangle {
                        Layout.fillWidth: true
                        height: 44
                        radius: 12
                        gradient: Gradient {
                            GradientStop { position: 0; color: theme.primary }
                            GradientStop { position: 1; color: theme.primaryHover }
                        }

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 8
                            Text { text: "+"; color: "white"; font.pixelSize: 18; font.bold: true }
                            Text { text: t("new_chat"); color: "white"; font.pixelSize: 14; font.bold: true }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: createNewSession()
                        }
                    }

                    // Chat History header
                    Text {
                        text: t("chat_history")
                        color: theme.textMuted
                        font.pixelSize: 12
                        font.bold: true
                        Layout.topMargin: 8
                    }

                    // Session list
                    ListView {
                        id: sessionList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.maximumHeight: parent.height * 0.35
                        clip: true
                        spacing: 4
                        model: sessionsModel

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 48
                            radius: 10
                            color: currentSessionId === model.id ? theme.bgItemHover : (sessionHover.containsMouse ? theme.bgItem : "transparent")
                            border.color: currentSessionId === model.id ? theme.primary : "transparent"
                            border.width: currentSessionId === model.id ? 1 : 0

                            Behavior on color { ColorAnimation { duration: 150 } }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                Text { text: "💬"; font.pixelSize: 14 }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: model.title
                                        color: theme.textMain
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: (model.message_count || 0) + " msgs"
                                        color: theme.textSecondary
                                        font.pixelSize: 10
                                    }
                                }

                                // Delete button
                                Rectangle {
                                    width: 24; height: 24; radius: 6
                                    color: delHover.containsMouse ? theme.dangerBg : "transparent"
                                    visible: sessionHover.containsMouse
                                    Text { text: "×"; color: theme.danger; font.pixelSize: 16; font.bold: true; anchors.centerIn: parent }
                                    MouseArea {
                                        id: delHover
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        hoverEnabled: true
                                        onClicked: {
                                            Database.deleteSession(model.id);
                                            if (currentSessionId === model.id) currentSessionId = 0;
                                            loadSessions();
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                id: sessionHover
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton
                                onClicked: switchToSession(model.id)
                                z: -1
                            }
                        }

                        // Empty state
                        Text {
                            visible: sessionsModel.count === 0
                            text: t("no_sessions")
                            color: theme.textSecondary
                            font.pixelSize: 13
                            anchors.centerIn: parent
                        }
                    }

                    // Separator
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.bgItemHover }

                    // Models section
                    Text {
                        text: t("models")
                        color: theme.textMuted
                        font.pixelSize: 12
                        font.bold: true
                    }

                    ListView {
                        id: modelListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: ModelManager.getModelCatalog(ramGB)

                header: Component {
                    ColumnLayout {
                        width: ListView.view ? ListView.view.width : 400
                        spacing: 8
                        // --- Custom Model Download ---
                        Rectangle {
                            width: ListView.view ? ListView.view.width : parent.width
                            height: 120
                            radius: 12
                            color: theme.bgPanelElevated
                            border.color: theme.border

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8

                                Text { text: "Custom HuggingFace Model"; color: theme.textMain; font.pixelSize: 14; font.bold: true }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    TextField {
                                        id: customRepoIdDrawer
                                        Layout.fillWidth: true
                                        placeholderText: "Repo ID (e.g. bartowski/Llama-3-8B-Instruct-GGUF)"
                                        color: theme.textMain
                                        background: Rectangle { color: theme.bgItem; radius: 6; border.color: theme.border }
                                    }

                                    TextField {
                                        id: customFilenameDrawer
                                        Layout.fillWidth: true
                                        placeholderText: "Filename (e.g. model.gguf)"
                                        color: theme.textMain
                                        background: Rectangle { color: theme.bgItem; radius: 6; border.color: theme.border }
                                    }
                                }

                                Rectangle {
                                    Layout.alignment: Qt.AlignRight
                                    width: 120; height: 32; radius: 16
                                    color: ModelManager.downloading ? theme.textMuted : theme.primary

                                    Text { text: "Download"; color: "white"; font.pixelSize: 12; font.bold: true; anchors.centerIn: parent }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        enabled: !ModelManager.downloading && customRepoIdDrawer.text.length > 0 && customFilenameDrawer.text.length > 0
                                        onClicked: {
                                            ModelManager.downloadModel(customRepoIdDrawer.text.trim(), customFilenameDrawer.text.trim(), "");
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 84
                            radius: 12
                            color: activeFilename === modelData.filename ? theme.bgActive : (modelData.compatible ? theme.bgPanelElevated : theme.bgMain)
                            border.color: activeFilename === modelData.filename ? theme.primary : (modelData.downloaded ? theme.success : theme.bgItemHover)
                            border.width: activeFilename === modelData.filename ? 2 : 1
                            opacity: modelData.compatible ? 1.0 : 0.45

                            Behavior on border.color { ColorAnimation { duration: 200 } }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: modelData.name
                                        color: theme.textMain
                                        font.pixelSize: 13
                                        font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        width: tagText.width + 10; height: 18; radius: 9
                                        color: theme.bgActive
                                        Text { id: tagText; text: modelData.type; color: theme.textAccent; font.pixelSize: 9; font.bold: true; anchors.centerIn: parent }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: t("min_ram") + ": " + modelData.minRamGB + " GB"
                                        color: theme.textMuted
                                        font.pixelSize: 10
                                        Layout.fillWidth: true
                                    }

                                    // Status / Actions
                                    Rectangle {
                                        width: 64; height: 26; radius: 13
                                        color: {
                                            if (activeFilename === modelData.filename) return theme.primary;
                                            if (modelData.downloaded) return theme.successBg;
                                            if (ModelManager.downloading && ModelManager.downloadingModel === modelData.filename) return theme.warningBg;
                                            return theme.bgItemHover;
                                        }
                                        visible: modelData.compatible

                                        Text {
                                            text: {
                                                if (activeFilename === modelData.filename) return "Active";
                                                if (ModelManager.downloading && ModelManager.downloadingModel === modelData.filename) return Math.round(ModelManager.downloadProgress * 100) + "%";
                                                if (modelData.downloaded) return "Load";
                                                return "↓";
                                            }
                                            color: "white"
                                            font.pixelSize: 10
                                            font.bold: true
                                            anchors.centerIn: parent
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: modelData.compatible && !ModelManager.downloading && activeFilename !== modelData.filename
                                            onClicked: {
                                                if (modelData.downloaded) {
                                                    // Load model
                                                    activeModelName = modelData.name;
                                                    activeFilename = modelData.filename;
                                                    var path = ModelManager.getModelPath(modelData.filename);
                                                    InferenceEngine.loadModel(path,
                                                        HardwareDetector.recommendedContextSize,
                                                        HardwareDetector.recommendedGpuLayers);
                                                    if (currentSessionId > 0) {
                                                        Database.updateSessionModel(currentSessionId, modelData.name);
                                                    }
                                                } else {
                                                    var mmproj = modelData.mmprojFilename || "";
                                                    ModelManager.downloadModel(modelData.repoId, modelData.filename, mmproj);
                                                }
                                            }
                                        }
                                    }
                                }

                                // Download progress bar
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 3
                                    radius: 2
                                    color: theme.bgItemHover
                                    visible: ModelManager.downloading && ModelManager.downloadingModel === modelData.filename

                                    Rectangle {
                                        width: parent.width * ModelManager.downloadProgress
                                        height: parent.height
                                        radius: 2
                                        color: theme.warning
                                        Behavior on width { NumberAnimation { duration: 200 } }
                                    }
                                }
                            }
                        }
                    }

                    // Language toggle
                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: 8
                        color: theme.bgPanelElevated

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 4

                            Rectangle {
                                width: 50; height: 28; radius: 6
                                color: Settings.language === "id" ? theme.primary : "transparent"
                                Text { text: "🇮🇩 ID"; color: "white"; font.pixelSize: 11; font.bold: true; anchors.centerIn: parent }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Settings.language = "id" }
                            }
                            Rectangle {
                                width: 50; height: 28; radius: 6
                                color: Settings.language === "en" ? theme.primary : "transparent"
                                Text { text: "🇬🇧 EN"; color: "white"; font.pixelSize: 11; font.bold: true; anchors.centerIn: parent }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Settings.language = "en" }
                            }
                        }
                    }
                }
            }

            // ---- MAIN CHAT AREA ----
            Rectangle {
                anchors.left: sidebar.right
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: theme.bgMain

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Top Bar
                    Rectangle {
                        Layout.fillWidth: true
                        height: 60
                        color: theme.bgPanel
                        border.color: theme.bgItem
                        border.width: 0

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            // Sidebar toggle (for small screens)
                            Rectangle {
                                width: 36; height: 36; radius: 8
                                color: sideToggle.containsMouse ? theme.bgItemHover : "transparent"
                                visible: root.width < 900
                                Text { text: "☰"; color: theme.textSecondary; font.pixelSize: 18; anchors.centerIn: parent }
                                MouseArea {
                                    id: sideToggle
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true
                                    onClicked: sidebarVisible = !sidebarVisible
                                }
                            }

                            // Active model / personality
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text {
                                    text: activeModelName || t("select_model_first")
                                    color: theme.textMain
                                    font.pixelSize: 16
                                    font.bold: true
                                }
                                Text {
                                    text: currentPersonality !== "general" ? ("🎭 " + currentPersonality) : ""
                                    color: theme.textMuted
                                    font.pixelSize: 12
                                    visible: text !== ""
                                }
                            }

                            // Personality selector
                            Rectangle {
                                width: personIcon.width + 16
                                height: 32; radius: 8
                                color: personBtn.containsMouse ? theme.bgItemHover : "transparent"
                                Text { id: personIcon; text: "🎭"; font.pixelSize: 16; anchors.centerIn: parent }
                                MouseArea {
                                    id: personBtn
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true
                                    onClicked: personalityPopup.open()
                                }
                            }

                            // Stop button
                            Rectangle {
                                width: 70; height: 32; radius: 16
                                color: theme.danger
                                visible: InferenceEngine.generating
                                Text { text: "⏹ " + t("stop"); color: "white"; font.pixelSize: 12; font.bold: true; anchors.centerIn: parent }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: InferenceEngine.stopGeneration()
                                }
                            }
                        }
                    }

                    // Messages Area
                    ListView {
                        id: chatList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 16
                        clip: true
                        spacing: 12
                        cacheBuffer: 2000
                        model: chatModel

                        // Empty state
                        Text {
                            visible: chatModel.count === 0
                            text: "🌊\n\n" + t("welcome_title") + "\n" + t("welcome_subtitle")
                            color: theme.textMuted
                            font.pixelSize: 16
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            anchors.centerIn: parent
                            width: parent.width * 0.6
                        }

                        delegate: Item {
                            width: chatList.width
                            height: bubble.height + 8
                            property bool isUser: model.sender === "User"

                            Rectangle {
                                id: bubble
                                width: Math.min(Math.max(msgContent.implicitWidth + 32, 100), chatList.width * 0.82)
                                height: msgContent.implicitHeight + 28
                                radius: 16
                                anchors.right: isUser ? parent.right : undefined
                                anchors.left: isUser ? undefined : parent.left

                                gradient: Gradient {
                                    GradientStop { position: 0; color: isUser ? theme.primaryHover : theme.bgItem }
                                    GradientStop { position: 1; color: isUser ? theme.primaryHover : theme.bgPanelElevated }
                                }

                                border.color: isUser ? theme.primary : theme.bgItemHover
                                border.width: 1

                                ColumnLayout {
                                    id: msgContent
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 4

                                    // Sender label
                                    Text {
                                        text: isUser ? "You" : "Ohara"
                                        color: isUser ? theme.textAccent : theme.textAccent
                                        font.pixelSize: 11
                                        font.bold: true
                                    }

                                    Text {
                                        text: model.text
                                        color: theme.textMain
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: Settings.fontSize
                                        textFormat: Text.MarkdownText
                                        Layout.fillWidth: true
                                        onLinkActivated: function(link) { Qt.openUrlExternally(link) }
                                    }
                                }

                                // Action buttons on hover
                                Row {
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 6
                                    spacing: 4
                                    visible: !isUser && bubbleHover.containsMouse

                                    // Copy
                                    Rectangle {
                                        width: 28; height: 28; radius: 6
                                        color: copyBtn.containsMouse ? theme.textMuted : theme.bgItemHover
                                        Text { text: "📋"; font.pixelSize: 12; anchors.centerIn: parent }
                                        MouseArea {
                                            id: copyBtn
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            hoverEnabled: true
                                            onClicked: {
                                                Clipboard.setText(model.text)
                                                // Optional: show a toast here
                                            }
                                        }
                                    }

                                    // Delete
                                    Rectangle {
                                        width: 28; height: 28; radius: 6
                                        color: delBtn.containsMouse ? theme.textMuted : theme.bgItemHover
                                        Text { text: "🗑️"; font.pixelSize: 12; anchors.centerIn: parent }
                                        MouseArea {
                                            id: delBtn
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            hoverEnabled: true
                                            onClicked: {
                                                // Simplified regenerate/delete logic for UI
                                                // Real deletion would need DB query, for now we remove from model
                                                chatModel.remove(index)
                                            }
                                        }
                                    }
                                }

                                MouseArea {
                                    id: bubbleHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                    z: -1
                                }
                            }
                        }

                        // Scroll to bottom button
                        Rectangle {
                            width: 40; height: 40; radius: 20
                            color: theme.bgItemHover
                            border.color: theme.textMuted
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 8
                            visible: !chatList.atYEnd && chatModel.count > 5

                            Text { text: "↓"; color: theme.textSecondary; font.pixelSize: 18; anchors.centerIn: parent }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: chatList.positionViewAtEnd()
                            }
                        }
                    }

                    // Thinking indicator
                    RowLayout {
                        Layout.leftMargin: 32
                        Layout.bottomMargin: 4
                        visible: isThinking
                        spacing: 6

                        Repeater {
                            model: 3
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: theme.primary

                                SequentialAnimation on opacity {
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.3; duration: 300; easing.type: Easing.InOutQuad }
                                    PauseAnimation { duration: index * 150 }
                                    NumberAnimation { to: 1.0; duration: 300; easing.type: Easing.InOutQuad }
                                    PauseAnimation { duration: (2 - index) * 150 }
                                }
                            }
                        }

                        Text { text: t("thinking"); color: theme.textMuted; font.pixelSize: 12; font.italic: true }
                    }

                    // Image preview
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        height: 64
                        color: "transparent"
                        visible: selectedImagePath !== ""

                        Rectangle {
                            width: 56; height: 56; radius: 12
                            color: theme.bgItem
                            border.color: theme.primary

                            Image {
                                anchors.fill: parent; anchors.margins: 3
                                source: selectedImagePath
                                fillMode: Image.PreserveAspectCrop; clip: true
                            }

                            Rectangle {
                                width: 18; height: 18; radius: 9; color: theme.danger
                                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: -6
                                Text { text: "×"; color: "white"; font.pixelSize: 12; font.bold: true; anchors.centerIn: parent }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: { selectedImagePath = ""; selectedImageBase64 = "" }
                                }
                            }
                        }
                    }

                    // ---- INPUT AREA ----
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        Layout.topMargin: 4
                        height: Math.max(56, inputField.implicitHeight + 20)
                        Layout.maximumHeight: 180
                        radius: 28
                        color: theme.bgItem
                        border.color: inputField.activeFocus ? theme.primary : theme.bgItemHover
                        border.width: inputField.activeFocus ? 2 : 1

                        Behavior on border.color { ColorAnimation { duration: 200 } }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 8
                            anchors.topMargin: 4
                            anchors.bottomMargin: 4
                            spacing: 8

                            // Attach button
                            Rectangle {
                                width: 36; height: 36; radius: 18
                                color: attachBtn.containsMouse ? theme.bgItemHover : "transparent"
                                Text { text: "📎"; font.pixelSize: 18; anchors.centerIn: parent }
                                MouseArea {
                                    id: attachBtn
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true
                                    onClicked: fileDialog.open()
                                }
                            }

                            // Multi-line TextArea
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                TextArea {
                                    id: inputField
                                    placeholderText: currentSessionId === 0 ? t("create_session_first") :
                                                     (!InferenceEngine.modelLoaded ? t("select_model_first") : t("type_message"))
                                    color: theme.textMain
                                    font.pixelSize: 14
                                    wrapMode: TextArea.Wrap
                                    background: Rectangle { color: "transparent" }
                                    enabled: InferenceEngine.modelLoaded && !InferenceEngine.generating && currentSessionId !== 0
                                    placeholderTextColor: theme.textSecondary

                                    Keys.onReturnPressed: function(event) {
                                        if (event.modifiers & Qt.ShiftModifier) {
                                            // Shift+Enter: new line
                                            event.accepted = false;
                                        } else {
                                            event.accepted = true;
                                            sendMessage();
                                        }
                                    }
                                }
                            }

                            // Send button

                                // --- Voice Recording Button ---
                                Rectangle {
                                    width: 40; height: 40; radius: 10
                                    color: VoiceManager.isRecording ? theme.danger : theme.bgItem
                                    border.color: theme.border
                                    Text { text: "🎙"; anchors.centerIn: parent; font.pixelSize: 18 }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (VoiceManager.isRecording) {
                                                VoiceManager.stopRecording();
                                                VoiceManager.processVoice("");
                                            } else {
                                                VoiceManager.startRecording();
                                            }
                                        }
                                    }
                                }
Rectangle {
                                width: 44; height: 44; radius: 22

                                gradient: Gradient {
                                    GradientStop { position: 0; color: (InferenceEngine.modelLoaded && inputField.text.trim().length > 0 && !InferenceEngine.generating && currentSessionId !== 0) ? theme.primary : theme.bgItemHover }
                                    GradientStop { position: 1; color: (InferenceEngine.modelLoaded && inputField.text.trim().length > 0 && !InferenceEngine.generating && currentSessionId !== 0) ? theme.primaryHover : theme.bgItemHover }
                                }

                                Text {
                                    text: "➤"
                                    color: "white"
                                    font.pixelSize: 18
                                    anchors.centerIn: parent
                                    rotation: 0
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: InferenceEngine.modelLoaded && inputField.text.trim().length > 0 && !InferenceEngine.generating && currentSessionId !== 0
                                    onClicked: sendMessage()
                                }
                            }
                        }
                    }

                    // ---- STATUS BAR ----
                    Rectangle {
                        Layout.fillWidth: true
                        height: 32
                        color: theme.bgMain

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            spacing: 16

                            // Connection status
                            Row {
                                spacing: 6
                                Rectangle {
                                    width: 8; height: 8; radius: 4
                                    color: InferenceEngine.modelLoaded ? theme.success : theme.danger
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: InferenceEngine.modelLoaded ? t("connected") : t("disconnected")
                                    color: theme.textMuted; font.pixelSize: 11
                                }
                            }

                            // Active model
                            Text {
                                text: InferenceEngine.loadedModelName ? ("🤖 " + InferenceEngine.loadedModelName) : ""
                                color: theme.textSecondary; font.pixelSize: 11
                                visible: text !== ""
                            }

                            Item { Layout.fillWidth: true }

                            // Tokens/sec
                            Text {
                                text: InferenceEngine.generating ? (InferenceEngine.tokensPerSecond.toFixed(1) + " " + t("tokens_per_sec")) : ""
                                color: theme.success; font.pixelSize: 11; font.bold: true
                                visible: InferenceEngine.generating
                            }

                            // RAM
                            Text {
                                text: t("ram") + ": " + availRamGB.toFixed(1) + "/" + ramGB.toFixed(1) + " GB"
                                color: theme.textSecondary; font.pixelSize: 11
                            }

                            // Download speed
                            Text {
                                text: ModelManager.downloading ? ("📥 " + ModelManager.downloadSpeedMBps.toFixed(1) + " MB/s") : ""
                                color: theme.warning; font.pixelSize: 11; font.bold: true
                                visible: ModelManager.downloading
                            }
                        }
                    }
                }
            }

            // ---- PERSONALITY POPUP ----
            Popup {
                id: personalityPopup
                x: parent.width - 340
                y: 64
                width: 320
                height: 400
                modal: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: theme.bgPanelElevated
                    radius: 16
                    border.color: theme.border
                    border.width: 1
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Text {
                        text: "🎭 " + t("personality")
                        color: theme.textMain
                        font.pixelSize: 16
                        font.bold: true
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: Settings.getPersonalityPresets()

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 56
                            radius: 10
                            color: currentPersonality === modelData.id ? theme.bgActive : theme.bgPanel
                            border.color: currentPersonality === modelData.id ? theme.primary : theme.bgItemHover

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                Text { text: modelData.icon; font.pixelSize: 20 }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { text: modelData.name; color: theme.textMain; font.pixelSize: 13; font.bold: true }
                                    Text {
                                        text: modelData.prompt.substring(0, 50) + "..."
                                        color: theme.textMuted; font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    currentPersonality = modelData.id;
                                    currentSystemPrompt = modelData.prompt;
                                    if (currentSessionId > 0) {
                                        Database.updateSessionPrompt(currentSessionId, modelData.prompt);
                                        Database.updateSessionPersonality(currentSessionId, modelData.id);
                                    }
                                    personalityPopup.close();
                                }
                            }
                        }
                    }

                    // Custom prompt
                    Text {
                        text: t("system_prompt")
                        color: theme.textMuted
                        font.pixelSize: 12
                        font.bold: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 60
                        radius: 8
                        color: theme.bgPanel
                        border.color: theme.border

                        TextArea {
                            anchors.fill: parent
                            anchors.margins: 8
                            text: currentSystemPrompt
                            color: theme.textMain
                            font.pixelSize: 11
                            wrapMode: TextArea.Wrap
                            background: Rectangle { color: "transparent" }
                            placeholderText: "Custom system prompt..."
                            placeholderTextColor: theme.textMuted
                            onTextChanged: {
                                currentSystemPrompt = text;
                                if (currentSessionId > 0) {
                                    Database.updateSessionPrompt(currentSessionId, text);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ================================================================
    //  SETTINGS DRAWER
    // ================================================================
    Drawer {
        id: settingsDrawer
        width: Math.min(root.width * 0.85, 400)
        height: root.height
        edge: Qt.RightEdge

        background: Rectangle {
            color: theme.bgPanel
            border.color: theme.border
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 20

            // Header
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "⚙ " + t("settings")
                    color: theme.textMain
                    font.pixelSize: 20
                    font.bold: true
                    Layout.fillWidth: true
                }
                Rectangle {
                    width: 32; height: 32; radius: 8
                    color: closeBtn.containsMouse ? theme.bgItemHover : "transparent"
                    Text { text: "×"; color: theme.textSecondary; font.pixelSize: 20; anchors.centerIn: parent }
                    MouseArea {
                        id: closeBtn
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: settingsDrawer.close()
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: theme.bgItemHover }

            // Language
            ColumnLayout {
                spacing: 8
                Text { text: "🌐 " + t("language"); color: theme.textSecondary; font.pixelSize: 13; font.bold: true }
                Row {
                    spacing: 8
                    Repeater {
                        model: [{"code": "id", "label": "🇮🇩 Bahasa Indonesia"}, {"code": "en", "label": "🇬🇧 English"}]
                        Rectangle {
                            width: 160; height: 40; radius: 10
                            color: Settings.language === modelData.code ? theme.bgActive : theme.bgItem
                            border.color: Settings.language === modelData.code ? theme.primary : theme.bgItemHover
                            Text { text: modelData.label; color: theme.textMain; font.pixelSize: 13; anchors.centerIn: parent }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Settings.language = modelData.code }
                        }
                    }
                }
            }

            // Theme
            ColumnLayout {
                spacing: 8
                Text { text: "🎨 " + t("theme"); color: theme.textSecondary; font.pixelSize: 13; font.bold: true }
                Row {
                    spacing: 8
                    Repeater {
                        model: [{"code": "dark", "label": "🌙 " + t("dark")}, {"code": "light", "label": "☀ " + t("light")}]
                        Rectangle {
                            width: 120; height: 40; radius: 10
                            color: Settings.theme === modelData.code ? theme.bgActive : theme.bgItem
                            border.color: Settings.theme === modelData.code ? theme.primary : theme.bgItemHover
                            Text { text: modelData.label; color: theme.textMain; font.pixelSize: 13; anchors.centerIn: parent }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Settings.theme = modelData.code }
                        }
                    }
                }
            }

            // Font size
            ColumnLayout {
                spacing: 8
                Text { text: "🔤 " + t("font_size") + ": " + Settings.fontSize + "px"; color: theme.textSecondary; font.pixelSize: 13; font.bold: true }
                Slider {
                    Layout.fillWidth: true
                    from: 10; to: 24; stepSize: 1
                    value: Settings.fontSize
                    onMoved: Settings.fontSize = value

                    background: Rectangle {
                        x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: parent.availableWidth; height: 4; radius: 2; color: theme.bgItemHover
                        Rectangle { width: parent.parent.visualPosition * parent.width; height: parent.height; radius: 2; color: theme.primary }
                    }
                    handle: Rectangle {
                        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: 20; height: 20; radius: 10; color: theme.primary; border.color: theme.textAccent
                    }
                }
            }

            // Temperature
            ColumnLayout {
                spacing: 8
                Text { text: "🌡 " + t("temperature_label") + ": " + Settings.temperature.toFixed(1); color: theme.textSecondary; font.pixelSize: 13; font.bold: true }
                Slider {
                    Layout.fillWidth: true
                    from: 0.0; to: 2.0; stepSize: 0.1
                    value: Settings.temperature
                    onMoved: Settings.temperature = value

                    background: Rectangle {
                        x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: parent.availableWidth; height: 4; radius: 2; color: theme.bgItemHover
                        Rectangle { width: parent.parent.visualPosition * parent.width; height: parent.height; radius: 2; color: theme.warning }
                    }
                    handle: Rectangle {
                        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: 20; height: 20; radius: 10; color: theme.warning; border.color: theme.warning
                    }
                }
            }

            // Max Response Tokens
            ColumnLayout {
                spacing: 8
                Text { text: "📝 " + t("max_tokens") + ": " + Settings.maxResponseTokens; color: theme.textSecondary; font.pixelSize: 13; font.bold: true }
                Slider {
                    Layout.fillWidth: true
                    from: 128; to: 8192; stepSize: 128
                    value: Settings.maxResponseTokens
                    onMoved: Settings.maxResponseTokens = value

                    background: Rectangle {
                        x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: parent.availableWidth; height: 4; radius: 2; color: theme.bgItemHover
                        Rectangle { width: parent.parent.visualPosition * parent.width; height: parent.height; radius: 2; color: theme.success }
                    }
                    handle: Rectangle {
                        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: 20; height: 20; radius: 10; color: theme.success; border.color: theme.success
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Clear Data
            Rectangle {
                Layout.fillWidth: true
                height: 40
                radius: 8
                color: clearDataBtn.containsMouse ? theme.dangerHover : theme.dangerBg
                
                Text {
                    text: Settings.language === "ID" ? "Hapus Semua Data" : "Clear All Data"
                    color: "white"
                    font.pixelSize: 13
                    font.bold: true
                    anchors.centerIn: parent
                }

                MouseArea {
                    id: clearDataBtn
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Database.clearAllData();
                        sessionsModel.clear();
                        chatModel.clear();
                        Settings.language = "ID";
                    }
                }
            }

            // About
            Rectangle {
                Layout.fillWidth: true
                height: 60
                radius: 12
                color: theme.bgPanelElevated

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 2
                    Text { text: "🌊 Ohara GPT v" + AppVersion; color: theme.textSecondary; font.pixelSize: 13; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "Fully Embedded C++ · Offline-First · Privacy-First"; color: theme.textSecondary; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                }
            }
        }
    }
}

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQuick.Dialogs 1.3
import QtQuick.Effects

Window {
    id: root
    width: 1280
    height: 860
    minimumWidth: 800
    minimumHeight: 600
    visible: true
    title: "Ohara GPT"
    color: "#06080f"

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
            color: "#06080f"

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
                            color: onboardingStep >= index ? "#3b82f6" : "#1e293b"
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
                        color: "#64748b"
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
                        color: "#3b82f6"
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
                            GradientStop { position: 0; color: "#3b82f6" }
                            GradientStop { position: 1; color: "#2563eb" }
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
                color: "#f1f5f9"
                font.pixelSize: 28
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: t("welcome_subtitle")
                color: "#94a3b8"
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
                color: "#f1f5f9"
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
                    color: "#0f1629"
                    border.color: "#1e293b"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        Text { text: modelData.icon; font.pixelSize: 20 }
                        Text { text: modelData.label; color: "#94a3b8"; font.pixelSize: 14; Layout.fillWidth: true }
                        Text { text: modelData.value; color: "#e2e8f0"; font.pixelSize: 14; font.bold: true }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 48
                radius: 12
                color: "#0a2540"
                border.color: "#10b981"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    Text { text: "✨"; font.pixelSize: 18 }
                    Text { text: t("recommended") + ": " + HardwareDetector.recommendedModel; color: "#10b981"; font.pixelSize: 13; font.bold: true }
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
                color: "#f1f5f9"
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
                    color: modelData.compatible ? "#0f1629" : "#0a0d15"
                    border.color: modelData.downloaded ? "#10b981" : (modelData.compatible ? "#1e293b" : "#111827")
                    opacity: modelData.compatible ? 1.0 : 0.4

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: modelData.name; color: "#e2e8f0"; font.pixelSize: 14; font.bold: true }
                            Text {
                                text: modelData.type + " · " + t("min_ram") + ": " + modelData.minRamGB + "GB"
                                color: "#64748b"; font.pixelSize: 11
                            }
                        }

                        Rectangle {
                            width: 80; height: 32; radius: 16
                            color: modelData.downloaded ? "#065f46" : "#1e40af"
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
                color: "#f1f5f9"
                font.pixelSize: 28
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: t("ready_subtitle")
                color: "#94a3b8"
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
            color: "#06080f"

            // ---- SIDEBAR ----
            Rectangle {
                id: sidebar
                width: sidebarWidth
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: "#0a0e1a"
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
                            color: "#f1f5f9"
                            font.pixelSize: 20
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        // Settings gear
                        Rectangle {
                            width: 32; height: 32; radius: 8
                            color: settingsArea.hovered ? "#1e293b" : "transparent"
                            property bool hovered: false
                            Text { text: "⚙"; font.pixelSize: 16; anchors.centerIn: parent; color: "#94a3b8" }
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
                            GradientStop { position: 0; color: "#3b82f6" }
                            GradientStop { position: 1; color: "#2563eb" }
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
                        color: "#64748b"
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
                            color: currentSessionId === model.id ? "#1e293b" : (sessionHover.containsMouse ? "#111827" : "transparent")
                            border.color: currentSessionId === model.id ? "#3b82f6" : "transparent"
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
                                        color: "#e2e8f0"
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: (model.message_count || 0) + " msgs"
                                        color: "#475569"
                                        font.pixelSize: 10
                                    }
                                }

                                // Delete button
                                Rectangle {
                                    width: 24; height: 24; radius: 6
                                    color: delHover.containsMouse ? "#7f1d1d" : "transparent"
                                    visible: sessionHover.containsMouse
                                    Text { text: "×"; color: "#ef4444"; font.pixelSize: 16; font.bold: true; anchors.centerIn: parent }
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
                            color: "#475569"
                            font.pixelSize: 13
                            anchors.centerIn: parent
                        }
                    }

                    // Separator
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#1e293b" }

                    // Models section
                    Text {
                        text: t("models")
                        color: "#64748b"
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

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 84
                            radius: 12
                            color: activeFilename === modelData.filename ? "#1a2744" : (modelData.compatible ? "#0f1629" : "#080b14")
                            border.color: activeFilename === modelData.filename ? "#3b82f6" : (modelData.downloaded ? "#10b981" : "#1e293b")
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
                                        color: "#e2e8f0"
                                        font.pixelSize: 13
                                        font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        width: tagText.width + 10; height: 18; radius: 9
                                        color: "#1e3a5f"
                                        Text { id: tagText; text: modelData.type; color: "#60a5fa"; font.pixelSize: 9; font.bold: true; anchors.centerIn: parent }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: t("min_ram") + ": " + modelData.minRamGB + " GB"
                                        color: "#64748b"
                                        font.pixelSize: 10
                                        Layout.fillWidth: true
                                    }

                                    // Status / Actions
                                    Rectangle {
                                        width: 64; height: 26; radius: 13
                                        color: {
                                            if (activeFilename === modelData.filename) return "#1e40af";
                                            if (modelData.downloaded) return "#065f46";
                                            if (ModelManager.downloading && ModelManager.downloadingModel === modelData.filename) return "#92400e";
                                            return "#1e293b";
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
                                    color: "#1e293b"
                                    visible: ModelManager.downloading && ModelManager.downloadingModel === modelData.filename

                                    Rectangle {
                                        width: parent.width * ModelManager.downloadProgress
                                        height: parent.height
                                        radius: 2
                                        color: "#f59e0b"
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
                        color: "#0f1629"

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 4

                            Rectangle {
                                width: 50; height: 28; radius: 6
                                color: Settings.language === "id" ? "#3b82f6" : "transparent"
                                Text { text: "🇮🇩 ID"; color: "white"; font.pixelSize: 11; font.bold: true; anchors.centerIn: parent }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Settings.language = "id" }
                            }
                            Rectangle {
                                width: 50; height: 28; radius: 6
                                color: Settings.language === "en" ? "#3b82f6" : "transparent"
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
                color: "#06080f"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Top Bar
                    Rectangle {
                        Layout.fillWidth: true
                        height: 60
                        color: "#0a0e1a"
                        border.color: "#111827"
                        border.width: 0

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            // Sidebar toggle (for small screens)
                            Rectangle {
                                width: 36; height: 36; radius: 8
                                color: sideToggle.containsMouse ? "#1e293b" : "transparent"
                                visible: root.width < 900
                                Text { text: "☰"; color: "#94a3b8"; font.pixelSize: 18; anchors.centerIn: parent }
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
                                    color: "#e2e8f0"
                                    font.pixelSize: 16
                                    font.bold: true
                                }
                                Text {
                                    text: currentPersonality !== "general" ? ("🎭 " + currentPersonality) : ""
                                    color: "#64748b"
                                    font.pixelSize: 12
                                    visible: text !== ""
                                }
                            }

                            // Personality selector
                            Rectangle {
                                width: personIcon.width + 16
                                height: 32; radius: 8
                                color: personBtn.containsMouse ? "#1e293b" : "transparent"
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
                                color: "#dc2626"
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
                            color: "#334155"
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
                                    GradientStop { position: 0; color: isUser ? "#2563eb" : "#111827" }
                                    GradientStop { position: 1; color: isUser ? "#1d4ed8" : "#0f1629" }
                                }

                                border.color: isUser ? "#3b82f6" : "#1e293b"
                                border.width: 1

                                ColumnLayout {
                                    id: msgContent
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 4

                                    // Sender label
                                    Text {
                                        text: isUser ? "You" : "Ohara"
                                        color: isUser ? "#93c5fd" : "#60a5fa"
                                        font.pixelSize: 11
                                        font.bold: true
                                    }

                                    Text {
                                        text: model.text
                                        color: "#e2e8f0"
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
                                        color: copyBtn.containsMouse ? "#334155" : "#1e293b"
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
                                        color: delBtn.containsMouse ? "#334155" : "#1e293b"
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
                            color: "#1e293b"
                            border.color: "#334155"
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 8
                            visible: !chatList.atYEnd && chatModel.count > 5

                            Text { text: "↓"; color: "#94a3b8"; font.pixelSize: 18; anchors.centerIn: parent }
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
                                color: "#3b82f6"

                                SequentialAnimation on opacity {
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.3; duration: 300; easing.type: Easing.InOutQuad }
                                    PauseAnimation { duration: index * 150 }
                                    NumberAnimation { to: 1.0; duration: 300; easing.type: Easing.InOutQuad }
                                    PauseAnimation { duration: (2 - index) * 150 }
                                }
                            }
                        }

                        Text { text: t("thinking"); color: "#64748b"; font.pixelSize: 12; font.italic: true }
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
                            color: "#111827"
                            border.color: "#3b82f6"

                            Image {
                                anchors.fill: parent; anchors.margins: 3
                                source: selectedImagePath
                                fillMode: Image.PreserveAspectCrop; clip: true
                            }

                            Rectangle {
                                width: 18; height: 18; radius: 9; color: "#dc2626"
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
                        color: "#111827"
                        border.color: inputField.activeFocus ? "#3b82f6" : "#1e293b"
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
                                color: attachBtn.containsMouse ? "#1e293b" : "transparent"
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
                                    color: "#e2e8f0"
                                    font.pixelSize: 14
                                    wrapMode: TextArea.Wrap
                                    background: Rectangle { color: "transparent" }
                                    enabled: InferenceEngine.modelLoaded && !InferenceEngine.generating && currentSessionId !== 0
                                    placeholderTextColor: "#475569"

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
                            Rectangle {
                                width: 44; height: 44; radius: 22

                                gradient: Gradient {
                                    GradientStop { position: 0; color: (InferenceEngine.modelLoaded && inputField.text.trim().length > 0 && !InferenceEngine.generating && currentSessionId !== 0) ? "#3b82f6" : "#1e293b" }
                                    GradientStop { position: 1; color: (InferenceEngine.modelLoaded && inputField.text.trim().length > 0 && !InferenceEngine.generating && currentSessionId !== 0) ? "#2563eb" : "#1e293b" }
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
                        color: "#080b14"

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
                                    color: InferenceEngine.modelLoaded ? "#10b981" : "#ef4444"
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: InferenceEngine.modelLoaded ? t("connected") : t("disconnected")
                                    color: "#64748b"; font.pixelSize: 11
                                }
                            }

                            // Active model
                            Text {
                                text: InferenceEngine.loadedModelName ? ("🤖 " + InferenceEngine.loadedModelName) : ""
                                color: "#475569"; font.pixelSize: 11
                                visible: text !== ""
                            }

                            Item { Layout.fillWidth: true }

                            // Tokens/sec
                            Text {
                                text: InferenceEngine.generating ? (InferenceEngine.tokensPerSecond.toFixed(1) + " " + t("tokens_per_sec")) : ""
                                color: "#10b981"; font.pixelSize: 11; font.bold: true
                                visible: InferenceEngine.generating
                            }

                            // RAM
                            Text {
                                text: t("ram") + ": " + availRamGB.toFixed(1) + "/" + ramGB.toFixed(1) + " GB"
                                color: "#475569"; font.pixelSize: 11
                            }

                            // Download speed
                            Text {
                                text: ModelManager.downloading ? ("📥 " + ModelManager.downloadSpeedMBps.toFixed(1) + " MB/s") : ""
                                color: "#f59e0b"; font.pixelSize: 11; font.bold: true
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
                    color: "#0f1629"
                    radius: 16
                    border.color: "#1e293b"
                    border.width: 1
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Text {
                        text: "🎭 " + t("personality")
                        color: "#f1f5f9"
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
                            color: currentPersonality === modelData.id ? "#1a2744" : "#0a0e1a"
                            border.color: currentPersonality === modelData.id ? "#3b82f6" : "#1e293b"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10

                                Text { text: modelData.icon; font.pixelSize: 20 }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { text: modelData.name; color: "#e2e8f0"; font.pixelSize: 13; font.bold: true }
                                    Text {
                                        text: modelData.prompt.substring(0, 50) + "..."
                                        color: "#64748b"; font.pixelSize: 10
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
                        color: "#64748b"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 60
                        radius: 8
                        color: "#0a0e1a"
                        border.color: "#1e293b"

                        TextArea {
                            anchors.fill: parent
                            anchors.margins: 8
                            text: currentSystemPrompt
                            color: "#e2e8f0"
                            font.pixelSize: 11
                            wrapMode: TextArea.Wrap
                            background: Rectangle { color: "transparent" }
                            placeholderText: "Custom system prompt..."
                            placeholderTextColor: "#334155"
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
            color: "#0a0e1a"
            border.color: "#1e293b"
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
                    color: "#f1f5f9"
                    font.pixelSize: 20
                    font.bold: true
                    Layout.fillWidth: true
                }
                Rectangle {
                    width: 32; height: 32; radius: 8
                    color: closeBtn.containsMouse ? "#1e293b" : "transparent"
                    Text { text: "×"; color: "#94a3b8"; font.pixelSize: 20; anchors.centerIn: parent }
                    MouseArea {
                        id: closeBtn
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: settingsDrawer.close()
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#1e293b" }

            // Language
            ColumnLayout {
                spacing: 8
                Text { text: "🌐 " + t("language"); color: "#94a3b8"; font.pixelSize: 13; font.bold: true }
                Row {
                    spacing: 8
                    Repeater {
                        model: [{"code": "id", "label": "🇮🇩 Bahasa Indonesia"}, {"code": "en", "label": "🇬🇧 English"}]
                        Rectangle {
                            width: 160; height: 40; radius: 10
                            color: Settings.language === modelData.code ? "#1e3a5f" : "#111827"
                            border.color: Settings.language === modelData.code ? "#3b82f6" : "#1e293b"
                            Text { text: modelData.label; color: "#e2e8f0"; font.pixelSize: 13; anchors.centerIn: parent }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Settings.language = modelData.code }
                        }
                    }
                }
            }

            // Theme
            ColumnLayout {
                spacing: 8
                Text { text: "🎨 " + t("theme"); color: "#94a3b8"; font.pixelSize: 13; font.bold: true }
                Row {
                    spacing: 8
                    Repeater {
                        model: [{"code": "dark", "label": "🌙 " + t("dark")}, {"code": "light", "label": "☀ " + t("light")}]
                        Rectangle {
                            width: 120; height: 40; radius: 10
                            color: Settings.theme === modelData.code ? "#1e3a5f" : "#111827"
                            border.color: Settings.theme === modelData.code ? "#3b82f6" : "#1e293b"
                            Text { text: modelData.label; color: "#e2e8f0"; font.pixelSize: 13; anchors.centerIn: parent }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Settings.theme = modelData.code }
                        }
                    }
                }
            }

            // Font size
            ColumnLayout {
                spacing: 8
                Text { text: "🔤 " + t("font_size") + ": " + Settings.fontSize + "px"; color: "#94a3b8"; font.pixelSize: 13; font.bold: true }
                Slider {
                    Layout.fillWidth: true
                    from: 10; to: 24; stepSize: 1
                    value: Settings.fontSize
                    onMoved: Settings.fontSize = value

                    background: Rectangle {
                        x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: parent.availableWidth; height: 4; radius: 2; color: "#1e293b"
                        Rectangle { width: parent.parent.visualPosition * parent.width; height: parent.height; radius: 2; color: "#3b82f6" }
                    }
                    handle: Rectangle {
                        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: 20; height: 20; radius: 10; color: "#3b82f6"; border.color: "#60a5fa"
                    }
                }
            }

            // Temperature
            ColumnLayout {
                spacing: 8
                Text { text: "🌡 " + t("temperature_label") + ": " + Settings.temperature.toFixed(1); color: "#94a3b8"; font.pixelSize: 13; font.bold: true }
                Slider {
                    Layout.fillWidth: true
                    from: 0.0; to: 2.0; stepSize: 0.1
                    value: Settings.temperature
                    onMoved: Settings.temperature = value

                    background: Rectangle {
                        x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: parent.availableWidth; height: 4; radius: 2; color: "#1e293b"
                        Rectangle { width: parent.parent.visualPosition * parent.width; height: parent.height; radius: 2; color: "#f59e0b" }
                    }
                    handle: Rectangle {
                        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: 20; height: 20; radius: 10; color: "#f59e0b"; border.color: "#fbbf24"
                    }
                }
            }

            // Max Response Tokens
            ColumnLayout {
                spacing: 8
                Text { text: "📝 " + t("max_tokens") + ": " + Settings.maxResponseTokens; color: "#94a3b8"; font.pixelSize: 13; font.bold: true }
                Slider {
                    Layout.fillWidth: true
                    from: 128; to: 8192; stepSize: 128
                    value: Settings.maxResponseTokens
                    onMoved: Settings.maxResponseTokens = value

                    background: Rectangle {
                        x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: parent.availableWidth; height: 4; radius: 2; color: "#1e293b"
                        Rectangle { width: parent.parent.visualPosition * parent.width; height: parent.height; radius: 2; color: "#10b981" }
                    }
                    handle: Rectangle {
                        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: 20; height: 20; radius: 10; color: "#10b981"; border.color: "#34d399"
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Clear Data
            Rectangle {
                Layout.fillWidth: true
                height: 40
                radius: 8
                color: clearDataBtn.containsMouse ? "#991b1b" : "#7f1d1d"
                
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
                color: "#0f1629"

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 2
                    Text { text: "🌊 Ohara GPT v" + AppVersion; color: "#94a3b8"; font.pixelSize: 13; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "Fully Embedded C++ · Offline-First · Privacy-First"; color: "#475569"; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                }
            }
        }
    }
}

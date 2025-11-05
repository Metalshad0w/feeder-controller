#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// ******************************************************
// --- CREDENCIAIS DE WiFi ---
// ******************************************************
const char* SSID_NAME = "Mojo Dojo Casa House";
const char* PASSWORD = "rockstaRxD0123";
// ******************************************************

// --- Configurações de Segurança e Lógica ---
const String SECRET_TOKEN = "COMIDA"; // Token em maiúsculas
// V19.0: Variáveis locais são apenas *espelhos* do Mega
int FEED_INTERVAL_HOURS = 4;
int FEED_DURATION_MS = 900;
int megaLastFeedHour = -1; // V19.0: -1 = indefinido
bool megaIsInitialized = false; // V19.0: Flag de inicialização

// --- Constantes de Tempo ---
const int START_HOUR = 7; // 7:00 AM
const int END_HOUR = 20;  // 20:00 PM (horário de parada, exclusivo)
const unsigned long ONE_SECOND_MS = 1000UL;
const unsigned long ONE_MINUTE_MS = 60 * ONE_SECOND_MS;
const unsigned long ONE_HOUR_S = 3600UL;

// --- Variáveis de Estado ---
int lastMinute = -1;
String megaSerialBuffer = ""; // V18.0: Buffer para ler dados do Mega
ESP8266WebServer server(80);

// NTP Client Setup (Timezone do Brasil: -3 horas)
const long utcOffsetInSeconds = -10800;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

// --- Protótipos de Funções ---
void send_time_interval(int value);
void send_feed_duration(int value);
void send_feed_now();
void handleRoot();
void handleFeedNow();
void handleFeedTime();
void handleFeedQuantity();
void handleNotFound();
void serveIndexHtml(String token, String alert, int fTime, int fQty);
void updateTime();
void send_feedback_and_refresh(String alert_message, String token_value, int fTime, int fQty);
String getNextFeedTimeDisplay(int lastHour, int intervalHours); 
bool isTokenValid(String token);
void parseMegaResponse(String msg); 

// ----------------------------------------------------------------------
// FUNÇÕES DE TEMPO E COMUNICAÇÃO SERIAL (ESP -> MEGA)
// ----------------------------------------------------------------------

void updateTime(){
    int minute = timeClient.getFormattedTime().substring(3,5).toInt();
    if(minute != lastMinute){
        timeClient.update();
        // Envia a hora atual para o Mega (que usa isso para a lógica automática)
        Serial.println(timeClient.getFormattedTime());
        lastMinute = minute;
    }
}
void send_time_interval(int value) {
    // V19.0: O ESP não atualiza seu próprio estado, apenas envia o comando.
    // O Mega confirmará a mudança com "ACK_TIME="
    String command = "TIME=" + String(value);
    Serial.println(command);
}
void send_feed_duration(int value) {
    // V19.0: O ESP não atualiza seu próprio estado.
    String command = "FEED_QUANTITY=" + String(value);
    Serial.println(command);
}
void send_feed_now() {
    timeClient.update();
    String command = "FEED_NOW=" + timeClient.getFormattedTime();
    Serial.println(command);
}

// ----------------------------------------------------------------------
// V19.0: FUNÇÃO DE PARSING OTIMIZADA (MEGA -> ESP)
// ----------------------------------------------------------------------
void parseMegaResponse(String msg) {
    msg.trim(); // Limpa espaços em branco ou newlines
    
    // Extrai o valor (mais rápido que N substrings)
    int eqPos = msg.indexOf('=');
    if (eqPos == -1) return; // Comando mal formatado
    int value = msg.substring(eqPos + 1).toInt();

    // Compara o comando (mais rápido que N indexOf)
    if (msg.startsWith("LAST_HOUR=")) {
        megaLastFeedHour = value;
        megaIsInitialized = true;
    }
    else if (msg.startsWith("INIT_HOUR=")) {
        megaLastFeedHour = value;
        if (value != -1) megaIsInitialized = true;
    }
    else if (msg.startsWith("INIT_INTERVAL=")) {
        FEED_INTERVAL_HOURS = value;
    }
    else if (msg.startsWith("INIT_QUANTITY=")) {
        FEED_DURATION_MS = value;
    }
    else if (msg.startsWith("ACK_TIME=")) {
        FEED_INTERVAL_HOURS = value;
    }
    else if (msg.startsWith("ACK_QUANTITY=")) {
        FEED_DURATION_MS = value;
    }
}

// ----------------------------------------------------------------------
// FUNÇÃO PRINCIPAL: SETUP (V19.0)
// ----------------------------------------------------------------------

void setup() {
    // Serial (Comunicação com o Mega)
    Serial.begin(115200);

    // --- Inicialização Robusta do LittleFS ---
    const int MAX_LITTLEFS_TRIES = 5;
    bool littleFS_ok = false;

    for (int i = 0; i < MAX_LITTLEFS_TRIES; i++) {
        if (LittleFS.begin()) {
            littleFS_ok = true;
            break;
        }
        delay(100);
        yield();
    }

    if (!littleFS_ok) {
        return;
    }

    // --- Conexão WiFi ROBUSTA e SILENCIOSA ---
    WiFi.mode(WIFI_STA); 
    WiFi.begin(SSID_NAME, PASSWORD);

    int maxTries = 120;
    while (WiFi.status() != WL_CONNECTED && maxTries > 0) {
        delay(500);
        yield();
        maxTries--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        timeClient.begin();
        timeClient.update();
        updateTime();
    }

    // --- Configuração das Rotas do Servidor ---
    server.on("/", handleRoot);
    server.on("/feed/now", handleFeedNow);
    server.on("/feedTime", handleFeedTime);
    server.on("/feedQuantity", handleFeedQuantity);
    server.on("/favicon.ico", []() { server.send(204); });
    server.onNotFound(handleNotFound);

    server.begin();
    
    // O ESP não envia mais as configurações no setup.
    // Ele espera o Mega enviar as configurações "INIT_"
    // (O Mega envia no setup() dele).
}

// ----------------------------------------------------------------------
// FUNÇÃO PRINCIPAL: LOOP (V19.0 - Ouve o Mega)
// ----------------------------------------------------------------------

void loop() {
    server.handleClient();

    // 1. Manutenção da Conexão WiFi (Assíncrona)
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 10000) { 
            WiFi.begin(SSID_NAME, PASSWORD); 
            lastReconnectAttempt = millis();
        }
    } else {
        // 2. Sincronização de Tempo (NTP -> Mega)
        updateTime(); 
    }

    // 3. V19.0: Ouvir respostas do Mega (Mega -> ESP)
    while (Serial.available()) {
        char c = Serial.read();
        megaSerialBuffer += c;
        if (c == '\n') {
            parseMegaResponse(megaSerialBuffer);
            megaSerialBuffer = "";
        }
    }
}

// ----------------------------------------------------------------------
// FUNÇÕES DE SERVIDOR E ROTEAMENTO
// ----------------------------------------------------------------------

// Helper para validação de token
bool isTokenValid(String token) {
    token.toUpperCase(); 
    return (token == SECRET_TOKEN);
}

// FUNÇÃO CRUCIAL (V19.0 - Refatorada para usar Flag de Init)
String getNextFeedTimeDisplay(int lastHour, int intervalHours) {
    
    // V19.0: Usa o booleano de inicialização
    if (!megaIsInitialized) {
        return "Sincronizando com Mega...";
    }
    
    // V19.0: Não precisa de timeClient.update() aqui, 
    // pois o loop() já atualiza o tempo 1x por minuto.
    int currentHour = timeClient.getHours();
    int currentMinute = timeClient.getMinutes();
    
    // 1. Define o próximo horário de alimentação teórico
    int nextFeedHour = lastHour + intervalHours;
    
    // 2. Verifica se o próximo horário ultrapassa 24h (ex: 22 + 4 = 26)
    if (nextFeedHour >= 24) {
        nextFeedHour -= 24; // Ajusta para o dia seguinte (ex: 2h da manhã)
    }

    // 3. Verifica a Janela de Bloqueio Noturno (START_HOUR às END_HOUR)
    
    // Se o próximo feed (teórico) cair na janela noturna...
    if (nextFeedHour >= END_HOUR || nextFeedHour < START_HOUR) {
        // O feed é reagendado para o START_HOUR (7:00 AM)
        nextFeedHour = START_HOUR;
        
        // Se a hora atual já passou das 7:00 (ex: 10:00), 
        // e o próximo feed era 7:00 (mas já passou), 
        // precisamos ajustar o 'lastHour' para o 'currentHour'
        // Mas a lógica do Mega deve tratar isso. Vamos manter o cálculo.
    }
    
    // 4. Calcula o tempo restante até o nextFeedHour
    
    long h_restantes = 0;
    long m_restantes = 0;

    if (currentHour <= nextFeedHour) {
        // Caso simples: Próximo feed hoje
        h_restantes = nextFeedHour - currentHour;
        m_restantes = 60 - currentMinute; 
        if (m_restantes > 0 && h_restantes > 0 && currentMinute > 0) h_restantes--; // Ajuste da hora se o minuto não for 0
    } else {
        // Caso complexo: Próximo feed amanhã
        h_restantes = (24 - currentHour) + nextFeedHour;
        m_restantes = 60 - currentMinute;
        if (m_restantes > 0 && h_restantes > 0 && currentMinute > 0) h_restantes--; // Ajuste da hora se o minuto não for 0
    }
    
    // Ajuste final de minutos
    if (m_restantes == 60) m_restantes = 0;

    String result = "";
    if (h_restantes > 0) result += String(h_restantes) + "h ";
    result += String(m_restantes) + "m";
    return "Em " + result;
}


// Função de Feedback (Meta-Refresh)
void send_feedback_and_refresh(String alert_message, String token_value, int fTime, int fQty) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta http-equiv=\"refresh\" content=\"5; url=/\">"; // 5 segundos
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<title>Processando...</title>";
    html += "<style>";
    html += "body{font-family:'Arial', sans-serif;margin:0;padding:20px;background-color:#f8f9fa;color:#495057; text-align: center;}";
    html += ".alert-success, .alert-warning{padding:12px;border-radius:6px;margin-bottom:15px;font-weight:bold; display: inline-block;}";
    html += ".alert-success{background-color:#d4edda;color:#155724;border:1px solid #c3e6cb;}";
    html += ".alert-warning{background-color:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}";
    html += "</style></head><body>";

    String alertClass = (alert_message.startsWith("SUCESSO")) ? "alert-success" : "alert-warning";
    html += "<div class=\"" + alertClass + "\"><p>";
    html += alert_message;
    html += "</p></div>";

    html += "<p>Redirecionando em 5 segundos...</p>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}


// OTIMIZAÇÃO V8.0: Esta função agora lê linha por linha (Stream)
void serveIndexHtml(String token, String alert, int fTime, int fQty) {
    File file = LittleFS.open("/index.html", "r");
    if (!file) {
        server.send(500, "text/plain", "ERRO: index.html nao encontrado. (Alimentador Mimi WiFi)");
        return;
    }

    // --- Envio Otimizado (Stream) ---
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.sendHeader("Cache-Control", "public, max-age=60");
    server.sendHeader("Connection", "keep-alive");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", ""); // Envia cabeçalhos

    // V19.0: Usa os dados espelhados do Mega para o countdown
    String nextTimeDisplay = getNextFeedTimeDisplay(megaLastFeedHour, FEED_INTERVAL_HOURS);

    String line;
    while (file.available()) {
        line = file.readStringUntil('\n');

        // Substituições em tempo real
        line.replace("[LAST_TOKEN_VALUE]", ""); 
        line.replace("[FEED_TIME]", String(fTime));
        line.replace("[FEED_QUANTITY]", String(fQty));
        line.replace("[NEXT_FEED_TIME]", nextTimeDisplay);
        line.replace("", ""); 

        server.sendContent(line + "\n"); 
        yield(); 
    }

    file.close();
    server.sendContent(""); 
}

// ROTA: / (Página Inicial)
void handleRoot() {
    // V19.0: Usa as variáveis espelhadas do Mega
    serveIndexHtml("", "", FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: /feed/now 
void handleFeedNow() {
    String token = server.arg("token");
    String alert = "";

    if (isTokenValid(token)) {
        send_feed_now();
        // V19.0: O ESP espera o Mega enviar "LAST_HOUR=..." de volta.
        alert = "SUCESSO: Alimento dispensado manualmente! Comando enviado: FEED_NOW";
    } else {
        alert = "ERRO: Token invalido ou nao fornecido.";
    }

    send_feedback_and_refresh(alert, token, FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: /feedTime 
void handleFeedTime() {
    String pathValue = server.arg("value");
    String token = server.arg("token");
    String alert = "";
    int newTime = pathValue.toInt();

    if (!isTokenValid(token)) {
        alert = "ERRO: Token invalido ou nao fornecido.";
    } else if (newTime >= 1 && newTime <= 24) {
        send_time_interval(newTime);
        alert = "SUCESSO: Intervalo de alimentacao definido para " + String(newTime) + " horas.";
    } else {
        alert = "ERRO: Valor de intervalo deve ser entre 1 e 24 horas.";
    }

    send_feedback_and_refresh(alert, token, FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: /feedQuantity
void handleFeedQuantity() {
    String pathValue = server.arg("value");
    String token = server.arg("token");
    String alert = "";
    int newQty = pathValue.toInt();

    if (!isTokenValid(token)) {
        alert = "ERRO: Token invalido ou nao fornecido.";
    } else if (newQty >= 100 && newQty <= 5000) {
        send_feed_duration(newQty);
        alert = "SUCESSO: Duracao da alimentacao definida para " + String(newQty) + " ms.";
    } else {
        alert = "ERRO: Valor de duracao deve ser entre 100 e 5000 ms.";
    }

    send_feedback_and_refresh(alert, token, FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: Não Encontrada
void handleNotFound() {
    server.send(404, "text/plain", "404: Nao Encontrado.");
}